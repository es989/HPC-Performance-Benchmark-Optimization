# HPC-Performance-Benchmark-Optimization

A C++17 **multi-threaded** microbenchmark suite for measuring **memory bandwidth**, **memory latency**, and **compute throughput** across the full memory hierarchy (L1/L2/LLC/DRAM).

*   **Architecture-Aware:** Explicit NUMA first-touch initialization and thread pinning.
*   **Hardware Limit Testing:** AVX/AVX-512 vectorization with 4-way FMA unrolling for peak throughput.
*   **Measurement Purity:** Anti-Dead-Code-Elimination (DCE) checksums and hardware prefetcher defeat mechanisms.

##  Project Overview

This project is designed to measure and illustrate the theoretical hardware limits of a machine (or server) in three key areas:

1.  **Memory Bandwidth**: How fast the system can move data from memory (GB/s).
2.  **Memory Latency**: How long it takes to fetch a single datum from memory/cache (ns or cycles).
3.  **Compute Throughput**: How many floating-point calculations the CPU can perform (GFLOP/s).

**Industrial-Grade Design:**
*   **Single Binary**: Switches "modes" via CLI (e.g., `--kernel stream`/`latency`/`compute`).
*   **JSON Output**: Generates structured, parsable logs for easy analysis.
*   **Automation Suite**: Scripts for running experiment suites, result collection, and integration with analysis tools like `perf`, `valgrind`, and `llvm-mca`.
*   **Organized Results**: Dedicated folders for `raw`, `summary`, `perf`, `valgrind`, and `llvm-mca` outputs.

## Sample Output

### Memory Bandwidth

This graph illustrates the performance behavior of the memory subsystem across different hierarchy levels (L1, L2, LLC, DRAM). As the working set size likely exceeds the capacity of each cache level, the observed bandwidth drops, a trend consistent with the hardware's throughput constraints.

![Memory Bandwidth vs Size](assets/bandwidth_vs_size_triad.svg)

### Memory Latency

By employing a pointer-chasing benchmark (`--kernel latency`) with a randomized traversal pattern, this test aims to defeat hardware prefetchers and measure dependent memory access latency. The resulting stair-step pattern is consistent with the access latency penalties expected at each level of the cache hierarchy.

![Memory Latency vs Size](assets/latency_vs_size_ptr_chase.png)

## Methodology (The Performance Contract)

Microbenchmarks are sensitive to system noise. This project makes results comparable and accurate by:

* **NUMA First-Touch**: arrays initialized in `#pragma omp parallel for schedule(static)` blocks that match the kernel's thread layout, ensuring physical pages are local to the processing cores.
* **Parallel Prefault** (`--prefault`): page pre-commitment runs in parallel to preserve NUMA binding.
* **Warmup**: primes caches and stabilizes DVFS (CPU frequency scaling) before measurement.
* **Compiler Barriers**: `clobber_memory()` is placed before and after each kernel invocation to prevent instruction hoisting across `Timer` boundaries.
* **Anti-DCE checksums**: sampled array checksums are passed to `do_not_optimize_away()` after every iteration to block the compiler from eliding the kernel.
* **Statistical rigor**: reports Median (typical), P95 (tail), Stddev (variance/stability), Min/Max (outlier detection).

### How to get stable measurements

| Platform | Recommended command |
| :--- | :--- |
| Linux | `taskset -c 0-7 OMP_NUM_THREADS=8 ./bench --kernel triad --warmup 50 --iters 200 --prefault` |
| Windows | `set OMP_NUM_THREADS=8 && bench.exe --kernel triad --warmup 50 --iters 200 --prefault` |

**Stability checklist:**
- Set `OMP_NUM_THREADS` to your physical core count (avoid hyperthreads for bandwidth tests).
- Use `--warmup 50` or higher to stabilize frequency (DVFS) and cache state.
- Use `--iters 200` or higher for statistically stable median/P95.
- Use `--prefault` to move page-fault latency outside the timed region.
- Run 3–5 trials and report median-of-medians per size for reproducible comparisons.

## Architecture Overview

### OpenMP Parallelism
Every kernel except `latency` is fully multi-threaded via OpenMP 4.0+. All loops use `schedule(static)` for deterministic, balanced chunk assignment matching the NUMA initialization layout.

