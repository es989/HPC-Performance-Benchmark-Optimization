# HPC-Performance-Benchmark-Optimization
 **A professional microbenchmark suite for measuring memory bandwidth, latency, and compute efficiency.**

## 1. Context & Purpose
This project provides a reproducible environment to quantify system performance across different memory hierarchies (L1/L2/LLC/DRAM) and compute kernels. It identifies bottlenecks using hardware counters (via `perf`) and demonstrates the impact of targeted code optimizations.

## 2. Methodology (The Contract)
To ensure measurements represent hardware reality rather than "system noise", we lock these upfront:
* **Warmup**: 5 iterations before any measurement to stabilize CPU frequency and prime caches.
* **Repetitions**: 30–50 runs per data point to ensure statistical significance.
* **Metrics**: Reporting **Median** and **P95** (Tail Latency) to filter out OS interrupts.
* **CPU Affinity**: All benchmarks are pinned to specific cores using `taskset` for stability.
* **Build**: Compiled with `-O3 -march=native` to leverage modern ISA extensions like AVX/AVX-512.

## 4. Platform Specifications (Baseline)
CPU: Intel i7-12700H
Cores/Threads: [14 Cores / 20 Threads]
OS: Linux (Native) / WSL2
Compiler: GCC/Clang
RAM: [16GB DDR5]

## 5.Project Structure
src/: C++ kernel implementations (STREAM, Latency, GEMM).
include/: Common utilities (High-res timers, CPU pinning).
scripts/: Python runners and visualization tools.
results/: Raw machine-readable results (JSON/CSV).

## 6.Limitations & Warnings
WSL2: Performance counters (perf stat) may be partially limited.
Thermal Throttling: Results may vary if the CPU exceeds thermal limits.

## 3. Quick Start (How it looks in action)
```bash
# Build the project (Planned)
mkdir build && cd build
cmake .. && make

# Run a specific kernel (Example: STREAM-like memory bandwidth)
./bench --kernel stream --size 64M --threads 1 --iters 30

