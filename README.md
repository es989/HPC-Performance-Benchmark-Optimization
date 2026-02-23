# HPC-Performance-Benchmark-Optimization

A C++17 microbenchmark suite for measuring **memory bandwidth** across the memory hierarchy (L1/L2/LLC/DRAM) using STREAM-like kernels, with robust statistics exported to JSON.

---

## Quick Start

### Build (out-of-source CMake)

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

On Windows/MSVC, you can also build an unoptimized Debug binary during development:

```powershell
cd build
cmake --build . --config Debug
```

### Quick sanity check (2 seconds)
Use a tiny run to verify the binary works and produces output:

```bash
./bench --kernel copy --warmup 2 --iters 5
```

### End-to-end suite (recommended)
This runs a minimal, reproducible pipeline:
**build → repeated runs → aggregate → generate 2 plots**.

Install Python plotting deps once:

```powershell
.\.venv\Scripts\pip.exe install -r scripts\requirements.txt
```

```powershell
.\.venv\Scripts\python.exe scripts\run_suite.py --config Release --repeats 3 --warmup 10 --iters 50 --prefault
```

Outputs:
- `results/raw/*.json` (per-run raw JSON + captured stdout/stderr)
- `results/summary/*_agg.json` + `results/summary/*_agg.csv` (aggregated)
- `plots/*.png` (bandwidth vs size + latency vs size)

### Run a stable baseline (recommended)

```bash
./bench --kernel triad --warmup 50 --iters 200 --out results.json
```
*Output is written to `--out` (default: `results.json`).*

---

## Build Modes

This project supports two primary CMake configurations:

- **Debug**  
  Built with `cmake --build . --config Debug`  
  Slower (no `-O3`), includes debug info, best for development and checking correctness.

- **Release**  
  Built with `cmake --build . --config Release` (after `cmake .. -DCMAKE_BUILD_TYPE=Release`)  
  Optimized; this is what you should use for real performance measurements.

---

## Kernel Modes

Kernels are selected with `--kernel` and fall into two categories:

- **Memory-oriented**: `copy`, `scale`, `add`, `triad`  
  Aliases: `stream_copy`, `stream_scale`, `stream_add`, `stream_triad`.

- **Compute-oriented**: `flops`, `fma`, `dot`, `saxpy`

- **Latency-oriented**: `latency` (pointer-chasing dependent-load sweep)

---

## Context & Purpose
This project provides a reproducible environment to quantify system performance across different memory hierarchies (L1/L2/LLC/DRAM) and compute kernels. It helps identify bottlenecks and the impact of targeted optimizations. Results are written to a machine-readable JSON file for post-processing and plotting.

---

## CLI Arguments
Defaults are defined in `include/config.hpp`.

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--kernel` | Kernel to run: `copy`, `scale`, `add`, `triad` | `copy` |
| `--size` | Dataset/problem size string (used by some modes) | `64MB` |
| `--threads` | Number of worker threads (must be >= 1) | `1` |
| `--iters` | Measured iterations per sweep point | `100` |
| `--warmup` | Unmeasured warmup iterations | `10` |
| `--out` | Output JSON path | `results.json` |
| `--seed` | RNG seed | `14` |
| `--prefault` | Pre-touch pages to reduce first-touch/page-fault noise | off |
| `--aligned` | Use 64B-aligned allocations where supported | off |
| `--help` | Print help |  |

> **Note:** For stable percentile statistics, prefer `--iters 200` (or higher), especially on Windows.

---

## Output & Metrics (JSON)
Each sweep point includes:
* `bytes`
* `median_ns`, `p95_ns`, `min_ns`, `max_ns`, `stddev_ns`
* `bandwidth_gb_s`
* `checksum`

**Why these metrics?**
* **Median**: typical performance (robust to outliers)
* **P95**: tail latency (interrupts / OS jitter)
* **Stddev + CV** (`stddev/median`): measurement stability
* **Min/Max**: best/worst observed iteration times

---

## Methodology (The Performance Contract)
Microbenchmarks are sensitive to system noise. This project aims to make results comparable by:

* **Warmup**: primes caches and stabilizes DVFS (frequency scaling)
* **Statistical rigor**: reports Median (typical) + P95 (tail) + Stddev (variance)
* **Hardware control**: pinning reduces core migration effects
* **Optional prefaulting**: can move first-touch page faults out of the timed region

### How to get stable measurements
If different runs look "best" at different sizes, that typically indicates OS noise (interrupts, scheduling jitter, DVFS, core migration).

**Recommended run settings**
* **Windows / WSL**: `--warmup 50 --iters 200` (or higher)
* **Linux**: defaults often work, but higher iters improves percentile stability

**Stability checklist (what usually matters)**
- Use warmup (`--warmup`) to stabilize cache/DVFS.
- Increase measured iters (`--iters`) for stable median/P95.
- Pin to one core (affinity) to reduce core migration.
- Use `--prefault` if you suspect first-touch / page-fault spikes.

For a copy/paste Windows command (affinity + high priority + prefault), see **Stable runs (Windows / low jitter)** below.

**Best practice: multiple trials**
Run 3–5 trials and report median-of-medians per size for reliable comparisons.

<!-- BENCH_STABILITY_START -->
## Stable runs (Windows / low jitter)

Microbenchmarks are sensitive to OS scheduling jitter. On Windows, use CPU pinning + high priority for more stable median/P95.

### Recommended stable run (Windows)
From `build\Release`:

```bat
C:\Windows\System32\cmd.exe /c start "" /wait /affinity 1 /high bench.exe --kernel triad --prefault --warmup 50 --iters 200 --out ..\..\results\triad_aff1.json
```

Notes:
- `/affinity 1` pins to CPU0 (use `2` for CPU1, `4` for CPU2, etc.)
- `--warmup N` is not timed (stabilizes CPU/cache/DVFS)
- `--iters N` is timed per sweep point (higher = more stable percentiles)
- `--prefault` pre-touches pages to reduce first-touch / page-fault noise

Linux equivalent:

```bash
taskset -c 0 ./bench --kernel triad --prefault --warmup 50 --iters 200 --out results/triad_core0.json
```
<!-- BENCH_STABILITY_END -->

<!-- PLOTTING_START -->
## Plotting (bandwidth waterfall)

This repo provides two annotation modes:

- **clean**: conservative labels; only calls a cache boundary if the knee is near OS-reported cache sizes.
- **research**: labels statistically significant drops as `drop #k (-X GB/s, -Y%)` without claiming cache levels.

