# HPC Performance Benchmark & Optimization Suite

A C++17/OpenMP microbenchmark suite for measuring memory bandwidth, pointer-chase latency, and floating-point compute throughput across the CPU cache/DRAM hierarchy. Built with CMake, it produces structured JSON output and includes Python scripts for aggregation and plotting.

The suite is designed to help separate hardware behavior from measurement artifacts. It uses NUMA-aware first-touch initialization, optional page prefaulting, compiler barriers around timed regions, anti-DCE checksums, 64-byte aligned allocations, and randomized pointer-chasing to reduce common benchmarking pitfalls. Multi-size sweeps make hierarchy transitions visible instead of collapsing them into a single average.

---

## Table of Contents

1. [What the suite measures](#what-the-suite-measures)
2. [Methodology](#methodology)
3. [Example observations](#example-observations)
4. [Requirements](#requirements)
5. [Build instructions](#build-instructions)
6. [Quick start](#quick-start)
7. [CLI reference](#cli-reference)
8. [Repository structure](#repository-structure)
9. [Plotting and analysis](#plotting-and-analysis)
10. [Caveats and limitations](#caveats-and-limitations)
11. [Additional documentation](#additional-documentation)

---

## What the suite measures

### Memory bandwidth

Four STREAM-style kernels sweep working-set sizes from 32 KB to 512 MB, crossing L1, L2, LLC, and DRAM:

| Kernel | Operation | Arrays read/written |
|--------|-----------|---------------------|
| Copy | `A[i] = B[i]` | 1 read + 1 write |
| Scale | `A[i] = s * B[i]` | 1 read + 1 write |
| Add | `A[i] = B[i] + C[i]` | 2 reads + 1 write |
| Triad | `A[i] = B[i] + s * C[i]` | 2 reads + 1 write |

All STREAM kernels are parallelized with OpenMP (`schedule(static)`) and use `__restrict`-qualified pointers to reduce aliasing barriers.

### Memory latency

A randomized pointer-chase kernel (`p = *p`) with cache-line-padded nodes (64 bytes each). The linked list is shuffled via `std::mt19937` to defeat hardware prefetchers. Each load depends on the result of the previous load, so the CPU cannot overlap or prefetch accesses. This is intended to approximate dependent-load round-trip latency across the memory hierarchy.

### Compute throughput

Four arithmetic kernels measure floating-point throughput and expose code-generation effects:

| Kernel | Operation | Notes |
|--------|-----------|-------|
| FLOPS | `x = x * a + b` | Separate MUL+ADD -- may auto-vectorize |
| FMA | `x = std::fma(x, a, b)` | Fused multiply-add -- compiler-sensitive |
| DOT | `sum += x[i] * y[i]` | Memory-bound reduction (OpenMP) |
| SAXPY | `out[i] = a * x[i] + y[i]` | BLAS-1 style (OpenMP) |

The FLOPS and FMA kernels use 4 independent accumulators per element to expose instruction-level parallelism. When `--aligned` is used, these kernels run a serial inner loop on raw pointers; without `--aligned`, they use OpenMP-parallelized `std::vector`-based paths.

---

## Methodology

Measurements are intended to capture steady-state hardware behavior rather than transient artifacts. Key design choices:

- **Warmup before timing.** Configurable warmup iterations reduce variation from CPU frequency ramp-up, cold caches, and lazy initialization.
- **First-touch parallel initialization.** Arrays are initialized under the same OpenMP layout used during measurement, helping align page ownership with worker threads.
- **Optional prefaulting** (`--prefault`). Touches pages before the timed region to remove page-fault latency from measurements.
- **Anti-DCE protection.** Checksums and `do_not_optimize_away()` sinks ensure the compiler preserves the computation being measured.
- **Compiler barriers.** `clobber_memory()` barriers around timing boundaries reduce instruction motion across the measured interval. On GCC/Clang this is an `asm volatile` memory clobber; on MSVC it uses `std::atomic_signal_fence`.
- **Median-based reporting.** The suite reports median, P95, min, max, and standard deviation. Median is more robust than mean under OS noise.
- **Working-set sweep.** Sizes range from tens of kilobytes to hundreds of megabytes, producing bandwidth waterfalls and latency staircases that reveal hierarchy structure.

---

## Example observations

The following were observed on one specific system and should not be generalized. They are included as an example of the output shape and scale the suite can produce under one specific configuration.

**System:** Intel Core i7-1165G7 (Tiger Lake), 15 GiB LPDDR4x, Windows 11, MSVC 1929
**Config:** `--warmup 50 --iters 200 --prefault --aligned`, `OMP_NUM_THREADS=4`

### Memory hierarchy summary

| Tier | Observed BW (GB/s) | Observed latency (ns/access) |
|------|---------------------|------------------------------|
| L1/L2 (cache-resident) | 65–139 | ~1.5–3.9 |
| LLC | 65–133 | ~5–14 |
| DRAM (sustained) | ~19–21 | ~84–94 |

The bandwidth waterfall shows a clear cliff near the 12 MB LLC boundary (between 4 MB and 8 MB per array), with a roughly 5× drop from cache-resident throughput (~97 GB/s) to sustained DRAM (~19–21 GB/s). Cache-resident values vary 30–40% across runs due to Turbo Boost and thermal dynamics; DRAM-tier values are more stable, especially under tighter execution control (see REPORT.md, Appendix D).

![STREAM Triad bandwidth waterfall](assets/bandwidth_vs_size_triad.svg)

### Compute throughput (64 MB, DRAM-resident)

| Kernel | GFLOPS | Notes |
|--------|--------|-------|
| FLOPS | ~34–36 | Auto-vectorized by MSVC (serial, `--aligned` path) |
| FMA | ~1.0–1.2 | Scalar serialization -- `std::fma` not vectorized by MSVC 1929 |
| DOT | ~4.03 | Memory-bound, 4-thread OpenMP |
| SAXPY | ~1.71 | Memory-bound, 4-thread OpenMP |

The ~29–33× gap between FLOPS and FMA observed across measurement campaigns is best explained by code generation rather than by a hardware FMA limitation. The FLOPS loop was successfully vectorized; the `std::fma` version was not. The exact ratio is configuration-sensitive (thermal state, core assignment, boost clock) but the qualitative conclusion is consistent. This is exactly the kind of distinction the suite is designed to expose.

### Memory latency

The pointer-chase benchmark shows a latency staircase across the hierarchy. L1 latency ranges from ~1.5 ns to ~1.7 ns across campaigns; DRAM latency stabilizes around ~89–94 ns at large working sets (128–256 MB). Transition regions (especially 4–16 MB) tend to show higher variance under Windows scheduling. Under tighter execution control (clean reruns with corrected affinity and sequential execution), latency measurements showed excellent run-to-run stability (<0.4% at cache-resident sizes).

![Memory latency staircase](assets/latency_vs_size_ptr_chase.png)

> Full sweep tables, detailed per-kernel results, and extended methodology discussion are in [REPORT.md](REPORT.md).

---

## Requirements

### Build

| Dependency | Version | Notes |
|------------|---------|-------|
| C++ compiler | C++17 | MSVC 2019+, GCC 9+, Clang 10+ |
| CMake | 3.10+ | Project generation |
| OpenMP | 2.0+ | Parallel STREAM/DOT/SAXPY kernels |

### Analysis (optional)

| Dependency | Purpose |
|------------|---------|
| Python 3.9+ | Automation, aggregation, plotting |
| pandas >= 2.0 | Aggregation and CSV export |
| matplotlib >= 3.7 | Plotting |

```bash
pip install -r scripts/requirements.txt
```

---

## Build instructions

Use **Release mode** for all measurements. Debug builds disable vectorization and optimization, producing numbers that do not reflect hardware capability.

```bash
# Linux/macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```powershell
# Windows (MSVC)
cmake -S . -B build
cmake --build build --config Release
```

On MSVC this enables `/O2 /openmp /fp:fast`. On GCC/Clang this enables `-O3 -march=native -fopenmp -ffast-math`.

---

## Quick start

```bash
# Linux/macOS
export OMP_NUM_THREADS=4
./build/bench --kernel copy --warmup 2 --iters 5
./build/bench --kernel triad --warmup 50 --iters 200 --prefault --aligned --out triad.json
./build/bench --kernel latency --warmup 50 --iters 200 --prefault --out latency.json
./build/bench --kernel flops --size 64MB --warmup 50 --iters 200 --aligned --out flops.json
./build/bench --kernel fma   --size 64MB --warmup 50 --iters 200 --aligned --out fma.json
```

```powershell
# Windows PowerShell
$env:OMP_NUM_THREADS = "4"
.\build\Release\bench.exe --kernel copy --warmup 2 --iters 5
.\build\Release\bench.exe --kernel triad --warmup 50 --iters 200 --prefault --aligned --out triad.json
.\build\Release\bench.exe --kernel latency --warmup 50 --iters 200 --prefault --out latency.json
.\build\Release\bench.exe --kernel flops --size 64MB --warmup 50 --iters 200 --aligned --out flops.json
.\build\Release\bench.exe --kernel fma   --size 64MB --warmup 50 --iters 200 --aligned --out fma.json
```

```bash
# End-to-end suite (build + run + aggregate + plot)
python scripts/run_suite.py --config Release --repeats 3 --warmup 50 --iters 200 --prefault --aligned
```

---

## CLI reference

```
bench [options]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--kernel <name>` | `stream` | Kernel to run (see table below) |
| `--size <str>` | `64MB` | Working-set size per array. Accepts `KB`, `MB`, `GB`, `KiB`, `MiB`, `GiB` |
| `--iters <n>` | `100` | Timed iterations |
| `--warmup <n>` | `10` | Warmup iterations (not timed) |
| `--out <file>` | `results.json` | JSON output path |
| `--prefault` | off | Touch pages before timed region |
| `--aligned` | off | 64-byte aligned allocations |
| `--seed <n>` | `14` | RNG seed for latency pointer shuffle |
| `--threads <n>` | `1` | Stored in output metadata (see note below) |
| `--help` | | Show usage |

> **Note:** `--threads` is parsed and recorded in the JSON output, but it does **not** call `omp_set_num_threads()`. OpenMP thread count is controlled exclusively through the `OMP_NUM_THREADS` environment variable. Set that variable before running the benchmark.

### Kernel names and aliases

| `--kernel` | Alias(es) | Measures | Threading |
|------------|-----------|----------|-----------|
| `copy` | `stream_copy` | Read+write bandwidth | OpenMP |
| `scale` | `stream_scale` | Scale+write bandwidth | OpenMP |
| `add` | `stream_add` | 3-array bandwidth | OpenMP |
| `triad` | `stream_triad`, `stream` | STREAM triad (standard HPC metric) | OpenMP |
| `flops` | -- | Arithmetic throughput | OpenMP or serial (see below) |
| `fma` | -- | FMA throughput / codegen test | OpenMP or serial (see below) |
| `dot` | -- | Read-dominated reduction | OpenMP |
| `saxpy` | -- | BLAS-1 style arithmetic | OpenMP |
| `latency` | -- | Dependent-load memory latency | Serial |

**`--aligned` behavior for FLOPS/FMA:** When `--aligned` is enabled, the FLOPS and FMA kernels use a serial inner loop on aligned raw pointers. Without `--aligned`, they use OpenMP-parallelized `std::vector`-based paths. This affects both threading and potentially code generation.

### Size parsing

The `--size` flag accepts human-readable size strings:

- Decimal: `64MB` = 64,000,000 bytes
- Binary: `64MiB` = 67,108,864 bytes
- Raw bytes: `1048576`

STREAM kernels ignore `--size` and instead run a built-in sweep (32 KB to 512 MB). Compute and latency kernels use `--size` for the working-set size.

> For a complete CLI reference with flag interactions and per-flag caveats, see [REPORT.md](REPORT.md).

---

## Repository structure

```
.
|-- CMakeLists.txt              # Build system (C++17, OpenMP, Release/Debug)
|-- CMakePresets.json            # VS 2019 preset
|-- README.md                    # This file
|-- REPORT.md                    # Full benchmark report and CLI reference
|-- include/
|   |-- aligned_buffer.hpp       # Cross-platform 64-byte aligned allocation
|   |-- config.hpp               # CLI parsing and Config struct
|   |-- results.hpp              # JSON output with platform metadata
|   |-- stream_kernels.hpp       # STREAM Copy/Scale/Add/Triad (OpenMP)
|   |-- size_parse.hpp           # Human-readable size string parser
|   |-- sys_info.hpp             # System info collection (CPU, RAM, caches)
|   |-- timer.hpp                # steady_clock nanosecond timer
|   |-- utils.hpp                # Anti-DCE, clobber, statistics, validation
|   +-- nlohmann/json.hpp        # JSON library (vendored)
|-- src/
|   |-- main.cpp                 # Entry point and kernel dispatch
|   |-- stream_sweep.cpp         # STREAM sweep runner (32 KB - 512 MB)
|   |-- compute_bench.cpp        # FLOPS/FMA/DOT/SAXPY runner
|   |-- latency_bench.cpp        # Pointer-chase latency runner
|   +-- sys_info.cpp             # Runtime system info (CPU model, caches, RAM)
|-- scripts/
|   |-- run_suite.py             # End-to-end: build, run, aggregate, plot
|   |-- run_all.ps1              # PowerShell batch runner for all kernels
|   |-- run_bench_runs.ps1       # Repeated runs with affinity/priority pinning
|   |-- aggregate_runs.py        # Median-of-medians cross-run aggregation
|   |-- plot_bandwidth_vs_size.py  # Bandwidth waterfall plots
|   |-- plot_latency_vs_size.py    # Latency staircase plots
|   |-- plot_results.py          # Simple bandwidth plot
|   |-- check_schema.py          # JSON schema validator
|   |-- run_perf_stat.py         # Linux perf stat wrapper
|   |-- run_llvm_mca.py          # LLVM-MCA static analysis (Linux)
|   |-- run_valgrind_cachegrind.py # Cachegrind wrapper (Linux)
|   |-- run_opt_experiment.py    # Compiler flag comparison experiment
|   +-- requirements.txt         # Python dependencies
|-- results/                     # Benchmark outputs (JSON, CSV)
|-- plots/                       # Generated plots (PNG, SVG, CSV)
+-- assets/                      # Figures for documentation
```

---

## Plotting and analysis

```bash
# Bandwidth waterfall (with automatic knee detection)
python scripts/plot_bandwidth_vs_size.py results/summary/stream_triad_agg.json

# Research mode with CSV export
python scripts/plot_bandwidth_vs_size.py results/summary/stream_triad_agg.json --mode research --export-csv

# Latency staircase
python scripts/plot_latency_vs_size.py results/summary/latency_ptr_chase_agg.json

# Aggregate multiple runs (median-of-medians)
python scripts/aggregate_runs.py results/raw/triad_run_*.json \
  --out-json results/summary/triad_agg.json \
  --out-csv results/summary/triad_agg.csv
```

### Output format

The suite emits structured JSON with:
- **`config`**: all CLI flags used for the run
- **`metadata.platform`**: CPU model, logical cores, cache sizes, RAM, compiler, OS
- **`stats.sweep[]`**: per-size data points with `bytes`, `median_ns`, `p95_ns`, `min_ns`, `max_ns`, `stddev_ns`, `bandwidth_gb_s`, `checksum`
- **`stats.performance`**: aggregate metrics (`gflops`, `bandwidth_gb_s`)

Latency sweep points additionally include `ns_per_access`.

---

## Caveats and limitations

- **Results are platform-specific.** All numbers shown were observed on one system under one configuration. Different hardware, compilers, OS, and BIOS settings will produce different results.
- **Compiler behavior matters.** The FMA/FLOPS gap in the example observations is a code-generation effect, not a hardware limitation. Always inspect generated assembly when interpreting surprising compute results.
- **Execution discipline affects reproducibility.** Clean reruns under tighter control (corrected physical-core affinity masks, strictly sequential execution, inter-run cooling, reduced background activity) noticeably improved DRAM-tier bandwidth stability (spread dropped from ~8.6% to ~1.6%). Cache-resident variability (30–40%) persists regardless of execution control, as it is driven by Turbo Boost and thermal dynamics. See REPORT.md Appendix D for details.
- **Example values are representative ranges, not constants.** Numbers in the example observations section span the range observed across multiple measurement campaigns conducted under different thermal states. Treat them as approximate.
- **`--threads` does not set OMP threads.** Use the `OMP_NUM_THREADS` environment variable instead.
- **`--aligned` changes threading for FLOPS/FMA.** The aligned path uses a serial loop. The non-aligned path uses OpenMP parallelism.
- **Windows scheduler noise.** The LLC-to-DRAM transition region can show elevated variance, particularly in latency measurements at intermediate working-set sizes.
- **fast-math is enabled in Release.** This may alter IEEE-strict floating-point behavior (MSVC `/fp:fast`, GCC/Clang `-ffast-math`).
- **STREAM kernels are not real applications.** They isolate bandwidth behavior cleanly but are intentionally simple.
- **Cache boundary inference is approximate.** Performance cliffs suggest cache capacity boundaries but do not prove exact geometry.
- **Profiling hooks are Linux-only.** The `perf`, `valgrind`, and `llvm-mca` scripts require Linux toolchains and are blocked on other platforms.
- **Practical reproducibility note.** Before collecting measurements, verify in Task Manager or PowerShell that no previous benchmark, build, or runner process is still active. Overlapping local processes can introduce avoidable timing noise.

---

## Additional documentation

- [REPORT.md](REPORT.md) -- Full benchmark report with detailed sweep tables, methodology, CLI reference, and analysis
- [scripts/requirements.txt](scripts/requirements.txt) -- Python dependencies for plotting and analysis
