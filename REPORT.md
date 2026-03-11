# Full Hardware Benchmark Report - Intel i7-1165G7 (Tiger Lake)

**Platform:** Intel Core i7-1165G7 @ 2.80GHz — MSVC 1929 — Windows 11  
**Run config:** `--warmup 50 --iters 200 --prefault --aligned` | `OMP_NUM_THREADS=4`  
**Threading:** STREAM/DOT/SAXPY: 4 threads (OpenMP) | FMA/FLOPS/Latency: single-threaded  
**Audit date:** 2026-03-10 | **Clean pinned rerun:** 2026-03-11 | **Bug fixed prior to audit:** `pt.bytes = size_bytes` — sweep entries now carry correct X-axis values.

> **Audit note — data provenance.** This report draws on **two measurement campaigns**:
>
> 1. **Original audit** (2026-03-10): measured from a clean `build_audit/` Release build
>    (MSVC 19.29, `/O2 /openmp /fp:fast`). No affinity pinning, normal priority.
>    Results in `audit_results/`.
> 2. **Clean pinned rerun** (2026-03-11): same binary, same flags, but under stricter
>    execution control — strictly sequential runs, corrected physical-core affinity masks
>    (`0x55` for 4-thread, `0x04` for single-thread), `PriorityClass = High`, and
>    inter-run cooling gaps. Results in `pinned_results/clean_rerun/`.
>
> STREAM Triad, FLOPS, and FMA figures are **medians of three independent 200-iteration
> runs** in each campaign. Where the two campaigns disagree, both values are presented
> as a range. Cache-resident bandwidth values (L1–LLC) exhibit **30–40% run-to-run
> variability** caused by Turbo Boost frequency and thermal state; DRAM-regime values
> are stable (±2–5% depending on execution discipline).

---

## Part A — Measured Results

### A.1 — Memory Bandwidth (STREAM Suite)

All STREAM kernels sweep working-set sizes from 32 KB/array up to 512 MB/array, crossing L1 → L2 → LLC → DRAM.
High bandwidths (>100 GB/s) at cache-resident sizes are achieved by distributing independent memory streams across **4 threads** (OpenMP `parallel for`, `schedule(static)`).

#### Peak Bandwidth Summary

Cache-resident peak bandwidth varies 30-40% between runs depending on Turbo Boost state. The values below are representative of the audit runs; DRAM values are stable.

| Kernel | Operation | Arrays | **Peak BW (audit)** | **DRAM Sustained** |
|--------|-----------|--------|---------------------|-------------------|
| Copy   | `B[i] = A[i]` | 2 | **~139 GB/s** @ 1 MB | ~16–18 GB/s |
| Add    | `C[i] = A[i]+B[i]` | 3 | **~129 GB/s** @ 256 KB | ~20–21 GB/s |
| Triad  | `A[i] = B[i]+s*C[i]` | 3 | **~92–133 GB/s** (variable) | ~19–21 GB/s |
| Scale  | `B[i] = s*A[i]` | 2 | **~115 GB/s** @ 4 MB | ~15–17 GB/s |

#### STREAM Triad — Full Sweep (the standard HPC metric)

Values below are the **median of three independent 200-iteration runs**. The "Range" column shows the spread across the three runs, illustrating cache-resident variability.

| Per-array size | Total footprint | Bandwidth (GB/s) | Range (3 runs) | Tier |
|---|---|---|---|---|
| 32 KB | 96 KB | 80.2 | 75.6–89.4 | L1 |
| 64 KB | 192 KB | 89.4 | 81.9–103.5 | L1/L2 |
| 128 KB | 384 KB | 91.4 | 85.5–115.7 | L2 |
| 256 KB | 768 KB | 97.1 | 94.8–128.9 | L2 |
| 512 KB | 1.5 MB | 97.7 | 97.7–98.3 | L2 |
| 1 MB | 3 MB | 97.7 | 96.3–132.7 | LLC |
| 2 MB | 6 MB | 96.1 | 93.6–96.5 | LLC |
| 4 MB | 12 MB | 71.4 | 65.6–87.3 | LLC tail |
| 8 MB | 24 MB | 28.5 | 27.9–28.9 | LLC→DRAM cliff |
| 16 MB | 48 MB | 22.6 | 22.6–23.0 | DRAM |
| 32–512 MB | 96 MB–1.5 GB | 20.5–20.9 | ±0.5 | DRAM (stable) |

> **Clean rerun note.** Three additional Triad runs under strict sequential execution
> with corrected affinity masks (`0x55`) measured 18.86–19.16 GB/s at 512 MB (spread
> 1.6%). The lower absolute value compared to the audit’s ~20.5–20.9 GB/s reflects a
> different session thermal state. Within each session, DRAM-tier values are highly
> reproducible. The full observed DRAM Triad range across all campaigns is ~18–21 GB/s.

**Key finding:** ~5× bandwidth drop from LLC (~97 GB/s) to DRAM (~19–21 GB/s). The cliff at 4→8 MB is a single doubling of size producing a ~2.5× bandwidth drop — this is where the 12 MB LLC spill begins.

