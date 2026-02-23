# HPC Performance Benchmark Report (Minimal, Reproducible)

## Scope
This repository is a C++17 microbenchmark suite intended to produce **reproducible, machine-readable** measurements for:
- Memory bandwidth vs working-set size (STREAM-like sweep)
- Dependent-load (pointer-chasing) latency vs working-set size
- Simple compute kernels with correctness checks (dot / saxpy)

All measurements are saved as JSON (and aggregated CSV) for plotting and later analysis.

## How To Reproduce (Windows)
From the repo root:

```powershell
.\.venv\Scripts\python.exe scripts\run_suite.py --config Release --repeats 3 --warmup 10 --iters 50 --prefault
```

Notes:
- `--repeats` runs the full benchmark process multiple times.
- Aggregation uses a **median-of-medians** approach for robustness.
- `--prefault` reduces noise from first-touch/page faults.
- Add `--aligned` to use 64B-aligned allocations where supported.

## Outputs
After `run_suite.py` completes:
- `results/raw/`: per-run JSON outputs (plus captured stdout/stderr next to each JSON)
- `results/summary/`: aggregated `*_agg.json` and `*_agg.csv`
- `plots/`:
  - `bandwidth_vs_size_stream_triad.png`
  - `latency_vs_size_ptr_chase.png`
- `results/system/system.json`: environment/tool snapshot

## Methodology (What The Numbers Mean)
Each sweep point records per-iteration timing samples (nanoseconds) and reports:
- `median_ns` (robust “typical” time)
- `p95_ns` (tail/jitter indicator)
- `min_ns`, `max_ns`, `stddev_ns`

For bandwidth kernels, effective bandwidth is derived from bytes-touched divided by the median time.
For the latency benchmark, the suite reports `ns_per_access` from a dependent-load pointer chase.

Warmup iterations are run but not timed to stabilize caches and CPU frequency.

## Benchmarks Included
### Memory bandwidth (STREAM sweep)
Kernels: `copy`, `scale`, `add`, `triad`.
The suite’s default plot uses `triad` because it stresses both loads and stores.

### Dependent-load latency (pointer chase)
Kernel: `latency`.
Implementation builds a randomized single-cycle permutation and times repeated `next` pointer dereferences.

### Compute kernels + correctness
Kernels: `dot`, `saxpy`.
These use deterministic inputs and validate expected results to catch silent miscompiles or unexpected numeric issues.

## Profiling & Optimization Experiments (Best-effort)
This repo includes wrappers under `scripts/`:
- `run_perf_stat.py`
- `run_valgrind_cachegrind.py`
- `run_llvm_mca.py`
- `run_opt_experiment.py`

On Windows, these tools are typically unavailable; the suite writes clear `*_BLOCKED.txt` notes under `results/perf/`, `results/valgrind/`, and `results/llvm-mca/` when blocked.

For Linux/WSL usage, run the scripts directly on a system where the tools are installed and available on `PATH`.
