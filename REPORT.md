# Performance Analysis & Methodology Report

## 1. Experimental Setup

*   **OS**: Windows 10/11 (MSVC environment)
*   **Compiler**: Microsoft Visual C++ (MSVC) with OpenMP 2.0 support.
*   **Build Config**: `Release` type with `/O2 /arch:AVX2` (or `/arch:AVX512` if available) and link-time code generation.
*   **Hardware**: (Auto-detected per run, typically x86-64 multi-core).

## 2. Methodology & Design Decisions

This benchmark suite targets **theoretical hardware peaks** by aggressively eliminating software bottlenecks.

### A. Memory Bandwidth (STREAM)
*   **NUMA-Awareness**: We utilize **Parallel First-Touch Initialization**. Memory pages are faulted in by the same threads that will later process them, ensuring they are allocated on the local NUMA node's memory controller.
*   **Vectorization**: All kernels (`copy`, `scale`, `add`, `triad`) use `__restrict` pointers to guarantee no aliasing, allowing the compiler to generate packed SIMD instructions (AVX/AVX2).

### B. True Latency (Pointer Chasing)
*   **Defeating Prefetchers**: Modern hardware prefetchers are excellent at predicting linear or strided patterns. To measure **pure latency**, we construct a linked list with a randomized traversal path using `std::mt19937`.
*   **Padding**: Each node is padded to 64 bytes (cache line size) to prevent adjacent-line prefetching (spatial locality) from interfering with the measurement.

### C. Compute Throughput (FMA)
*   **Instruction Level Parallelism (ILP)**: A standard loop `x = x * a + b` creates a dependency chain where the CPU must wait for the previous result.
*   **4-Way Unrolling**: Our FMA kernel explicitly maintains 4 independent accumulators (`x0`...`x3`). This fills the CPU's execution pipeline, masking the 4-5 cycle latency of FMA instructions and exposing the true throughput potential.

## 3. Results & Interpretation

### Bandwidth Waterfall
As the working set size increases, performance typically drops in distinct "shelves":
1.  **L1 Cache**: Peak bandwidth (often >200 GB/s).
2.  **L2 Cache**: Significant drop (e.g., to ~100 GB/s).
3.  **LLC (L3)**: Bandwidth limited by the ring bus/mesh interconnect.
4.  **DRAM**: The bandwidth settles at the limit of the memory controllers (e.g., 20-50 GB/s depending on channels).

### Latency "Steps"
The `latency_bench` clearly reveals the cache hierarchy:
*   **~1-2 ns**: L1 hit.
*   **~3-5 ns**: L2 hit.
*   **~10-20 ns**: L3 hit.
*   **>60 ns**: Main Memory (DRAM) access.

### Compute Scaling
With the 4-way unrolled kernel, we expect to see performance approaching the theoretical peak GFLOP/s defined by:
$$ Peak = \text{Cores} \times \text{Frequency} \times \text{Ops/Cycle} \times \text{SIMD Width} $$

## 4. Windows-Specific Constraints

While the suite supports Linux tools (`perf`, `valgrind`, `llvm-mca`), these are disabled on Windows builds:
*   **perf**: Requires Linux kernel PMC access.
*   **Valgrind**: Not available on Windows.
*   **llvm-mca**: Requires separate installation and is less integrated on Windows.

On Windows, we rely on high-resolution `std::chrono` timers and `QueryPerformanceCounter` for precise wall-clock measurements.

## 5. Future Work
*   **Cross-Architecture**: Verification on ARM64 (Apple Silicon, AWS Graviton).
*   **Blocked GEMM**: Adding a blocked matrix multiplication kernel to test cache reuse strategies.