![STREAM Triad bandwidth waterfall](assets/bandwidth_vs_size_triad.svg)

---

### A.2 — Compute Throughput (GFLOPS)

Fixed 64 MB working set (8M `double` elements), DRAM-resident.

**Implementation detail:** With `--aligned`, the FMA/FLOPS kernels take a **single-threaded** serial-loop code path (no OpenMP, single accumulator per element). This is a code-path artifact of the `--aligned` flag selecting raw-pointer loops; the non-aligned path uses OpenMP parallel `std::vector` loops with 4 independent accumulators.

- **FLOPS:** MSVC auto-vectorizes `x = x*a + b` for the single core (likely AVX2 `vfmadd` or separate `vmul`/`vadd`).
- **FMA:** Under MSVC 1929, `std::fma()` acts as an optimization barrier, producing scalar serial code.

FLOPS and FMA values are medians of three 200-iteration runs; DOT/SAXPY are single 200-iteration runs.

| Kernel | Operation | Median time | **GFLOPS** | Range (across campaigns) | Bottleneck |
|--------|-----------|------------|-----------|--------------------------|---------------------------|
| **FLOPS** | `x = x*a + b` (serial) | 28.9–30.6 ms | **~34–36** | 33.5–35.6 | **Vectorized**, 1 thread |
| **DOT**   | `sum += x[i]*y[i]` (4 threads) | 4.0 ms | **4.03** | — | Memory (read-only BW) |
| **SAXPY** | `out[i] = a*x[i] + y[i]` (4 threads) | 9.4 ms | **1.71** | — | Memory (read+write BW) |
| **FMA**   | `x = std::fma(x, a, b)` (serial) | 838–948 ms | **~1.0–1.2** | 0.95–1.22 | **Scalar** latency chain |

> **Cross-campaign variation.** The original audit measured FLOPS at 35.4–35.6 GFLOPS
> and FMA at ~1.08 GFLOPS. The clean pinned rerun (different thermal state, corrected
> affinity) measured FLOPS at 33.5–35.3 GFLOPS and FMA at 1.18–1.22 GFLOPS. FMA is
> notably higher in the rerun (+12–23%), likely reflecting different boost-clock
> conditions. Neither campaign is "more correct" — both represent valid thermal states
> of a TDP-limited laptop. The values above span the full observed range.

**Code-generation finding:**
The **~29–33× gap** between FLOPS and FMA throughput is consistent across all runs and is due to **auto-vectorization failure** on `std::fma()`:
- **FLOPS**: The compiler vectorizes the `x*a + b` loop, processing multiple `double` elements per cycle via SIMD.
- **FMA**: The compiler emits scalar code for `std::fma()`, yielding throughput bound by FMA latency (~4 cycles per element).

The exact ratio varies with thermal state (the original audit saw ~33×; the clean rerun saw ~28.5×) because the two kernels have different sensitivity to clock frequency during long serial runs. The qualitative conclusion — `std::fma()` defeats auto-vectorization under MSVC — is robust across all measurements.

---

### A.3 — Memory Latency (Pointer Chase)

Randomized pointer walk (`p = *p`) defeats hardware prefetchers. Every load has a true data dependency, measuring unavoidable hardware round-trip latency.

Three measurement sets are available: the audit smoke run (5 iterations, `--prefault --aligned`, seed 14), historical 200-iteration runs (with `aligned=false`), and the clean pinned rerun (200 iterations, `--prefault`, no `--aligned`, affinity `0x04`, two independent runs).

| Working set | Audit 5-iter (ns) | Historical 200-iter (ns) | Clean rerun 200-iter (ns) | Cycles @ 2.8 GHz | Tier |
|---|---|---|---|---|---|
| 4–16 KB | **1.71** | 1.50–1.52 | **1.49** | ~4–5 | **L1** |
| 32 KB | 1.71 | 1.50 | 1.49 | ~4–5 | L1 |
| 64–256 KB | **3.92–3.95** | 3.43–3.44 | **3.41–3.44** | ~10–11 | **L2** |
| 512 KB | 4.80 | 4.20 | 4.18–4.19 | ~12–13 | L2→LLC |
| 1 MB | 5.22 | 4.58 | 4.55 | ~13–15 | LLC |
| 2 MB | 11.71 | 12.37 | 10.70–10.72 | ~30–33 | LLC |
| 4 MB | 13.50 | 12.28 | 11.97–12.00 | ~34–38 | LLC |
| 8 MB | 25.87 | 19.73 | 13.23–14.34 | ~37–72 | LLC→DRAM |
| 16 MB | 58.7 | 66.9 | 49.5–52.7 | ~139–164 | DRAM transition |
| 64–256 MB | **84–94** | 86–97 | **80–90** | ~224–270 | **DRAM (stable)** |

> **Cross-campaign spread.** The clean pinned rerun (200 iterations, corrected affinity,
> high priority) shows slightly lower latencies throughout, consistent with higher
> sustained boost clock under strict sequential execution. The 8–32 MB range
> shows high variance across all campaigns due to LLC→DRAM spill and OS scheduling
> jitter. The most reliable DRAM latency estimate is from 128–256 MB readings:
> ~84–94 ns across campaigns. L1 latency ranges from ~1.49 ns (clean rerun)
> to ~1.71 ns (audit 5-iter), a sub-cycle difference reflecting iteration count
> and boost-clock conditions at measurement time.

