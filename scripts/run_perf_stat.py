"""Run `perf stat` around the benchmark command (Linux-only) and save outputs.

Minimum counters:
- cycles
- instructions
- cache-misses
- LLC-load-misses (best-effort; may vary by CPU/perf version)

Outputs under results/perf/:
- perf_stat_<stamp>.txt   (raw perf output)
- perf_stat_<stamp>.json  (parsed summary + IPC)

Usage:
    python scripts/run_perf_stat.py -- ./build/bench --kernel dot --size 64MB --warmup 10 --iters 50 --out results/raw/dot.json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


DEFAULT_EVENTS = [
    "cycles",
    "instructions",
    "cache-misses",
    "LLC-load-misses",
]


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


def parse_perf_stat(text: str) -> dict:
    # Very small parser for perf stat human output.
    # Example lines:
    #   1,234,567      cycles
    #   2,345,678      instructions              #    1.90  insn per cycle
    out: dict[str, float] = {}

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        # Match: <number> <event>
        m = re.match(r"^([0-9,\.]+)\s+([A-Za-z0-9\-:_.]+)", line)
        if not m:
            continue

        raw_num = m.group(1).replace(",", "")
        event = m.group(2)
        try:
            val = float(raw_num)
        except ValueError:
            continue

        out[event] = val

    cycles = out.get("cycles")
    instr = out.get("instructions")
    if cycles and instr and cycles > 0:
        out["IPC"] = instr / cycles

    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Run perf stat and save raw + parsed outputs.")
    ap.add_argument("--events", type=str, default=",".join(DEFAULT_EVENTS), help="Comma-separated perf events")
    ap.add_argument("cmd", nargs=argparse.REMAINDER, help="Command to run after --")

    args = ap.parse_args()

    if os.name != "posix" or shutil.which("perf") is None:
        out_dir = ROOT / "results" / "perf"
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "perf_stat_BLOCKED.txt").write_text(
            "perf stat is not available. Run this script on Linux with perf installed and permissions enabled.\n",
            encoding="utf-8",
        )
        return 2

    if not args.cmd or args.cmd[0] != "--":
        ap.error("Usage: python scripts/run_perf_stat.py -- <command...>")

    cmd = args.cmd[1:]
    out_dir = ROOT / "results" / "perf"
    out_dir.mkdir(parents=True, exist_ok=True)

    s = stamp()
    raw_path = out_dir / f"perf_stat_{s}.txt"
    json_path = out_dir / f"perf_stat_{s}.json"

    events = [e.strip() for e in args.events.split(",") if e.strip()]

    perf_cmd = ["perf", "stat", "-e", ",".join(events), "--"] + cmd

    completed = subprocess.run(perf_cmd, cwd=str(ROOT), text=True, capture_output=True)

    # perf writes stats to stderr by default
    raw = (completed.stderr or "") + "\n" + (completed.stdout or "")
    raw_path.write_text(raw, encoding="utf-8")

    parsed = {
        "cmd": cmd,
        "events": events,
        "returncode": completed.returncode,
        "parsed": parse_perf_stat(raw),
    }
    json_path.write_text(json.dumps(parsed, indent=4), encoding="utf-8")

    print(f"[run_perf_stat] Wrote: {raw_path}")
    print(f"[run_perf_stat] Wrote: {json_path}")

    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
