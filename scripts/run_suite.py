"""End-to-end local benchmarking pipeline: build -> run -> aggregate -> plot.

Goals:
- Minimal, reliable workflow suitable for portfolio/resume claims.
- Repeated runs (process-level) with aggregation (median-of-medians).
- Generates at least two plots:
  1) Bandwidth vs working-set size (STREAM triad)
  2) Latency vs working-set size (pointer chasing)

Usage (Windows PowerShell):
    python scripts/run_suite.py --config Release --repeats 3

Usage (Linux):
    python3 scripts/run_suite.py --repeats 3

Outputs:
- results/raw/*.json           (per-run raw JSON)
- results/summary/*_agg.json   (aggregated JSON)
- results/summary/*_agg.csv    (aggregated CSV)
- results/system/*             (tool + environment snapshot)
- plots/*.png                  (Plot #1 + Plot #2)

Profiling (perf/valgrind/llvm-mca) is best-effort and OS/tool dependent; this
script will emit a clear "blocked" note when unavailable.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def now_stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


def run(cmd: list[str], *, cwd: Path, log_lines: list[str], timeout: int | None = None) -> subprocess.CompletedProcess:
    log_lines.append("$ " + " ".join(cmd))
    return subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True, timeout=timeout)


def ensure_dirs():
    for p in [
        ROOT / "results" / "raw",
        ROOT / "results" / "summary",
        ROOT / "results" / "system",
        ROOT / "results" / "perf",
        ROOT / "results" / "valgrind",
        ROOT / "results" / "llvm-mca",
        ROOT / "plots",
    ]:
        p.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def detect_bench_path(build_dir: Path, config: str) -> Path:
    if os.name == "nt":
        cand = build_dir / config / "bench.exe"
        if cand.exists():
            return cand

    # common single-config
    for cand in [build_dir / "bench", build_dir / "bench.exe"]:
        if cand.exists():
            return cand

    # Try a shallow search
    matches = list(build_dir.glob("**/bench.exe")) + list(build_dir.glob("**/bench"))
    matches = [m for m in matches if m.is_file()]
    if matches:
        return matches[0]

    raise FileNotFoundError(f"bench executable not found under {build_dir}")


def cmake_build(build_dir: Path, config: str, *, log_lines: list[str]) -> None:
    # Configure (best-effort) if cache missing.
    if not (build_dir / "CMakeCache.txt").exists():
        r = run(["cmake", "-S", str(ROOT), "-B", str(build_dir)], cwd=ROOT, log_lines=log_lines)
        if r.returncode != 0:
            raise RuntimeError(f"cmake configure failed:\n{r.stdout}\n{r.stderr}")

    # Build
    cmd = ["cmake", "--build", str(build_dir)]
    if os.name == "nt":
        cmd += ["--config", config]

    r = run(cmd, cwd=ROOT, log_lines=log_lines)
    if r.returncode == 0:
        return

    combined = (r.stdout or "") + "\n" + (r.stderr or "")

    # Windows: a common failure mode is that bench.exe is still running
    # (e.g., a previous run hung or was interrupted), preventing the linker
    # from overwriting the output binary.
    if os.name == "nt" and "LNK1104" in combined and "bench.exe" in combined:
        log_lines.append("[run_suite] Detected LNK1104 for bench.exe; attempting to stop running bench.exe and retry build")

        # Best-effort: stop by image name. We intentionally avoid extra deps.
        _ = run(["taskkill", "/F", "/T", "/IM", "bench.exe"], cwd=ROOT, log_lines=log_lines)

        r2 = run(cmd, cwd=ROOT, log_lines=log_lines)
        if r2.returncode == 0:
            return
        raise RuntimeError(f"cmake build failed after retry:\n{r2.stdout}\n{r2.stderr}")

    raise RuntimeError(f"cmake build failed:\n{r.stdout}\n{r.stderr}")


def snapshot_system(out_dir: Path, *, log_lines: list[str]) -> None:
    info = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "python": sys.version,
        "platform": {
            "os_name": os.name,
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
            "processor": platform.processor(),
        },
    }

    # Tool versions (best-effort)
    tools = [
        ("cmake", ["cmake", "--version"]),
    ]
    for name, cmd in tools:
        exe = shutil.which(cmd[0])
        if not exe:
            info.setdefault("tools", {})[name] = {"available": False}
            continue
        r = run(cmd, cwd=ROOT, log_lines=log_lines)
        info.setdefault("tools", {})[name] = {
            "available": True,
            "stdout": (r.stdout or "").strip(),
            "stderr": (r.stderr or "").strip(),
            "returncode": r.returncode,
        }

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "system.json").write_text(json.dumps(info, indent=4), encoding="utf-8")


def run_bench(bench: Path, args: list[str], out_json: Path, *, log_lines: list[str], timeout: int | None = None) -> None:
    cmd = [str(bench)] + args + ["--out", str(out_json)]
    r = run(cmd, cwd=ROOT, log_lines=log_lines, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError(f"bench failed (rc={r.returncode}):\n{r.stdout}\n{r.stderr}")

    # Record raw stdout/stderr alongside the JSON for reproducibility/debugging.
    write_text(out_json.with_suffix(out_json.suffix + ".stdout.txt"), r.stdout or "")
    write_text(out_json.with_suffix(out_json.suffix + ".stderr.txt"), r.stderr or "")


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def aggregate_runs(inputs: list[Path], out_json: Path, out_csv: Path) -> None:
    from collections import defaultdict
    from statistics import median
    import csv

    docs = [load_json(p) for p in inputs]
    if not docs:
        raise ValueError("No input docs")

    base = docs[0]
    grouped: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for doc in docs:
        sweep = (doc.get("stats") or {}).get("sweep") or []
        for pt in sweep:
            kernel = str(pt.get("kernel") or "unknown")
            bytes_ = int(pt.get("bytes") or 0)
            grouped[(kernel, bytes_)].append(pt)

    def mean(xs: list[float]) -> float:
        xs = [float(x) for x in xs if x is not None]
        return sum(xs) / len(xs) if xs else 0.0

    sweep_rows: list[dict] = []
    csv_rows: list[dict] = []

    for (kernel, bytes_), pts in sorted(grouped.items(), key=lambda t: (t[0][0], t[0][1])):
        medians = [float(p.get("median_ns") or 0.0) for p in pts]
        p95s = [float(p.get("p95_ns") or 0.0) for p in pts]
        stddevs = [float(p.get("stddev_ns") or 0.0) for p in pts]
        bws = [float(p.get("bandwidth_gb_s") or 0.0) for p in pts]
        nspa = [float(p.get("ns_per_access") or 0.0) for p in pts if "ns_per_access" in p]

        row = {
            "kernel": kernel,
            "bytes": bytes_,
            "median_ns": float(median(medians)) if medians else 0.0,
            "p95_ns": mean(p95s),
            "min_ns": min(float(p.get("min_ns") or 0.0) for p in pts),
            "max_ns": max(float(p.get("max_ns") or 0.0) for p in pts),
            "stddev_ns": mean(stddevs),
            "bandwidth_gb_s": float(median(bws)) if bws else 0.0,
            "checksum": float(median([float(p.get("checksum") or 0.0) for p in pts])),
            "runs": len(pts),
        }
        if nspa:
            row["ns_per_access"] = float(median(nspa))

        csv_rows.append(row)
        sweep_rows.append({k: v for k, v in row.items() if k != "runs"})

    out_doc = {
        "metadata": base.get("metadata", {}),
        "config": base.get("config", {}),
        "aggregation": {
            "runs": len(docs),
            "inputs": [str(p) for p in inputs],
        },
        "stats": {
            "performance": base.get("stats", {}).get("performance", {}),
            "sweep": sweep_rows,
        },
    }

    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(out_doc, indent=4), encoding="utf-8")

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    if csv_rows:
        cols = list(csv_rows[0].keys())
        with out_csv.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for row in csv_rows:
                w.writerow(row)


def plot_all(stream_json: Path, latency_json: Path, *, log_lines: list[str]) -> None:
    # Plot #1
    r1 = run(
        [sys.executable, str(ROOT / "scripts" / "plot_results.py"), str(stream_json)],
        cwd=ROOT,
        log_lines=log_lines,
    )
    if r1.returncode != 0:
        raise RuntimeError(f"plot_results.py failed:\n{r1.stdout}\n{r1.stderr}")

    # Plot #2
    r2 = run(
        [sys.executable, str(ROOT / "scripts" / "plot_latency_vs_size.py"), str(latency_json)],
        cwd=ROOT,
        log_lines=log_lines,
    )
    if r2.returncode != 0:
        raise RuntimeError(f"plot_latency_vs_size.py failed:\n{r2.stdout}\n{r2.stderr}")


def write_blocked_note(dir_: Path, name: str, reason: str):
    write_text(dir_ / f"{name}_BLOCKED.txt", reason.strip() + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build + run benchmark suite + aggregate + plot.")
    ap.add_argument("--build-dir", type=Path, default=ROOT / "build", help="CMake build directory")
    ap.add_argument("--config", type=str, default="Release", help="CMake config (Windows multi-config)")
    ap.add_argument("--skip-build", action="store_true", help="Skip CMake build step")
    ap.add_argument("--repeats", type=int, default=3, help="Number of repeated process-level runs")
    ap.add_argument("--iters", type=int, default=50, help="Measured iterations per sweep point")
    ap.add_argument("--warmup", type=int, default=10, help="Warmup iterations")
    ap.add_argument("--prefault", action="store_true", help="Enable --prefault")
    ap.add_argument("--aligned", action="store_true", help="Enable --aligned (compute/latency)")
    ap.add_argument("--skip-plots", action="store_true", help="Skip plot generation")

    args = ap.parse_args()

    ensure_dirs()

    log_lines: list[str] = []

    # 1) Build
    if not args.skip_build:
        cmake_build(args.build_dir, args.config, log_lines=log_lines)

    # 2) Locate binary
    bench = detect_bench_path(args.build_dir, args.config)

    # 3) Snapshot system/tools
    snapshot_system(ROOT / "results" / "system", log_lines=log_lines)

    # 4) Run benchmarks (repeats)
    stamp = now_stamp()

    def common_flags() -> list[str]:
        flags = ["--warmup", str(args.warmup), "--iters", str(args.iters)]
        if args.prefault:
            flags.append("--prefault")
        if args.aligned:
            flags.append("--aligned")
        return flags

    stream_runs: list[Path] = []
    latency_runs: list[Path] = []
    dot_runs: list[Path] = []

    for i in range(1, args.repeats + 1):
        stream_out = ROOT / "results" / "raw" / f"stream_triad_{stamp}_run{i}.json"
        run_bench(bench, ["--kernel", "triad"] + common_flags(), stream_out, log_lines=log_lines)
        stream_runs.append(stream_out)

        lat_out = ROOT / "results" / "raw" / f"latency_ptr_chase_{stamp}_run{i}.json"
        # Latency can take longer on large sizes; allow a longer timeout.
        run_bench(bench, ["--kernel", "latency"] + common_flags(), lat_out, log_lines=log_lines, timeout=None)
        latency_runs.append(lat_out)

        dot_out = ROOT / "results" / "raw" / f"compute_dot_{stamp}_run{i}.json"
        run_bench(bench, ["--kernel", "dot", "--size", "64MB"] + common_flags(), dot_out, log_lines=log_lines)
        dot_runs.append(dot_out)

    # 5) Aggregate
    stream_agg_json = ROOT / "results" / "summary" / f"stream_triad_agg_{stamp}.json"
    stream_agg_csv = ROOT / "results" / "summary" / f"stream_triad_agg_{stamp}.csv"
    aggregate_runs(stream_runs, stream_agg_json, stream_agg_csv)

    latency_agg_json = ROOT / "results" / "summary" / f"latency_ptr_chase_agg_{stamp}.json"
    latency_agg_csv = ROOT / "results" / "summary" / f"latency_ptr_chase_agg_{stamp}.csv"
    aggregate_runs(latency_runs, latency_agg_json, latency_agg_csv)

    dot_agg_json = ROOT / "results" / "summary" / f"compute_dot_agg_{stamp}.json"
    dot_agg_csv = ROOT / "results" / "summary" / f"compute_dot_agg_{stamp}.csv"
    aggregate_runs(dot_runs, dot_agg_json, dot_agg_csv)

    # 6) Plot
    if not args.skip_plots:
        try:
            import pandas  # noqa: F401
            import matplotlib  # noqa: F401
        except Exception as e:
            write_blocked_note(
                ROOT / "plots",
                "plotting",
                "Plotting dependencies missing. Install with:\n"
                "  pip install -r scripts/requirements.txt\n"
                f"Details: {e}",
            )
        else:
            plot_all(stream_agg_json, latency_agg_json, log_lines=log_lines)

    # 7) Best-effort profiling placeholders (Linux-only)
    if platform.system().lower() != "linux":
        write_blocked_note(
            ROOT / "results" / "perf",
            "perf_stat",
            "perf stat is Linux-only. Run under native Linux or WSL2 (may require permissions: /proc/sys/kernel/perf_event_paranoid).",
        )
        write_blocked_note(
            ROOT / "results" / "valgrind",
            "valgrind_cachegrind",
            "Valgrind (cachegrind) is Linux-focused. Run under native Linux for reliable results.",
        )
        write_blocked_note(
            ROOT / "results" / "llvm-mca",
            "llvm_mca",
            "LLVM-MCA requires llvm-mca + clang toolchain (typically on Linux). See scripts/run_llvm_mca.py for exact commands.",
        )

    # 8) Write command log
    write_text(ROOT / "results" / "system" / f"commands_{stamp}.log", "\n".join(log_lines) + "\n")

    print("[run_suite] Done")
    print(f"[run_suite] Aggregated STREAM:  {stream_agg_json}")
    print(f"[run_suite] Aggregated latency: {latency_agg_json}")
    print(f"[run_suite] Plots in:           {ROOT / 'plots'}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