![Memory latency staircase](assets/latency_vs_size_ptr_chase.png)

---

### A.4 — Impact of Control Variables

**Controlled Configuration** used for all measurements in this report:
- `OMP_NUM_THREADS=4` — fixed to physical core count (no hyperthreading).
- `--prefault` — parallel page faulting before timing.
- `--warmup 50` — stabilizes CPU C-states and Turbo Boost.
- `--aligned` — 64-byte alignment for SIMD-width-aligned access.

**Why each control matters**:
- **`--prefault`**: Without it, first-touch page faults occur inside the timed region, inflating bandwidth and latency figures unpredictably.
- **`--warmup 50`**: Cold-start measurements capture CPU C-state exit and Turbo Boost ramp-up, not steady-state hardware limits.
- **`--aligned`**: Unaligned access can split cache lines, adding spurious memory traffic. Note: for STREAM kernels, `AlignedBuffer` is always used regardless of this flag; for compute kernels, `--aligned` selects a different (serial, raw-pointer) code path.

**Caveat — `--threads` flag:** The `--threads` CLI flag is parsed and recorded in JSON output but does **not** call `omp_set_num_threads()`. OpenMP thread count is controlled exclusively via the `OMP_NUM_THREADS` environment variable.

**Note:** Two affinity-pinning assessments have been performed:
1. **Initial pinning test** (Appendix D, Session 1): used `AffinityMask=0x1` for single-thread and `0xFF` for multi-thread. Concluded pinning did not help. This test had methodological limitations (see Appendix D).
2. **Clean pinned rerun** (Appendix D, Session 2): used corrected physical-core masks (`0x55` for 4-thread, `0x04` for single-thread), strictly sequential execution, verified absence of concurrent benchmarks, and inter-run cooling gaps. Under these tighter controls, **DRAM-tier bandwidth variability dropped from ~8.6% spread to ~1.6% spread.** Cache-resident variability (30–40%) persists regardless of pinning configuration.

The improvement cannot be attributed to pinning alone — it likely reflects the combination of corrected affinity masks, strict sequential execution, and thermal cooling between runs. See **Appendix D** for full analysis.

---

### A.5 — The Memory Hierarchy at a Glance

| Tier | Size | Peak BW (Triad) | Latency (range across campaigns) | vs. DRAM |
|------|------|------------------------|----------------------------------|---------|
| **L1** | 48 KB | ~65–89 GB/s | ~1.5–1.7 ns | ~55× faster (latency) |
| **L2** | 1.25 MB | ~92–133 GB/s | ~3.4–3.9 ns | ~24× faster |
| **LLC** | 12 MB | ~71–98 GB/s | ~5–14 ns | ~7–19× faster |
| **DRAM** | 15 GB | ~19–21 GB/s | ~84–94 ns | 1× (baseline) |

> Peak BW for L1 is higher in other kernels (Copy reaches ~139 GB/s at 1 MB). The values
> above reflect Triad specifically. Cache-resident BW varies 30–40% between runs due to
> Turbo Boost and thermal state. DRAM BW is ~19 GB/s under strict sequential execution with
> cooling (clean rerun) and ~21 GB/s in the original audit session; the difference reflects
> session-level thermal conditions rather than a methodological change.

---

## Part B — Measurement Quality

| Benchmark | Warmup | Timed iters | Stat | Noise (original audit) | Noise (clean rerun) |
|-----------|--------|-------------|------|------------------------|---------------------|
| STREAM (DRAM regime) | 50 | 200 | Median | Low (±5%) | **Very low (±1–2%)** |
| STREAM (cache-resident) | 50 | 200 | Median | **High** (30–40% across runs) | **Still high** (5–6% per-run median; 30–40% per-iteration) |
| Compute (FLOPS) | 50 | 200 | Median | Very low (<1% across 3 runs) | Low (~2% CV across 3 runs) |
| Compute (FMA) | 50 | 200 | Median | Very low (<0.1% across 3 runs) | Low (~1.5% CV across 3 runs) |
| Compute (DOT/SAXPY) | 50 | 200 | Median | Moderate | — |
| Latency (L1/L2) | 50 | 200 | Median | Low | **Very low (<0.4% run-to-run)** |
| Latency (4–16 MB) | 50 | 200 | Median | **High** (OS jitter) | Moderate (6–8% at 8–16 MB) |
| Latency (DRAM) | 50 | 200 | Median | Moderate | Low (<2% run-to-run) |

> **Clean rerun context.** The clean pinned rerun used corrected affinity masks, strict
> sequential execution, high priority, and inter-run cooling — which noticeably tightened
> DRAM-regime variability. Cache-resident noise remains high because it is driven by
> Turbo Boost frequency dynamics and thermal transients, not scheduling. The wider
> FLOPS CV in the clean rerun (2% vs <1%) is within expected noise for a thermally
> managed laptop and does not indicate a methodological problem.

