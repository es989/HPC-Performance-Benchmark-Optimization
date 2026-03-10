#include "config.hpp"
#include "results.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "stream_kernels.hpp"
#include "aligned_buffer.hpp"

#include <vector>
#include <cstddef>
#include <iostream>
#include <algorithm>

/**
 * @brief Convert bytes to number of double elements.
 *
 * Since the STREAM kernels operate on arrays of `double` (8 bytes each),
 * we need to convert the requested working set size in bytes to the
 * corresponding array length.
 *
 * @param bytes The total size of one array in bytes.
 * @return The number of `double` elements.
 */
static std::size_t bytes_to_elems(std::size_t bytes) {
    return bytes / sizeof(double);
}

/**
 * @brief Build a sweep across typical cache/DRAM regions.
 *
 * Generates a vector of array sizes (in bytes) to test. The sizes are chosen
 * to span from small enough to fit in L1 cache (32KB) up to large enough
 * to force DRAM access (512MB). This creates the "waterfall" plot effect.
 *
 * @return A vector of sizes in bytes.
 */
static std::vector<std::size_t> build_sweep_bytes() {
    std::vector<std::size_t> sizes;

    // 32KB -> 256KB each step increase *2 (Targets L1/L2 caches)
    for (std::size_t kb = 32; kb <= 256; kb *= 2) sizes.push_back(kb * 1024);

    // 512KB -> 8MB (Targets L2/L3/LLC caches)
    for (std::size_t kb = 512; kb <= 8192; kb *= 2) sizes.push_back(kb * 1024);

    // 16MB -> 512MB (Targets Main Memory / DRAM)
    for (std::size_t mb = 16; mb <= 512; mb *= 2) sizes.push_back(mb * 1024ull * 1024ull);

    return sizes;
}

/**
 * @brief Run STREAM sweep for one kernel op (copy/scale/add/triad).
 *
 * This is the core measurement loop for memory bandwidth. It iterates over
 * the sizes created by `build_sweep_bytes()`, allocates the arrays,
 * performs a warmup phase, and then times the kernel execution.
 *
 * Output:
 * - one BenchmarkResult::Point per size
 * - each point contains median/p95 and effective bandwidth
 *
 * @param conf The parsed configuration (warmup, iters, prefault, etc.).
 * @param res The result object to populate with sweep points.
 * @param op The specific STREAM operation to run.
 */
