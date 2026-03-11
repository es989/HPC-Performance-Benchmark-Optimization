# Reproduction Notes

## Hardware & Software
- **CPU:** Intel Core i7-1165G7 @ 2.80 GHz, 4C/8T, Tiger Lake
- **RAM:** 15 GiB LPDDR4x
- **OS:** Windows 11
- **Compiler:** MSVC 19.29 (VS 2019, 16.11.2), `/O2 /openmp /fp:fast`, LTO
- **Build:** `cmake -S . -B build && cmake --build build --config Release`

## Benchmark Configuration
```
OMP_NUM_THREADS=4
--warmup 50 --iters 200 --prefault --aligned
```
- STREAM kernels: 4 OpenMP threads, `schedule(static)`
- FLOPS/FMA/Latency: single-threaded (serial code path via `--aligned`)

## Affinity & Priority (Clean Rerun)
| Scope | AffinityMask | Meaning |
|-------|-------------|---------|
| Multi-thread (Triad, Copy, Add) | `0x55` | LP 0,2,4,6 — one thread per physical core |
| Single-thread (FLOPS, FMA, Latency) | `0x04` | LP 2 — physical core 1 (avoids OS-heavy core 0) |

Priority: `PriorityClass = High` via `Start-Process -PassThru`.

## Execution Discipline
1. Verify no concurrent `bench.exe` in Task Manager between runs.
2. Run benchmarks **strictly sequentially** (one at a time).
3. Insert **5-second cooling gaps** between runs.
4. Reduce background activity (close browsers, IDEs, updates).

## Artifact Locations
| Campaign | Directory | Notes |
|----------|-----------|-------|
| Original audit (2026-03-10) | `audit_results/` | No affinity, normal priority |
| Clean pinned rerun (2026-03-11) | `pinned_results/clean_rerun/` | Corrected masks, sequential, cooling |

## Key Lessons Learned
- **Affinity masks must target physical cores.** `0xFF` (all LPs) provides no isolation; `0x55` (even LPs only) pins one thread per physical core on Tiger Lake.
- **Sequential execution matters as much as pinning.** Concurrent benchmarks sharing thermals and cache invalidate reproducibility.
- **Cache-resident variability (~30–40%) is inherent** to Turbo Boost / thermal dynamics and cannot be eliminated by user-space configuration.
- **DRAM-tier variability is controllable.** Spread dropped from ~8.6% to ~1.6% under tighter discipline.
- **FMA throughput is thermal-sensitive.** Observed range ~0.95–1.22 GFLOPS across campaigns; the long serial loop amplifies clock-frequency differences.
- **`--threads` flag does not set OpenMP threads.** Use `$env:OMP_NUM_THREADS` (PowerShell) or `export OMP_NUM_THREADS` (bash).