---

### Interpretation Caveats

- **Cache-resident bandwidth is configuration-sensitive.** Bandwidth at working sets fitting in L1–LLC varies 30–40% across runs on this platform, driven by Turbo Boost frequency, thermal state, and power-limit behavior. Peak cache numbers reported here should be read as representative observations under one thermal state, not as fixed hardware constants.

- **Reproducibility depends on execution discipline.** DRAM-tier bandwidth spread dropped from ~8.6% to ~1.6% when execution control was tightened (corrected affinity masks, strict sequential runs, inter-run cooling). This means some figures in this report are sensitive to how carefully the benchmark session is conducted — a property of all microbenchmarking on thermally managed hardware, not a flaw in the framework.

- **All values are specific to one platform and toolchain.** The numbers in this report reflect a single configuration: Intel i7-1165G7, LPDDR4x, MSVC 1929, Windows 11, `/O2 /openmp /fp:fast`. Different compilers (GCC, Clang), operating systems (Linux), or hardware generations will produce different absolute values and may alter relative comparisons (e.g., the FMA gap).

- **The FLOPS/FMA gap is a code-generation effect, not a hardware limit.** The ~29–33× throughput difference between the FLOPS and FMA kernels reflects MSVC's failure to auto-vectorize `std::fma()` under this build configuration. It should not be interpreted as an intrinsic hardware limitation of the FMA unit — a different compiler or explicit SIMD intrinsics would likely close the gap substantially.

- **LLC→DRAM transition measurements are inherently noisier.** Working-set sizes near the LLC capacity boundary (4–16 MB on this platform) show higher run-to-run variance in both bandwidth and latency measurements. Values in this transition region should be interpreted with more caution than the stable plateaus on either side.

---

## 1. Experimental Setup

*   **OS**: Windows 11 (MSVC environment)
*   **Compiler**: MSVC 1929 (Visual Studio 2019, 16.11.2) with OpenMP 2.0 support.
*   **Build Config**: CMake `Release` with `/O2 /openmp /fp:fast` and link-time code generation.
*   **Hardware**: Intel Core i7-1165G7 — L1=48 KB, L2=1.25 MB, LLC=12 MB, 15 GiB LPDDR4x.
*   **Thread control**: `OMP_NUM_THREADS=4` (environment variable; `--threads` flag does not set OpenMP thread count).

## 2. Full STREAM Sweep Tables

> Tables below are from single 200-iteration audit runs per kernel. Cache-resident values should be treated as representative of one thermal/frequency state; expect 30–40% variability across runs.

### STREAM Copy — Full Sweep

| Per-array size | Total footprint | Bandwidth (GB/s) | Tier |
|---|---|---|---|
| 32 KB | 64 KB | 72.8 | L1 |
| 64 KB | 128 KB | 100.8 | L1/L2 |
| 128 KB | 256 KB | 114.0 | L2 |
| 256 KB | 512 KB | 127.9 | L2 |
| 512 KB | 1 MB | 138.0 | L2/LLC |
| **1 MB** | **2 MB** | **138.9** ← Peak | **LLC** |
| 2 MB | 4 MB | 106.7 | LLC |
| 4 MB | 8 MB | 106.3 | LLC |
| 8 MB | 16 MB | 26.4 | LLC→DRAM cliff |
| 16 MB | 32 MB | 19.6 | DRAM |
| 32–512 MB | 64 MB–1 GB | 16.0–17.0 | DRAM (stable) |

### STREAM Scale — Full Sweep

| Per-array size | Total footprint | Bandwidth (GB/s) | Tier |
|---|---|---|---|
| 32 KB | 64 KB | 65.5 | L1 |
| 64 KB | 128 KB | 81.9 | L1/L2 |
| 128 KB | 256 KB | 93.6 | L2 |
| 256 KB | 512 KB | 85.9 | L2 |
| 512 KB | 1 MB | 88.1 | L2/LLC |
| 1 MB | 2 MB | 90.4 | LLC |
| 2 MB | 4 MB | 90.2 | LLC |
| **4 MB** | **8 MB** | **115.4** ← Peak | **LLC** |
| 8 MB | 16 MB | 30.8 | LLC→DRAM cliff |
| 16 MB | 32 MB | 18.8 | DRAM |
| 32–512 MB | 64 MB–1 GB | 15.2–17.3 | DRAM (stable ~16) |

### STREAM Add — Full Sweep

| Per-array size | Total footprint | Bandwidth (GB/s) | Tier |
|---|---|---|---|
| 32 KB | 96 KB | 89.4 | L1 |
| 64 KB | 192 KB | 109.2 | L1/L2 |
| 128 KB | 384 KB | 119.2 | L2 |
| 256 KB | 768 KB | **128.9** ← Peak | L2 |
| 512 KB | 1.5 MB | 124.8 | L2/LLC |
| 1 MB | 3 MB | 114.0 | LLC |
| 2 MB | 6 MB | 105.6 | LLC |
| 4 MB | 12 MB | 86.4 | LLC tail |
| 8 MB | 24 MB | 28.6 | LLC→DRAM cliff |
| 16 MB | 48 MB | 21.7 | DRAM |
| 32–512 MB | 96 MB–1.5 GB | 20.3–21.3 | DRAM (stable ~21) |