Examples:

```powershell
python scripts\plot_bandwidth_vs_size.py .\results\triad_aff1.json --mode clean
python scripts\plot_bandwidth_vs_size.py .\results\triad_aff1.json --mode research --max-knees 5
```

Optional (pandas): export CSV tables for reporting/analysis:

```powershell
pip install -r scripts\requirements.txt
python scripts\plot_bandwidth_vs_size.py .\results\triad_aff1.json --mode research --export-csv --export-drops-csv
```

This writes:
* `plots\sweep_<kernel>.csv` (per-size sweep points)
* `plots\drops_<kernel>_<mode>.csv` (detected knees/drops)

If you want a minimal Plot #1 implemented explicitly with **pandas + matplotlib** (for portfolio alignment), use:

```powershell
pip install -r scripts\requirements.txt
python scripts\plot_results.py .\results\triad_aff1.json
```
<!-- PLOTTING_END -->

---

## Spec Assumptions (Reproducibility)
To make results comparable across machines and runs, record these environmental assumptions:

* **Huge Pages / THP (Linux)**: Transparent Huge Pages (THP) can significantly affect TLB behavior and bandwidth. Record whether THP is enabled:
  ```bash
  cat /sys/kernel/mm/transparent_hugepage/enabled
  ```
* **ISA targeting**: On GCC/Clang, Release builds may enable CPU-specific ISA via `-march=native` (e.g., AVX2 / AVX-512 if available). Record CPU model and supported ISA when comparing results across systems.

---

## Expected Results (What you should see)
Bandwidth typically forms a "waterfall":
1. Very high in L1/L2
2. Drops at LLC
3. Drops again at DRAM

Bandwidth peaks at small working sets (cache-resident), then drops sharply once the effective working set (3× arrays for triad) exceeds L2 capacity, and later transitions toward a DRAM-bound plateau.

In research mode, we mark only statistically significant drops; clean mode labels cache boundaries only when the knee is near OS-reported cache sizes.

Cache boundaries are approximate (OS-reported; per-core vs shared). Additional knees may reflect TLB/page effects, prefetch behavior, or memory-controller saturation.
Non-monotonic "bumps" can also occur due to OS noise / DVFS, prefetcher behavior, or page/TLB effects, so interpret individual knees cautiously.

**Ballpark ranges (highly CPU-dependent):**

Ballpark only; depends heavily on kernel, ISA, and measurement method.

| Memory level | Typical bandwidth (GB/s) |
| :--- | :--- |
| **L1 cache** | ~200–400 |
| **L2 cache** | ~100–200 |
| **LLC** | ~30–100 |
| **DRAM (1 thread)** | ~10–25 |
| **DRAM (multi-thread)** | ~20–50+ |

---

## Platform Baseline (example machine)
* **CPU**: Intel i7-12700H (14 cores / 20 threads)
* **OS**: Linux (native) / WSL2 / Windows
* **Compiler**: GCC/Clang/MSVC
* **RAM**: 16GB DDR5

---

## Project Structure
* `src/` – benchmark executable, STREAM kernels, sweep runner
* `include/` – config, timers, kernel implementations, utilities, JSON output
* `scripts/` – PowerShell runners and post-processing helpers
* `results/` – saved outputs (JSON/CSV)
* `plots/` – visualization assets (optional)
* `.github/` – CI/CD and documentation templates

## Report
See `REPORT.md` for a minimal, reproducible run recipe and the artifact contract (where outputs live and what they mean).

## Profiling & Opt Experiments (best-effort)
Linux-focused helpers live in `scripts/`:
- `run_perf_stat.py`, `run_valgrind_cachegrind.py`, `run_llvm_mca.py`, `run_opt_experiment.py`

On non-Linux (e.g., Windows), the suite writes explicit `*_BLOCKED.txt` notes under `results/perf/`, `results/valgrind/`, and `results/llvm-mca/` when these tools aren’t available.

---

## Limitations & Warnings
* **WSL2**: performance counters via `perf` may be limited.
* **Thermal throttling**: long sweeps can heat the CPU and change frequency.
* **Memory pressure**: multi-array kernels need enough free RAM to avoid swapping/paging.
* **Hardware-specific builds**: Release builds may be CPU-tuned on GCC/Clang; rebuild per machine.