### NUMA-Aware First-Touch Initialization
All benchmark arrays are initialized inside `#pragma omp parallel for schedule(static)` blocks **before** measurement begins. This triggers the OS first-touch policy, assigning physical memory pages to the NUMA node and CPU socket that will later process them. Applied in both `stream_sweep.cpp` and `compute_bench.cpp`. The optional `--prefault` pass is also parallelized to preserve NUMA binding.

### SIMD Vectorization
`RESTRICT`-qualified pointer macros (`__restrict__` GCC/Clang, `__restrict` MSVC) are applied to all kernel arguments in `stream_kernels.hpp`. This guarantees the compiler generates packed AVX/AVX2/AVX-512 instructions without aliasing-check prologues. `#pragma omp simd` is avoided to maintain compatibility with MSVC's OpenMP 2.0 implementation.

### FMA Throughput Optimization
`compute_fma_kernel` and `compute_flops_kernel` use a 4-accumulator unrolled inner loop, replacing the previous single-scalar serial chain. This is required to measure FMA **throughput** (not latency). A single-scalar chain stalls every 4–5 cycles; 4 independent chains can issue 1 FMA per cycle per port.

### Aligned Memory (`include/aligned_buffer.hpp`)
`benchmark::AlignedBuffer<T>` is a move-only RAII allocator providing exact 64-byte aligned storage. The constructor validates that the requested alignment is a power of 2 before dispatching to `posix_memalign` (POSIX) or `_aligned_malloc` (Windows).

---

## Quick Start

### Build (out-of-source CMake)

**Windows (PowerShell)**:
```powershell
mkdir build -ErrorAction SilentlyContinue
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

**Linux / macOS**:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

> **OpenMP is required.**
> - **GCC/Clang**: included by default.
> - **MSVC (Windows)**: OpenMP 2.0 runtime is bundled with Visual Studio.
>   *(Note: This project uses compatible OpenMP pragmas for MSVC support)*.

**Debug Build (Slow, for development)**:
```powershell
cd build
cmake --build . --config Debug
```

### Quick sanity check (2 seconds)

```powershell
# Windows
.\build\Release\bench.exe --kernel copy --warmup 2 --iters 5

# Linux / Mac
./build/bench --kernel copy --warmup 2 --iters 5
```

### Control OpenMP thread count
All bandwidth and compute kernels are multi-threaded via OpenMP. Set the thread count with an environment variable:

```bash
# Linux / macOS
export OMP_NUM_THREADS=8
./bench --kernel triad --warmup 50 --iters 200 --prefault