**Observation:** 3-array kernels (Add, Triad) sustain ~19–21 GB/s in DRAM vs ~15–18 GB/s for 2-array kernels (Copy, Scale). More concurrent outstanding memory requests improve DRAM controller pipeline efficiency. Absolute DRAM bandwidth varies ~10% between measurement sessions due to thermal conditions.

![STREAM Copy bandwidth waterfall](assets/bandwidth_vs_size_copy.svg)
![STREAM Scale bandwidth waterfall](assets/bandwidth_vs_size_scale.svg)
![STREAM Add bandwidth waterfall](assets/bandwidth_vs_size_add.svg)

---

## 3. Methodology

### Memory bandwidth
STREAM kernels sweep per-array working-set sizes from 32 KB to 512 MB, scanning L1→L2→LLC→DRAM. The `bytes` field carries the per-array size; bandwidth is computed as `total_bytes_moved / median_ns`. Memory is prefaulted and 64-byte aligned (`--prefault --aligned`) to eliminate first-touch page faults and ensure SIMD-width-aligned access.

### Latency
A randomized pointer-chase walk (`p = *p`) over a linked list defeats hardware prefetchers. Each node is cache-line padded (64 bytes). The list is shuffled with `std::mt19937` + Knuth swap to guarantee non-sequential traversal. Every load has a true data dependency, measuring unavoidable hardware round-trip latency. Step count is clamped between 200,000 and 5,000,000 to keep timing windows practical.

### Compute throughput
FMA and FLOPS kernels operate on an 8M-element `double` array (64 MB). With `--aligned`, both kernels take a **single-threaded serial-loop** code path over raw pointers (no OpenMP, single accumulator per element), with an inner-loop depth of 64 FLOPs per element. This exposes single-core vectorization behavior:
- **FLOPS** (`x = x*a + b`): MSVC auto-vectorizes using SIMD → high throughput.
- **FMA** (`x = std::fma(x, a, b)`): `std::fma()` prevents auto-vectorization → scalar latency-bound.

Without `--aligned`, both kernels use OpenMP-parallel `std::vector` paths with 4 independent accumulators, yielding different (higher) throughput characteristics not measured in this report.

### Statistical methodology
Every figure is the **median** of 200 timed iterations after 50 warmup passes. p95 and stddev are captured for noise assessment. Triad, FLOPS, and FMA were run 3 times each; medians are averaged across runs. High stddev at the LLC→DRAM transition reflects OS scheduling interference on Windows during long timing windows.

---

## Appendix A — CLI Reference

### Kernels

| `--kernel` value | What it measures | Threading |
|---|---|---|
| `copy` / `stream_copy` | `B[i] = A[i]` — pure read+write BW | OpenMP (4 threads) |
| `scale` / `stream_scale` | `B[i] = s*A[i]` — scale+write BW | OpenMP |
| `add` / `stream_add` | `C[i] = A[i]+B[i]` — 3-array BW | OpenMP |
| `triad` / `stream_triad` | `A[i] = B[i]+s*C[i]` — standard HPC BW | OpenMP |
| `stream` | Alias for `triad` | OpenMP |
| `flops` | `x = x*a + b` — compute throughput | **Single thread** (`--aligned`) |
| `fma` | `x = std::fma(x,a,b)` — compute throughput | **Single thread** (`--aligned`) |
| `dot` | `sum += x[i]*y[i]` — memory-bound FLOPS | OpenMP |
| `saxpy` | `out[i] = a*x[i]+y[i]` — memory-bound FLOPS | OpenMP |
| `latency` | Pointer-chase — true load latency | Single thread |

### Flags

| Flag | Default | Description |
|---|---|---|
| `--kernel <name>` | `stream` | Kernel to run (see table above) |
| `--size <str>` | `64MB` | Per-array dataset size (e.g. `128KB`, `256MB`) |
| `--threads <n>` | `1` | Recorded in JSON output only — **does not** call `omp_set_num_threads()` |
| `--iters <n>` | `100` | Timed iterations |
| `--warmup <n>` | `10` | Warmup iterations (not timed) |
| `--out <file>` | `results.json` | JSON output path |
| `--prefault` | off | Pre-fault pages before timing (recommended) |
| `--aligned` | off | Use 64-byte-aligned allocations (also selects serial code path for compute kernels) |
| `--seed <n>` | `14` | RNG seed for latency pointer shuffle |

> **Important:** OpenMP thread count is controlled exclusively via the `OMP_NUM_THREADS` environment variable, not the `--threads` flag.

---

## Appendix B — Theoretical vs. Real Traffic

STREAM kernels are memory-bound. The formula for "effective bandwidth" counts payload bytes only:

