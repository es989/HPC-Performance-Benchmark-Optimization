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

### Quick sanity check (2 seconds)
Use a tiny run to verify the binary works and produces output:

```bash
./bench --kernel copy --warmup 2 --iters 5
```

### Run a stable baseline (recommended)

```bash
./bench --kernel triad --warmup 50 --iters 200 --out results.json
```
*Output is written to `--out` (default: `results.json).*

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

**Pin to one core (Affinity) + High priority**

*Linux:*
```bash
taskset -c 0 ./bench --kernel copy --warmup 50 --iters 200
```

*Windows (PowerShell):*
```powershell
cd .\build\Release
cmd /c 'start /wait "" /affinity 1 /high bench.exe --kernel copy --warmup 50 --iters 200'
```

**Optional: --prefault (pre-touch pages)**
If you observe large `max_ns` / `p95_ns` spikes consistent with first-touch/page-fault effects, enable `--prefault`:
```bash
./bench --kernel copy --warmup 50 --iters 200 --prefault
```

**Best practice: multiple trials**
Run 3–5 trials and report median-of-medians per size for reliable comparisons.

---

## Spec Assumptions (Reproducibility)
To make results comparable across machines and runs, record these environmental assumptions:

* **Huge Pages / THP (Linux)**: Transparent Huge Pages (THP) can significantly affect TLB behavior and bandwidth. Record whether THP is enabled:
  ```bash
  cat /sys/kernel/mm/transparent_hugepage/enabled
  ```
* **ISA targeting (`-march=native`)**: Release builds may enable CPU-specific vector ISA via `-march=native` (e.g., AVX2 / AVX-512 if available). Record CPU model and supported ISA when comparing results across systems.

---

## Expected Results (What you should see)
Bandwidth typically forms a "waterfall":
1. Very high in L1/L2
2. Drops at LLC
3. Drops again at DRAM

**Ballpark ranges (highly CPU-dependent):**

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

---

## Limitations & Warnings
* **WSL2**: performance counters via `perf` may be limited.
* **Thermal throttling**: long sweeps can heat the CPU and change frequency.
* **Memory pressure**: multi-array kernels need enough free RAM to avoid swapping/paging.
* **Hardware-specific builds**: Release builds may use `-march=native`; rebuild per machine.
