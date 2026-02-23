"""Optimization/ISA experiment: compare compile flags on a compute kernel.

Requirement target (Linux/GCC/Clang):
- -O2
- -O3
- -O3 -march=native

This script builds three separate build directories, runs the compute kernel
(`--kernel dot` by default), and writes a summary CSV/MD table.

Outputs under results/summary/:
- opt_experiment_<stamp>.csv
- opt_experiment_<stamp>.md

On Windows (or missing GCC/Clang), it writes a clear BLOCKED note.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True)


def read_gflops(json_path: Path) -> float:
    doc = json.loads(json_path.read_text(encoding="utf-8"))
    return float(((doc.get("stats") or {}).get("performance") or {}).get("gflops") or 0.0)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build and run optimization flag experiment (Linux).")
    ap.add_argument("--kernel", type=str, default="dot", help="Compute kernel to run (dot/saxpy/fma/flops)")
    ap.add_argument("--size", type=str, default="64MB", help="Problem size for compute kernels")
    ap.add_argument("--iters", type=int, default=50)
    ap.add_argument("--warmup", type=int, default=10)
    args = ap.parse_args()

    out_dir = ROOT / "results" / "summary"
    out_dir.mkdir(parents=True, exist_ok=True)

    if os.name != "posix":
        (out_dir / "opt_experiment_BLOCKED.txt").write_text(
            "Optimization experiment requires a Linux toolchain (clang++ or g++) to build with -O2/-O3/-march=native.\n"
            "Suggested: run under native Linux or WSL2.\n",
            encoding="utf-8",
        )
        return 2

    # Require a GCC/Clang-style compiler in PATH.
    cxx = shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        (out_dir / "opt_experiment_BLOCKED.txt").write_text(
            "No clang++/g++ found in PATH. Install a Linux compiler toolchain and retry.\n",
            encoding="utf-8",
        )
        return 2

    variants = [
        ("O2", "-O2"),
        ("O3", "-O3"),
        ("O3_native", "-O3 -march=native"),
    ]

    s = stamp()
    rows = []

    for name, flags in variants:
        build_dir = ROOT / f"build_opt_{name}"
        build_dir.mkdir(parents=True, exist_ok=True)

        cfg = run(
            [
                "cmake",
                "-S",
                str(ROOT),
                "-B",
                str(build_dir),
                f"-DCMAKE_CXX_COMPILER={cxx}",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
            ],
            cwd=ROOT,
        )
        if cfg.returncode != 0:
            (out_dir / f"opt_experiment_{s}_{name}_cmake.txt").write_text(cfg.stdout + "\n" + cfg.stderr, encoding="utf-8")
            return cfg.returncode

        b = run(["cmake", "--build", str(build_dir)], cwd=ROOT)
        if b.returncode != 0:
            (out_dir / f"opt_experiment_{s}_{name}_build.txt").write_text(b.stdout + "\n" + b.stderr, encoding="utf-8")
            return b.returncode

        bench = build_dir / "bench"
        if not bench.exists():
            # Some generators might place it elsewhere; shallow search.
            matches = list(build_dir.glob("**/bench"))
            bench = matches[0] if matches else bench

        out_json = out_dir / f"opt_{args.kernel}_{name}_{s}.json"
        r = run(
            [
                str(bench),
                "--kernel",
                args.kernel,
                "--size",
                args.size,
                "--warmup",
                str(args.warmup),
                "--iters",
                str(args.iters),
                "--out",
                str(out_json),
            ],
            cwd=ROOT,
        )
        (out_dir / f"opt_experiment_{s}_{name}_run.txt").write_text(r.stdout + "\n" + r.stderr, encoding="utf-8")
        if r.returncode != 0:
            return r.returncode

        gflops = read_gflops(out_json)
        rows.append({"variant": name, "flags": flags, "kernel": args.kernel, "size": args.size, "gflops": gflops})

    csv_path = out_dir / f"opt_experiment_{s}.csv"
    md_path = out_dir / f"opt_experiment_{s}.md"

    import csv

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # Markdown table
    lines = ["# Optimization Experiment\n", "\n", "| Variant | Flags | Kernel | Size | GFLOP/s |", "|---|---|---|---|---:|"]
    for r in rows:
        lines.append(f"| {r['variant']} | `{r['flags']}` | {r['kernel']} | {r['size']} | {r['gflops']:.3f} |")
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"[run_opt_experiment] Wrote: {csv_path}")
    print(f"[run_opt_experiment] Wrote: {md_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