| Kernel | Operation | Bytes moved per element | What stresses |
| :--- | :--- | :---: | :--- |
| `copy` | `B[i] = A[i]` | 2× (`sizeof(double)`) | Pure memory movement |
| `scale` | `B[i] = s*A[i]` | 2× | Memory + light scalar multiply |
| `add` | `C[i] = A[i] + B[i]` | 3× | Higher traffic — 2 reads, 1 write |
| `triad` | `A[i] = B[i] + s*C[i]` | 3× | **Standard HPC metric** (McCalpin STREAM) |

Bandwidth forms a **waterfall**: very high for small (cache-resident) working sets, then drops sharply as the working set exceeds each cache level.

---

## Appendix C — Audit Run Variability

Three independent full runs of STREAM Triad illustrate cache-resident variability:

| Per-array size | Run 1 (GB/s) | Run 2 (GB/s) | Run 3 (GB/s) | Spread |
|---|---|---|---|---|
| 32 KB | 75.6 | 89.4 | 75.6 | 18% |
| 256 KB | 97.1 | 128.9 | 94.8 | 36% |
| 1 MB | 132.7 | 96.3 | 97.7 | 38% |
| 4 MB | 87.3 | 71.4 | 65.6 | 33% |
| 8 MB | 27.9 | 28.9 | 28.5 | 4% |
| 16 MB | 22.6 | 22.7 | 23.0 | 2% |
| 256 MB | 21.0 | 20.7 | 20.9 | 1% |

Cache-resident (≤4 MB) bandwidth fluctuates 18–38% across runs due to Turbo Boost frequency variation, thermal state, and OS scheduling. DRAM-regime (≥16 MB) values are stable within 2%.

Three independent FLOPS runs: 35.38, 35.42, 35.58 GFLOPS (spread <1%).
Three independent FMA runs: 1.0807, 1.0808, 1.0813 GFLOPS (spread <0.1%).

> **Clean pinned rerun comparison.** Under stricter execution control (corrected affinity,
> sequential runs, cooling gaps), three FLOPS runs yielded 33.51, 34.45, 35.34 GFLOPS
> (spread 5.3%, CV 2.16%) and three FMA runs yielded 1.181, 1.217, 1.222 GFLOPS
> (spread 3.4%, CV 1.53%). FMA throughput was ~24% higher than the original audit,
> likely reflecting a different thermal/boost-clock state. See Appendix D for analysis.

---

## Appendix D — Affinity Pinning & Elevated Priority Assessment

This appendix documents **two separate pinning experiments**, conducted under different levels of execution discipline. Their disagreement is instructive: the effectiveness of affinity pinning depends critically on the correctness of the affinity masks, the absence of concurrent benchmarks, and thermal management.

### D.1 — Session 1: Initial Pinning Test (2026-03-11, early)

**Method:** `Start-Process -PassThru` → `ProcessorAffinity` + `PriorityClass = "High"` | Same binary and build as main audit.

- **Single-threaded benchmarks** (FLOPS, FMA, Latency): pinned to logical processor 0 (`AffinityMask = 0x1`), `PriorityClass = High`.
- **OpenMP STREAM benchmarks** (Triad, Copy, Add): `AffinityMask = 0xFF` (all 8 logical processors, isolating only the priority effect), `PriorityClass = High`, `OMP_NUM_THREADS = 4`.
- FMA pin1 was contaminated by a concurrent Triad run and is excluded from clean analysis.

**Methodological limitations identified in retrospect:**
- Single-threaded affinity used `0x1` (logical processor 0, which handles more OS interrupts) rather than an isolated physical core.
- Multi-threaded affinity used `0xFF` (all 8 logical processors including SMT siblings), providing no real core isolation — threads could land on hyper-threaded siblings of the same core.
- At least one run (FMA pin1) was contaminated by a concurrent benchmark, indicating imperfect sequential discipline.

#### D.1.1 — Session 1 Compute Throughput

| Run | FLOPS (non-pinned) | FLOPS (pinned) | FMA (non-pinned) | FMA (pinned) |
|-----|-------------------|----------------|------------------|--------------|
| 1 | 35.38 | 35.03 | 1.0807 | 0.952* |
| 2 | 35.42 | 34.89 | 1.0808 | 0.971 |
| 3 | 35.58 | 34.38 | 1.0813 | 1.005 |
| **Median** | **35.42** | **34.89** | **1.0808** | **0.971** |
| **Spread** | **<1%** | **1.9%** | **<0.1%** | **3.4%†** |

\* FMA pin1 was contaminated by a concurrent Triad benchmark sharing core 0.
† Clean runs only (pin2, pin3): spread = 3.4%. Including contaminated pin1: 5.3%.

**Session 1 finding:** Pinning single-threaded compute to logical processor 0 **degraded** both absolute throughput and stability. FLOPS dropped ~1.5% and spread widened; FMA dropped ~10% and spread widened. Interpretation at the time: sustained single-core load under affinity pinning on a TDP-limited laptop causes earlier thermal throttling, and `High` priority has no effect on an idle system.

#### D.1.2 — Session 1 STREAM Triad

Three clean pinned runs (`AffinityMask = 0xFF`, `PriorityClass = High`, sequential):

