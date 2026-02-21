# AI Coding Agent Instructions: HPC Performance Benchmark

## Project Overview
This is a **high-performance computing microbenchmark suite** designed to measure memory bandwidth and latency across the memory hierarchy (L1/L2/LLC/DRAM) using STREAM-like kernels. The "big picture" is: generate reproducible hardware performance data, identify bottlenecks via kernel timing and statistical analysis, and output machine-readable JSON results for visualization.

## Architecture & Data Flow

### Core Pipeline
1. **Configuration Layer** ([config.hpp](include/config.hpp)): CLI parsing into a single `Config` struct with defaults (kernel type, dataset size, iteration count, warmup count, RNG seed)
2. **Kernel Dispatching** ([main.cpp](src/main.cpp)): Map kernel names to `StreamOp` enum, then pass to runner
3. **Sweep Execution** ([stream_sweep.cpp](src/stream_sweep.cpp)): For each size in a predefined range (32KB → 512MB), run:
   - **Warmup phase** (5–10 iterations, not timed) to stabilize CPU frequency and prime caches
   - **Measurement phase** (30–50 iterations, each timed individually) collecting per-iteration samples in nanoseconds
4. **Results Aggregation** ([results.hpp](include/results.hpp)): Calculate median/P95 statistics from samples, compute effective bandwidth (bytes_touched / median_time), serialize to JSON with metadata (OS, compiler, timestamp, config for reproducibility)

### Key Insight: Why This Structure
- **Separation of concerns**: Config handles CLI parsing, kernels are agnostic to timing, runners manage the measurement loop, results handle output formatting
- **Deterministic reproducibility**: Config includes a seed; warmup ensures thermal/frequency stability; results capture platform metadata for cross-system comparison
- **One kernel dispatch per benchmark run**: `main.cpp` instantiates ONE `StreamOp` and hands it to `run_stream_sweep()` which sweeps across all sizes

## Critical Patterns & Conventions

### 1. The `Config` Struct Pattern
All CLI arguments are collected into a single `Config` struct (not scattered globals). This struct is:
- **Immutable after parsing** (passed by const ref throughout)
- **Printed for debugging** via `Config::print()`
- **Self-documenting**: includes default values and docstrings for each field

**Implication for new features**: If adding a new CLI flag (e.g., `--cpu-affinity`), add it to `Config`, update `parse_args()`, and pass it through the function chain.

### 2. Kernel Registration via Enum + Descriptor
New kernels are registered using the `StreamOp` enum and `KernelDesc` struct:

```cpp
enum class StreamOp { Copy, Scale, Add, Triad };
struct KernelDesc {
    StreamOp op;
    StreamKernelFn fn;          // function pointer to the actual kernel
    double bytes_mult();        // how many bytes touched (2x or 3x per element)
};
```

**To add a new kernel**:
1. Add variant to `StreamOp` enum
2. Implement kernel function in [stream_kernels.hpp](include/stream_kernels.hpp)
3. Add case in `make_stream_desc()` and `stream_op_name()`
4. Add case in `main.cpp` to map CLI name to enum variant

### 3. Timing & Dead Code Elimination (DCE)
Kernels and measurement use helper utilities from [utils.hpp](include/utils.hpp):
- `clobber_memory()`: volatile asm to prevent compiler from hoisting/sinking memory ops outside timing boundaries
- `do_not_optimize_away()`: volatile asm to force compiler to keep a result (prevents dead code elimination)

**Critical rule**: Every kernel iteration must be wrapped with `clobber_memory()` before/after timing to isolate the kernel from surrounding code.

### 4. JSON Output Contract
Results are serialized to JSON with **required top-level keys**:
- `metadata.timestamp`, `metadata.platform` (OS, compiler, C++ standard)
- `config` (exact CLI arguments for reproducibility)
- `stats.performance` (aggregate timings, bandwidth, GFLOPS if applicable)
- `sweep_points` (array of Point objects with bytes, median_ns, p95_ns, bandwidth_gb_s, checksum)

**Implication**: Any new metrics (e.g., FLOP counts) must be added to `BenchmarkResult::Point` and serialized in the JSON writer.

## Build & Workflow

### CMake Build
- **Standard**: C++17 with `-O3 -march=native` (Release) and `-O0 -g` (Debug)
- **Export compile commands**: `CMAKE_EXPORT_COMPILE_COMMANDS ON` generates `build/compile_commands.json` for VS Code IntelliSense
- **Executable**: Single target `bench` linking `src/main.cpp` and `src/stream_sweep.cpp`

**When adding new .cpp files**, update the `add_executable()` target in [CMakeLists.txt](CMakeLists.txt) to avoid link errors.

### Typical Dev Cycle
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./bench --kernel stream_copy --size 64MB --threads 1 --iters 30
```

## Cross-Component Communication

- **Config → Kernel Runner**: `Config` is passed const-ref; immutable after parse
- **Runner → Results**: `BenchmarkResult` is populated with `sweep_points`; timing samples are reduced to median/P95 at aggregation time
- **Results → JSON**: `BenchmarkResult::save(conf)` uses nlohmann::json to serialize

**No global state**: All communication flows through function parameters.

## External Dependencies & Notes
- **nlohmann/json** (included as header-only in `include/nlohmann/json.hpp`): Used for JSON serialization; no external link-time dependency
- **stdlib only**: No pthread, Boost, or other external libraries; platform detection via compiler macros

## Common Pitfalls to Avoid
1. **Forgetting to add .cpp files to CMake**: Link errors will result
2. **Removing `clobber_memory()` calls**: Compiler will optimize away kernel iterations, yielding false results
3. **Not accounting for bytes_multiplier()**: Copy/Scale touch 2× bytes per element; Add/Triad touch 3×
4. **Modifying Config after parse**: Config is const throughout; create new Config instead