void run_stream_sweep(const Config& conf, BenchmarkResult& res, StreamOp op) {
    const KernelDesc kd = make_stream_desc(op);
    const auto sweep = build_sweep_bytes();

    for (std::size_t size_bytes : sweep) {
        const std::size_t n = bytes_to_elems(size_bytes);
        if (n == 0) continue;

        // Inputs/outputs (aligned to 64 bytes for AVX-512/Cache lines)
        benchmark::AlignedBuffer<double> A, B, C;
        try {
            A = benchmark::AlignedBuffer<double>(n, 64);
            B = benchmark::AlignedBuffer<double>(n, 64);
            C = benchmark::AlignedBuffer<double>(n, 64);
        } catch (const std::bad_alloc&) {
            std::cerr << "[ERROR] Out of Memory allocating "
                      << (3 * size_bytes) / (1024 * 1024)
                      << " MB. Skipping size.\n";
            continue;
        }
        const double s = 3.0;

        // First-touch NUMA parallel initialization
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < static_cast<long long>(n); ++i) {
            A[i] = 1.0;
            B[i] = 2.0;
            C[i] = 3.0;
        }
        
        // Optional: prefault / pre-touch pages in parallel to preserve NUMA binding
        if (conf.prefault) {
            const std::size_t page_elems = 4096 / sizeof(double);
            #pragma omp parallel for schedule(static)
            for (long long idx = 0; idx < static_cast<long long>(n); idx += static_cast<long long>(page_elems)) {
                volatile double t = A[idx]; A[idx] = t;
                volatile double u = B[idx]; B[idx] = u;
                volatile double v = C[idx]; C[idx] = v;
            }
            do_not_optimize_away(A[0]);
        }

        // ---- Warmup phase (not timed) ----
        for (int w = 0; w < conf.warmup; ++w) {
            kd.fn(A.data(), B.data(), C.data(), s, n);
            do_not_optimize_away(A[static_cast<std::size_t>(w) % n]);
        }

        // ---- Measurement phase: collect per-iteration samples ----
        //saves time for each iteration
        std::vector<long long> samples;
        samples.reserve(conf.iters);
        for (int it = 0; it < conf.iters; ++it) {
            Timer t;

            // Reduce chance compiler moves memory ops across start/stop
            clobber_memory();
            t.start();

            kd.fn(A.data(), B.data(), C.data(), s, n);

            clobber_memory();
            const long long ns = t.elapsed_ns();
            samples.push_back(ns);

            // Per-iteration small sink (minimal overhead)
            // (it) % n ensures access stays within array boundaries
            do_not_optimize_away(A[static_cast<std::size_t>(it) % n]);
        }

        // ---- Validation (outside timed region) ----
        // Use sampled checksum for huge sizes (cheap, still meaningful).
        // 1024 samples fit entirely within L1 Cache (instant processing).
        // The odds of all 1024 sampled points being correct by "luck" are nearly zero.
        const std::size_t stride = std::max<std::size_t>(1, n / 1024); // ~1024 samples  
        const double sum_sample = Validator::checksum_sampled(A.data(), n, stride);
        do_not_optimize_away(sum_sample);

        // Optional full correctness check for small sizes (keep overhead low)
        if (size_bytes <= 8 * 1024 * 1024) {
            const double full = Validator::checksum_full(A.data(), n);

            // expected value per element for each op given B=2.0, C=3.0, s=3.0
            double expected_val = 0.0;
            switch (op) {
                case StreamOp::Copy:  expected_val = 2.0; break;
                case StreamOp::Scale: expected_val = s * 2.0; break;
                case StreamOp::Add:   expected_val = 2.0 + 3.0; break;
                case StreamOp::Triad: expected_val = 2.0 + s * 3.0; break;
            }

            const double expected_sum = static_cast<double>(n) * expected_val;
            if (!Validator::nearly_equal(full, expected_sum, 1e-9, 1e-9)) {
                std::cerr << "CRITICAL: Validation failed for " << kd.name()
                          << " at size_bytes=" << size_bytes << "\n";
            }
        }

        // ---- Statistics ----
        std::sort(samples.begin(), samples.end());
        const long long min_sample = samples.front();
        const long long max_sample = samples.back();

        const double med = percentile_ns(samples, 50.0);
        const double p95 = percentile_ns(samples, 95.0);

        std::vector<double> samples_double;
        samples_double.reserve(samples.size());
        for (auto ns : samples) {
            samples_double.push_back(static_cast<double>(ns));
        }
        const double stddev = compute_stddev(samples_double);

        // Effective bytes per iteration:
        // multiplier (2 or 3) * size_bytes (ONE array size in bytes) GB calculate prone to numeric error
        const double bytes_per_iter = kd.bytes_mult() * static_cast<double>(size_bytes);

        // Effective bandwidth computed from MEDIAN iteration time (stable) for scaling and numeric error 
        const double bw_gb_s = (bytes_per_iter / 1e9) / (med / 1e9);

        BenchmarkResult::Point pt;
        pt.bytes = size_bytes;
        pt.kernel = kd.name();
        pt.median_ns = med;
        pt.p95_ns = p95;
        pt.min_ns = static_cast<double>(min_sample);
        pt.max_ns = static_cast<double>(max_sample);
        pt.stddev_ns = stddev;
        pt.bandwidth_gb_s = bw_gb_s;
        pt.checksum = sum_sample;

        res.sweep_points.push_back(pt);
    }
}