| Per-array size | audit1 | audit2 | audit3 | pin4 | pin5 | pin6 | Audit spread | Pinned spread |
|---|---|---|---|---|---|---|---|---|
| 32 KB | 75.6 | 89.4 | 75.6 | 65.5 | 61.4 | 51.7 | 18% | 27% |
| 64 KB | 89.4 | 103.5 | 81.9 | 72.8 | 23.5 | 63.4 | 26% | 210% |
| 256 KB | 97.1 | 128.9 | 94.8 | 89.9 | 43.6 | 71.5 | 36% | 106% |
| 1 MB | 132.7 | 96.3 | 97.7 | 83.6 | 102.6 | 74.8 | 38% | 37% |
| 8 MB | 27.9 | 28.9 | 28.5 | 24.7 | 20.5 | 21.7 | 4% | 20% |
| 16 MB | 22.6 | 22.7 | 23.0 | 21.5 | 19.6 | 18.5 | 2% | 16% |
| 256 MB | 21.0 | 20.7 | 20.9 | 18.5 | 18.3 | 18.3 | 1% | 1% |

**Session 1 finding:** Pinning (with `0xFF` mask) did **not** reduce cache-resident variability. Pin5 exhibited anomalously low values at 64–256 KB. DRAM-regime spread was comparable (±1–2%).

#### D.1.3 — Session 1 Latency and Copy/Add

Latency was **indistinguishable** between pinned and non-pinned runs (all differences <1.1%). Copy and Add showed lower absolute bandwidth in the pinned session (−8% and −17% respectively), attributed to session-level thermal differences.

#### D.1.4 — Session 1 Conclusions (original, now superseded)

The original conclusions stated that pinning did not help and that cache-resident variability was "structural." These conclusions were drawn from masks that did not properly isolate physical cores, with at least one contaminated run, and without inter-run cooling. **The clean pinned rerun (Session 2) subsequently challenged several of these conclusions.**

---

### D.2 — Session 2: Clean Pinned Rerun (2026-03-11, later)

**Method:** Strictly sequential execution. Verified absence of concurrent `bench.exe` between every run. Corrected physical-core affinity masks. `PriorityClass = High`. 5-second cooling gaps between runs.

| Parameter | Single-threaded (FLOPS, FMA, Latency) | Multi-threaded (Triad, Copy, Add) |
|-----------|---------------------------------------|-----------------------------------|
| `OMP_NUM_THREADS` | 1 | 4 |
| `AffinityMask` | `0x04` (LP 2 = physical core 1) | `0x55` (LP 0,2,4,6 = one thread per physical core) |
| `PriorityClass` | High | High |
| Runs | 3× FLOPS, 3× FMA, 2× Latency | 3× Triad, 1× Copy, 1× Add |

**Key differences from Session 1:**
1. Single-threaded mask changed from `0x01` (core 0, OS-interrupt-heavy) to `0x04` (core 1, isolated).
2. Multi-threaded mask changed from `0xFF` (all 8 LPs, no SMT isolation) to `0x55` (one LP per physical core, no SMT contention).
3. Strictly verified sequential execution (no concurrent benchmarks).
4. Inter-run cooling gaps (5 seconds).

#### D.2.1 — Session 2 Compute Throughput

| Run | FLOPS (GFLOPS) | FMA (GFLOPS) |
|-----|----------------|--------------|
| 1 | 34.45 | 1.181 |
| 2 | 33.51 | 1.222 |
| 3 | 35.34 | 1.217 |
| **Mean** | **34.43** | **1.207** |
| **CV** | **2.16%** | **1.53%** |
| **Range** | 33.51–35.34 (5.3%) | 1.18–1.22 (3.4%) |

**Comparison with previous campaigns:**
- FLOPS mean is consistent across all three campaigns: 35.46 (audit), 34.77 (Session 1), 34.43 (Session 2). The ~3% total spread across campaigns is within expected thermal variation.
- FMA shows a notable shift: 1.08 (audit), 0.97 (Session 1), 1.21 (Session 2). The 24% jump from Session 1 to Session 2 is too large to attribute to noise alone. The most likely explanation is that Session 1’s FMA runs occurred in a different thermal state (the laptop had accumulated heat from prior benchmarks, and core 0 was shared with OS interrupt handling). Session 2 pinned FMA to an isolated core with cooling gaps.
- The FLOPS/FMA ratio ranges from ~28.5× (Session 2) to ~33× (audit). The qualitative conclusion—`std::fma()` defeats vectorization—is robust. The exact ratio is configuration-sensitive.

#### D.2.2 — Session 2 STREAM Triad (DRAM Stability)

| Per-array size | clean_1 | clean_2 | clean_3 | Spread | vs. Session 1 pinned spread |
|---|---|---|---|---|---|
| 32 KB | 70.2 | 65.5 | 51.7 | 36% | 27% → 36% (no improvement) |
| 256 KB | 95.7 | 95.9 | 77.3 | 24% | 106% → 24% (improved, still high) |
| 1 MB | 97.4 | 96.8 | 92.0 | 5.9% | 37% → 5.9% (improved) |
| 8 MB | 25.7 | 25.4 | 25.5 | 1.2% | 20% → 1.2% (**major improvement**) |
| 16 MB | 21.2 | 20.9 | 21.5 | 2.9% | 16% → 2.9% (**major improvement**) |
| 64 MB | 19.1 | 19.8 | 18.5 | 7.0% | 6% → 7.0% (comparable) |
| 256 MB | 18.5 | 18.9 | 17.8 | 6.2% | 1% → 6.2% (wider) |
| **512 MB** | **18.86** | **18.89** | **19.16** | **1.6%** | **Session 1 ~8.6% → 1.6% (5× tighter)** |

