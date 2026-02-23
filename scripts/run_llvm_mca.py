"""Run LLVM-MCA on a tiny extracted compute kernel (Linux-only).

LLVM-MCA analyzes throughput/latency of an instruction stream. This script:
- generates a small C++ file with a tight loop (dot-like)
- compiles it to assembly with clang
- runs llvm-mca on the assembly

Outputs under results/llvm-mca/:
- kernel_<stamp>.cpp
- kernel_<stamp>.s
- llvm_mca_<stamp>.txt

Usage:
    python scripts/run_llvm_mca.py

Notes:
- Requires: clang++, llvm-mca (from LLVM toolchain)
- On non-Linux, writes a BLOCKED note.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


KERNEL_CPP = r"""
#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
extern "C" double dot_kernel(const double* __restrict x, const double* __restrict y, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}
"""


def main() -> int:
    out_dir = ROOT / "results" / "llvm-mca"
    out_dir.mkdir(parents=True, exist_ok=True)

    if os.name != "posix":
        (out_dir / "llvm_mca_BLOCKED.txt").write_text(
            "LLVM-MCA run is intended for Linux with an LLVM toolchain installed (clang++ + llvm-mca).\n",
            encoding="utf-8",
        )
        return 2

    clang = shutil.which("clang++")
    mca = shutil.which("llvm-mca")
    if not clang or not mca:
        (out_dir / "llvm_mca_BLOCKED.txt").write_text(
            "Missing tools: clang++ and/or llvm-mca not found in PATH. Install LLVM and retry.\n",
            encoding="utf-8",
        )
        return 2

    s = stamp()
    cpp_path = out_dir / f"kernel_{s}.cpp"
    asm_path = out_dir / f"kernel_{s}.s"
    out_txt = out_dir / f"llvm_mca_{s}.txt"

    cpp_path.write_text(KERNEL_CPP.strip() + "\n", encoding="utf-8")

    # Compile to assembly. Use -O3 and native ISA for realistic scheduling.
    compile_cmd = [
        clang,
        "-O3",
        "-march=native",
        "-S",
        "-masm=intel",
        "-fno-asynchronous-unwind-tables",
        "-fno-exceptions",
        "-fno-rtti",
        str(cpp_path),
        "-o",
        str(asm_path),
    ]

    c = subprocess.run(compile_cmd, cwd=str(ROOT), text=True, capture_output=True)
    if c.returncode != 0:
        out_txt.write_text("clang++ failed\n\n" + (c.stdout or "") + "\n" + (c.stderr or ""), encoding="utf-8")
        return c.returncode

    # Run llvm-mca on the whole file; users can refine to labels if desired.
    m = subprocess.run([mca, str(asm_path)], cwd=str(ROOT), text=True, capture_output=True)
    out_txt.write_text((m.stdout or "") + "\n" + (m.stderr or ""), encoding="utf-8")

    print(f"[run_llvm_mca] Wrote: {out_txt}")
    return m.returncode


if __name__ == "__main__":
    raise SystemExit(main())