# Windows PowerShell
$env:OMP_NUM_THREADS = "8"
.\bench.exe --kernel triad --warmup 50 --iters 200 --prefault
```

> For DRAM bandwidth tests, set `OMP_NUM_THREADS` to your physical core count to saturate the memory controller.

### End-to-end suite (recommended)

**build → repeated runs → aggregate → generate 2 plots**

Activate the venv and install Python deps once:

```powershell
& .\.venv\Scripts\Activate.ps1
python -m pip install -r scripts\requirements.txt
```

```powershell
python scripts\run_suite.py --config Release --repeats 3 --warmup 10 --iters 50 --prefault
```

Outputs:
- `results/raw/*.json` — per-run raw JSON + captured stdout/stderr
- `results/summary/*_agg.json` + `results/summary/*_agg.csv` — aggregated statistics
- `plots/*.png` — bandwidth vs size + latency vs size

### Run a stable baseline (recommended)

```powershell
# Windows PowerShell
$env:OMP_NUM_THREADS = "8"
.\build\Release\bench.exe --kernel triad --warmup 50 --iters 200 --prefault --out results.json
```

```bash
# Linux / macOS
OMP_NUM_THREADS=8 ./bench --kernel triad --warmup 50 --iters 200 --prefault --out results.json
```

*Output is written to `--out` (default: `results.json`).*

---

## Interpreting Results

### 1. Memory Bandwidth (STREAM Kernels)
Measures how fast the system reads/writes data from memory.
- **`copy`** ($A[i] = B[i]$): Pure memory movement.
- **`scale`** ($A[i] = s \times B[i]$): Memory with light arithmetic.
- **`add`** ($A[i] = B[i] + C[i]$): Higher memory traffic (2 reads, 1 write).
- **`triad`** ($A[i] = B[i] + s \times C[i]$): The standard metric for system memory performance.

### 2. Compute Throughput (GFLOP/s)
Measures raw floating-point calculation power (fitting in cache).
- **`flops`**: Separated multiply and add instructions.
- **`fma`**: Uses **Fused Multiply-Add** (AVX2/AVX-512) for peak throughput.
- **`dot`**: Dot Product (SIMD reduction).
- **`saxpy`**: Vector scale-add (BLAS Level 1).

### 3. Memory Latency
- **`latency`**: Pointer chasing ($node = node \to next$).
    *   **What it does**: Traverses a randomized linked list. The CPU *must* wait for the current node to load before it can find the address of the next node. This defeats hardware prefetchers (which try to guess and load data ahead of time).
    *   **Why we check it**: To measure the pure hardware response time (latency) of each memory tier, unmasked by parallelism or prefetching.
    - **~1-2ns**: L1 Cache
    - **~3-5ns**: L2 Cache
    - **~10-20ns**: L3 Cache (LLC)
    - **~60-100ns+**: Main Memory (DRAM)

---

## Build Modes

| Config | Compiler Flags | Purpose |
| :--- | :--- | :--- |
| **Debug** | `-O0 -g -fopenmp` / `/Od /Zi` | Development, correctness checks |
| **Release** | `-O3 -march=native -fopenmp -ffast-math` + LTO | Real performance measurement |

**Release-only features:**
- `-march=native` selects AVX, AVX2, or AVX-512 instructions for the current CPU.
- `-ffast-math` (`/fp:fast` on MSVC) allows the compiler to reassociate floating-point reductions and fuse multiply-add chains.
- **Link-Time Optimization (LTO)** is enabled via `check_ipo_supported()` and `INTERPROCEDURAL_OPTIMIZATION_RELEASE`, allowing the linker to inline and optimize across translation units.

---

## VS Code CMake Tools (Windows)
If VS Code shows “Unable to configure the project”, use the included `CMakePresets.json`:
- In the Command Palette: **CMake: Select Configure Preset** → `msvc-x64`
- Then: **CMake: Build** (or select **Build Preset**: `release`)

## Kernel Modes

Kernels are selected with `--kernel`:

### Memory-Bandwidth Kernels
`copy`, `scale`, `add`, `triad` (aliases: `stream_copy`, `stream_scale`, `stream_add`, `stream_triad`)

All four run as `#pragma omp parallel for schedule(static)` across all threads with `RESTRICT`-qualified pointers to enable full compiler vectorization.

| Kernel | Operation | Bytes/Element |
| :--- | :--- | :--- |
| Copy  | `A[i] = B[i]` | 2× |
| Scale | `A[i] = s * B[i]` | 2× |
| Add   | `A[i] = B[i] + C[i]` | 3× |
| Triad | `A[i] = B[i] + s * C[i]` | 3× |

### Compute-Bound Kernels
`fma`, `flops`

Both kernels use `#pragma omp parallel for schedule(static)` with a **4-accumulator unrolled inner loop** per element. This exposes 4 independent FMA dependency chains simultaneously, masking the 4–5 cycle hardware FMA execution latency and saturating all available FMA execution ports across all cores.

### BLAS Level-1 Kernels
`dot`, `saxpy`

- `dot` — parallel dot product with `#pragma omp parallel for reduction(+:sum) schedule(static)` for thread-safe partial sum aggregation.
- `saxpy` — parallel `out[i] = a * x[i] + y[i]` with `#pragma omp parallel for schedule(static)`.

### Latency Kernel
`latency`

Randomized pointer-chasing sweep from 4KB to 256MB using `alignas(64)` padded nodes and a `std::mt19937`-shuffled access pattern that defeats all hardware prefetchers. Inherently serial by design — measures true dependent-load latency per cache tier.



---

## Key Strengths & Design Philosophy
*   **Measurement Purity**: Usage of high-resolution timers, `clobber_memory()` barriers, and anti-DCE (Dead Code Elimination) checksums ensures we measure the code, not the compiler's optimizations.
*   **Memory Semantics**: Strict 64-byte alignment and padding prevents cache-line straddling.
*   **True Latency**: The pointer-chasing benchmark uses `std::mt19937` randomization to effectively defeat hardware prefetchers.
*   **Tooling Integration**: Built-in support for `perf` (counters), `valgrind` (cache simulation), and `llvm-mca` (static analysis).

## Status
- **Implemented**: 
    - Full multi-threaded STREAM suite (Copy, Scale, Add, Triad).
    - FMA/FLOPS compute kernels with **4-accumulator unrolling** (fixing the single-accumulator latency bottleneck).
    - Latency pointer-chasing sweep.
    - NUMA-aware parallel initialization ("first-touch").
    - Automated build/run/plot pipeline.
- **In Progress / Planned**: 
    - **Compute Optimization**: Extend `RESTRICT` macros to compute kernels (currently only on STREAM) to further aid auto-vectorization.
    - **Cross-Architecture**: Verification on ARM/non-x86 ISAs.
    - **Advanced**: Blocked GEMM and Loaded-latency benchmarks.

---

## System Requirements
- **OS**: Windows (MSVC), Linux (GCC/Clang), or macOS (Clang).
- **Compiler**: C++17-compatible compiler **with OpenMP runtime** (GCC ≥ 7, Clang ≥ 6, MSVC 2019+).
- **Python**: 3.8+ (for automation and plotting).
- **CMake**: 3.10+.
- **Optional (profiling)**: `perf`, `valgrind`, `llvm-mca` (Linux/WSL only).

---

## Context & Purpose
This project provides a reproducible environment to approach and quantify theoretical hardware limits across the memory hierarchy and compute units. STREAM kernels saturate the memory controller across all threads. FMA kernels target peak GFLOP/s via multi-threaded accumulator-unrolled chains. The latency sweep profiles every cache tier unambiguously with prefetcher-defeating access patterns. All measurements are anchored by correctness validation and noise-resistant statistical reporting.

---

## CLI Arguments
Defaults are defined in `include/config.hpp`.

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--kernel` | Kernel: `copy`, `scale`, `add`, `triad`, `fma`, `flops`, `dot`, `saxpy`, `latency` | `copy` |
| `--size` | Problem size string for compute kernels (e.g., `64MB`, `256MiB`) | `64MB` |
| `--iters` | Measured iterations per sweep point | `100` |
| `--warmup` | Unmeasured warmup iterations (primes caches and DVFS) | `10` |
| `--out` | Output JSON path | `results.json` |
| `--seed` | RNG seed for latency benchmark shuffle | `14` |
| `--prefault` | Pre-touch pages (in parallel) to eliminate first-touch noise | off |
| `--aligned` | Use 64B-aligned allocations via `AlignedBuffer` | off |
| `--help` | Print help | |

> **Thread count**: controlled by the `OMP_NUM_THREADS` environment variable, not a CLI flag.
> **Stable statistics**: prefer `--iters 200` or higher, especially on Windows.

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



<!-- BENCH_STABILITY_START -->
## Stable runs (Windows / low jitter)

Microbenchmarks are sensitive to OS scheduling jitter. On Windows, use CPU pinning + high priority for more stable median/P95.

### Recommended stable run (Windows)
From `build\Release`:

```bat
set OMP_NUM_THREADS=8
C:\Windows\System32\cmd.exe /c start "" /wait /affinity FF /high bench.exe --kernel triad --prefault --warmup 50 --iters 200 --out ..\..\results\triad_aff1.json
```

Notes:
- `/affinity FF` pins to the first 8 logical processors (adjust mask for your core count)
- Set `OMP_NUM_THREADS` to your physical core count before running
- `--warmup N` is not timed (stabilizes CPU/cache/DVFS)
- `--iters N` is timed per sweep point (higher = more stable percentiles)
- `--prefault` pre-touches pages in parallel, preserving NUMA binding

Linux equivalent:

```bash
taskset -c 0-7 OMP_NUM_THREADS=8 ./bench --kernel triad --prefault --warmup 50 --iters 200 --out results/triad_core0-7.json
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
| **DRAM (multi-thread, all cores)** | ~20–80+ |

> Actual multi-thread DRAM numbers depend on memory channel count, JEDEC spec (DDR4/DDR5), and NUMA topology.

---

## Platform Baseline (example machine)
* **CPU**: Intel i7-12700H (14 cores / 20 threads)
* **OS**: Linux (native) / WSL2 / Windows
* **Compiler**: GCC/Clang/MSVC
* **RAM**: 16GB DDR5

---

## Project Architecture

### Root Files
*   **`CMakeLists.txt`**: Build configuration (C++17, `-O3`, `-march=native`, OpenMP).
*   **`CMakePresets.json`**: Convenient build presets for Release/Debug to ensure consistent builds.
*   **`README.md` / `REPORT.md`**: Documentation and technical reports/analysis.

### `include/` (Infrastructure)
The architectural heart of the project containing the shared infrastructure:
*   **`aligned_buffer.hpp`**: RAII wrapper for cache-line aligned allocation (64B) using `posix_memalign` (Linux) or `_aligned_malloc` (Windows). Crucial for preventing "straddling" (data crossing cache lines) and ensuring stable measurements.
*   **`timer.hpp`**: High-resolution timer with measurement hygiene (excluding overheads).
*   **`utils.hpp`**: Helper functions including `do_not_optimize_away()` and `clobber_memory()` to prevent the compiler from "optimizing out" work or reordering instructions across measurement boundaries.
*   **`stream_kernels.hpp`**: Implementation of STREAM kernels (Copy/Scale/Add/Triad) using `RESTRICT` macros to assist vectorization.
*   **`sys_info.hpp`**: Captures system context (CPU model, frequency, logical cores) for reproducible results.
*   **`size_parse.hpp`**: Human-readable size parsing (e.g., "64KB", "1MB").
*   **`results.hpp`**: Data structures for statistics (median, P95, min/max) and JSON serialization.

### `src/` (Implementation)
*   **`main.cpp`**: Entry point. Dispatches kernels based on CLI arguments.
*   **`stream_sweep.cpp`**: **Bandwidth Mode**. Runs STREAM kernels across a sweep of sizes (L1 to DRAM). Handles NUMA-aware initialization and pre-faulting.
*   **`compute_bench.cpp`**: **Compute Mode**. Kernels like FMA, FLOPS, Dot, SAXPY.
    *   *Feature*: The FMA kernel uses **4-way unrolling** to expose distinct dependency chains, ensuring we measure **throughput** (filling the pipeline) rather than latency (waiting for a single accumulator).
*   **`latency_bench.cpp`**: **Latency Mode**. Pointer chasing on a randomized linked list (64B padded nodes) to defeat hardware prefetchers and measure pure load latency.

### `scripts/` (Automation)
*   **`run_suite.py`**: The "Master" script. Runs combinations of sizes, kernels, and iterations to generate a full dataset.
*   **`aggregate_runs.py`**: Aggregates raw JSON runs into summaries (median, per-size stats).
*   **`plot_*.py`**: Generates bandwidth/latency waterfall plots from summary data.
*   **`run_perf_stat.py` / `valgrind` / `llvm-mca`**: Wrappers for deep analysis tools.

### `results/` (Artifacts)
*   **`raw/`**: Individual run JSONs + stdout/stderr logs.
*   **`summary/`**: Aggregated statistics.
*   **`perf/`, `valgrind/`, `llvm-mca/`**: Output from external analysis tools.

## Report
See `REPORT.md` for a minimal, reproducible run recipe and the artifact contract (where outputs live and what they mean).

## Profiling & Opt Experiments (best-effort)
Linux-focused helpers live in `scripts/`:
- `run_perf_stat.py`, `run_valgrind_cachegrind.py`, `run_llvm_mca.py`, `run_opt_experiment.py`

On non-Linux (e.g., Windows), the suite writes explicit `*_BLOCKED.txt` notes under `results/perf/`, `results/valgrind/`, and `results/llvm-mca/` when these tools aren’t available.

---

## Limitations & Warnings
* **WSL2**: `perf` hardware counters may be limited or unavailable.
* **Thermal throttling**: long sweeps can heat the CPU and reduce frequency; use `--warmup` to stabilize beforehand.
* **Memory pressure**: multi-array kernels (Triad, Add) need enough free RAM to avoid swapping/paging.
* **Hardware-specific builds**: Release binaries with `-march=native` are CPU-tuned; rebuild on each target machine.
* **Hyperthreading**: for maximum memory bandwidth, set `OMP_NUM_THREADS` = physical core count, not logical thread count.
* **stream_sweep OOM**: `std::vector` allocations in `run_stream_sweep` are not wrapped in try/catch; extremely large arrays may terminate ungracefully if the system runs out of memory.

---

## Contact & Feedback
If you encounter any problems, have questions, or want to suggest fixes and improvements, feel free to reach out!

- **Author:** Elishama Seri
- **Email:** Elishamaseri@gmail.com
- **GitHub:** [@es989](https://github.com/es989)