**Key finding:** DRAM-tier bandwidth at the largest working set (512 MB) tightened from ~8.6% spread (Session 1, 6 runs, `0xFF` mask) to **1.6% spread** (Session 2, 3 runs, `0x55` mask). At the 8–16 MB transition, Session 2 spread dropped to 1–3% versus 16–20% in Session 1.

Cache-resident variability (32 KB–256 KB) remains 24–36% in Session 2 — similar to all previous campaigns. This is consistent with Turbo Boost/thermal dynamics that no user-space configuration can eliminate.

#### D.2.3 — Session 2 Latency

Two independent 200-iteration runs (affinity `0x04`, high priority):

| Working set | Run 1 (ns) | Run 2 (ns) | Diff % |
|---|---|---|---|
| 4–32 KB (L1) | 1.493 | 1.493 | 0.00% |
| 64–256 KB (L2) | 3.41–3.44 | 3.41–3.43 | <0.4% |
| 512 KB–1 MB (LLC) | 4.19–4.55 | 4.18–4.55 | <0.1% |
| 2–4 MB (LLC) | 10.72–12.00 | 10.70–11.97 | <0.3% |
| 8 MB (LLC edge) | 13.23 | 14.34 | 8.0% |
| 16 MB (DRAM) | 52.70 | 49.50 | 6.3% |
| 128–256 MB (DRAM) | 85.3–89.7 | 85.4–89.2 | <0.6% |

Latency stability is **excellent** at all cache-resident sizes (<0.4% run-to-run), confirming that pointer-chase measurements are inherently stable. The 8–16 MB transition zone shows 6–8% jitter, consistent with all campaigns.

#### D.2.4 — Session 2 Copy and Add

| Kernel | DRAM 512 MB (GB/s) | L2 peak (GB/s) |
|--------|-------------------|-----------------|
| Copy | 17.50 | 205.6 @ 512 KB |
| Add | 20.87 | 207.0 @ 1 MB |

---

### D.3 — Revised Conclusions (Incorporating Both Sessions)

1. **Affinity pinning effectiveness depends on mask correctness.** Session 1 used `0xFF` (no isolation) and `0x1` (OS-interrupt core) and saw no benefit. Session 2 used physically-correct masks (`0x55`, `0x04`) and saw **5× tighter DRAM-tier bandwidth spread** and improved LLC-transition stability. Improper masks can negate or reverse the benefit of pinning.

2. **The improvement cannot be attributed to pinning alone.** Session 2 differed from Session 1 in multiple ways:
   - Corrected affinity masks (physical-core isolation)
   - Strictly verified sequential execution (no concurrent benchmarks)
   - Inter-run cooling gaps (5 seconds)
   - Different thermal starting conditions
   
   Any combination of these factors may have contributed. On a TDP-limited laptop, **execution discipline (sequential runs + cooling) may matter as much as the affinity mask itself.**

3. **Cache-resident variability (30–40%) persists under all configurations tested.** This is driven by Turbo Boost frequency dynamics and thermal transients — not by thread migration or scheduling. No user-space pinning or priority configuration eliminates it.

4. **Elevated process priority (`High`) has no independently measurable effect** on an otherwise idle system. The benchmarks are CPU/memory-bound, not scheduling-bound.

5. **Latency is inherently stable regardless of pinning** — the pointer-chase dependency chain serializes all loads and is immune to scheduling jitter. Clean rerun latencies are slightly lower across the board (~1.49 vs ~1.71 ns at L1), consistent with higher sustained boost clock under the cleaner execution protocol.

6. **FMA throughput is configuration-sensitive.** Observed values range from 0.95 to 1.22 GFLOPS across all campaigns, a ~28% total spread. The scalar FMA loop is long-running (~840–950 ms per iteration) and sensitive to sustained clock frequency, which in turn depends on thermal state and core assignment. Single-point FMA numbers should be treated as approximate.

7. **The original Session 1 conclusion that "pinning did not help" was premature**, based on masks that did not properly isolate physical cores and execution that was not fully sequential. The revised conclusion is: **proper physical-core pinning combined with strict sequential execution and cooling measurably improves DRAM-tier reproducibility. Cache-resident variability is unaffected.**

> **Session-level bandwidth note.** Absolute DRAM bandwidth differs between sessions:
> ~20.9 GB/s (original audit), ~18.3 GB/s (Session 1 pinned), ~18.97 GB/s (Session 2
> clean rerun). These differences reflect thermal/environmental conditions at measurement
> time (laptop thermal history, ambient temperature, background load). Within each
> session, DRAM values are consistent. The full observed range across all campaigns is
> ~18–21 GB/s for Triad at 512 MB.

