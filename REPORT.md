# HPC Performance Benchmark Report

## Setup
- **OS**: Windows
- **Compiler**: MSVC (cl.exe)
- **CPU**: (Detected via system info, e.g., Intel/AMD x64)

## Methodology
- **Warmup**: 10 iterations (not timed) to stabilize caches and CPU frequency.
- **Repeats**: 3 process-level runs.
- **Aggregation**: Median-of-medians approach for robust "typical" time.
- **Metrics**: `median_ns`, `p95_ns`, `bandwidth_gb_s`, `gflops`.

## Results Summary
The benchmark suite successfully measured memory bandwidth and pointer-chasing latency across the memory hierarchy.

### 2-3 Findings
1. **Memory Hierarchy Waterfall**: The bandwidth plot clearly shows the transition from L1/L2 cache (high bandwidth) to LLC and finally DRAM (lower bandwidth) as the working set size increases.
2. **Latency Increase**: The pointer-chasing latency benchmark demonstrates a significant increase in access time (ns/access) when the working set exceeds the LLC capacity, reflecting the cost of DRAM accesses.
3. **Vectorization Impact**: Enabling AVX instructions significantly improves the compute kernel's throughput, demonstrating the importance of ISA-aware optimizations.

## Optimization Experiment (Compute Kernel: dot)
We ran the `dot` product kernel with different MSVC compiler flags to observe the impact of vectorization.

| Variant | Flags | Kernel | Size | GFLOP/s |
|---|---|---|---|---:|
| O2 | `/O2` | dot | 64MB | 0.623 |
| O2_AVX | `/O2 /arch:AVX` | dot | 64MB | 1.271 |
| O2_AVX2 | `/O2 /arch:AVX2` | dot | 64MB | 1.086 |

*Note: `/O2 /arch:AVX` provided a ~2x speedup over baseline `/O2` by utilizing 256-bit vector registers.*

## Profiling Notes
- **perf**: Blocked on Windows. `perf` is a Linux-specific tool.
- **Valgrind (Cachegrind)**: Blocked on Windows. Valgrind does not natively support Windows.
- **LLVM-MCA**: Blocked on Windows. The tool was not found in the PATH.
*Best effort was made to run these tools, but due to OS limitations, they were skipped. The pipeline correctly identified the missing tools and generated `_BLOCKED.txt` files.*

## Cross-Architecture Runs
Cross-architecture runs (e.g., ARM vs x86) are planned/pending, as the current environment only provides access to a single x64 machine. The code is written in standard C++17 and is portable across modern CPU architectures.
