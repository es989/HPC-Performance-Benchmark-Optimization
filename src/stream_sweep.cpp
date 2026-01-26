#include "config.hpp"
#include "results.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "stream_kernels.hpp"

#include <vector>
#include <cstddef>
#include <iostream>
#include <algorithm>

/**
 * @brief Convert bytes to number of double elements.
 */
static std::size_t bytes_to_elems(std::size_t bytes) {
    return bytes / sizeof(double);
}

/**
 * @brief Build a sweep across typical cache/DRAM regions.
 *
 * Note: Later can refine this using actual cache sizes of the CPU.
 */
static std::vector<std::size_t> build_sweep_bytes() {
    std::vector<std::size_t> sizes;

    // 32KB -> 256KB each step increase *2
    for (std::size_t kb = 32; kb <= 256; kb *= 2) sizes.push_back(kb * 1024);

    // 512KB -> 8MB
    for (std::size_t kb = 512; kb <= 8192; kb *= 2) sizes.push_back(kb * 1024);

    // 16MB -> 512MB
    for (std::size_t mb = 16; mb <= 512; mb *= 2) sizes.push_back(mb * 1024ull * 1024ull);

    return sizes;
}

/**
 * @brief Run STREAM sweep for one kernel op (copy/scale/add/triad).
 *
 * Output:
 * - one BenchmarkResult::Point per size
 * - each point contains median/p95 and effective bandwidth
 */
void run_stream_sweep(const Config& conf, BenchmarkResult& res, StreamOp op) {
    const KernelDesc kd = make_stream_desc(op);
    const auto sweep = build_sweep_bytes();

    for (std::size_t size_bytes : sweep) {
        const std::size_t n = bytes_to_elems(size_bytes);
        if (n == 0) continue;

        // Inputs/outputs (constant init => deterministic)
        std::vector<double> A(n, 1.0);
        std::vector<double> B(n, 2.0);
        std::vector<double> C(n, 3.0);
        const double s = 3.0;

        // ---- Warmup phase (not timed) ----
        for (int w = 0; w < conf.warmup; ++w) {
            kd.fn(A.data(), B.data(), C.data(), s, n);
            do_not_optimize_away(A[static_cast<std::size_t>(w) % n]);
        }

        // ---- Measurement phase: collect per-iteration samples ----
        //saves time for each iteration
        std::vector<long long> samples;
        samples.reserve(conf.iters);
        //measure loop
        for (int it = 0; it < conf.iters; ++it) {
            Timer t;

            // Reduce chance compiler moves memory ops across start/stop
            clobber_memory();
            t.start();

            kd.fn(A.data(), B.data(), C.data(), s, n);

            clobber_memory();
            const long long ns = t.elapsed_ns();
            samples.push_back(ns);

            // Per-iteration small sink (minimal overhead thats why we enter only one index to minimise )
            //(it) % n in order to stay in array boundries in different sizes
            do_not_optimize_away(A[static_cast<std::size_t>(it) % n]);
        }

        // ---- Validation (outside timed region) ----
        // Use sampled checksum for huge sizes (cheap, still meaningful).
        //1024 is a small enough number that the CPU can process it almost instantly (it fits entirely within the L1 Cache)
        //he odds of all 1024 sampled points being correct by "luck" are nearly zero
        const std::size_t stride = std::max<std::size_t>(1, n / 1024); // ~1024 samples 
        const double sum_sample = Validator::checksum_sampled(A, stride);
        do_not_optimize_away(sum_sample);

        // Optional full correctness check for small sizes (keep overhead low)
        if (size_bytes <= 8 * 1024 * 1024) {
            const double full = Validator::checksum_full(A);

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
        const double med = percentile_ns(samples, 50.0);
        const double p95 = percentile_ns(samples, 95.0);

        // Effective bytes per iteration:
        // multiplier (2 or 3) * size_bytes (ONE array size in bytes) GB calculate prone to numeric error
        const double bytes_per_iter = kd.bytes_mult() * static_cast<double>(size_bytes);

        // Effective bandwidth computed from MEDIAN iteration time (stable) for scaling and numeric error 
        const double bw_gb_s = (bytes_per_iter / 1e9) / (med / 1e9);

        // Store point
        BenchmarkResult::Point pt;
        pt.kernel = kd.name();
        pt.bytes = size_bytes;
        pt.median_ns = med;
        pt.p95_ns = p95;
        pt.bandwidth_gb_s = bw_gb_s;
        pt.checksum = sum_sample;

        res.sweep_points.push_back(pt);
    }
}
