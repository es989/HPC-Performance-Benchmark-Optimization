"""Run Valgrind Cachegrind on a benchmark command (Linux-only) and save outputs.

Outputs under results/valgrind/:
- cachegrind_<stamp>.out
- cachegrind_<stamp>.txt (human summary via cg_annotate if available)

Usage:
    python scripts/run_valgrind_cachegrind.py -- ./build/bench --kernel dot --size 64MB --warmup 2 --iters 5 --out results/raw/dot.json
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


def main() -> int:
    ap = argparse.ArgumentParser(description="Run valgrind --tool=cachegrind and save outputs.")
    ap.add_argument("cmd", nargs=argparse.REMAINDER, help="Command to run after --")
    args = ap.parse_args()

    out_dir = ROOT / "results" / "valgrind"
    out_dir.mkdir(parents=True, exist_ok=True)

    if os.name != "posix" or shutil.which("valgrind") is None:
        (out_dir / "valgrind_cachegrind_BLOCKED.txt").write_text(
            "valgrind/cachegrind not available. Run on Linux with valgrind installed.\n",
            encoding="utf-8",
        )
        return 2

    if not args.cmd or args.cmd[0] != "--":
        ap.error("Usage: python scripts/run_valgrind_cachegrind.py -- <command...>")

    cmd = args.cmd[1:]
    s = stamp()
    out_file = out_dir / f"cachegrind_{s}.out"

    vg_cmd = [
        "valgrind",
        "--tool=cachegrind",
        f"--cachegrind-out-file={out_file}",
        "--",
    ] + cmd

    completed = subprocess.run(vg_cmd, cwd=str(ROOT), text=True, capture_output=True)

    # Save valgrind stdout/stderr
    (out_dir / f"cachegrind_{s}.stdout.txt").write_text(completed.stdout or "", encoding="utf-8")
    (out_dir / f"cachegrind_{s}.stderr.txt").write_text(completed.stderr or "", encoding="utf-8")

    # Human annotation (best-effort)
    if shutil.which("cg_annotate") is not None:
        ann = subprocess.run(["cg_annotate", str(out_file)], cwd=str(ROOT), text=True, capture_output=True)
        (out_dir / f"cachegrind_{s}.txt").write_text((ann.stdout or "") + "\n" + (ann.stderr or ""), encoding="utf-8")

    print(f"[run_valgrind_cachegrind] Wrote: {out_file}")
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
