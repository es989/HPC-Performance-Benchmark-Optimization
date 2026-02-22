#include "config.hpp"
#include "results.hpp"
#include "size_parse.hpp"
#include "timer.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::size_t bytes_to_elems(std::uint64_t bytes) {
    return static_cast<std::size_t>(bytes / sizeof(double));
}

// High-arithmetic-intensity per-element kernel.
// Each inner iteration is 1 FMA ~= 2 flops (mul+add).
// Returns checksum to prevent DCE.
double compute_fma_kernel(std::vector<double>& a, int inner) {
    const double alpha = 1.0000000001;
    const double beta = 0.0000000001;

    for (double& v : a) {
        double x = v;
        for (int k = 0; k < inner; ++k) {
            x = std::fma(x, alpha, beta);
        }
        v = x;
    }

    const std::size_t stride = std::max<std::size_t>(1, a.size() / 1024);
    return Validator::checksum_sampled(a, stride);
}

// Similar to FMA, but expressed as mul+add (so compilers may or may not fuse).
double compute_flops_kernel(std::vector<double>& a, int inner) {
    const double alpha = 1.0000000001;
    const double beta = 0.0000000001;

    for (double& v : a) {
        double x = v;
        for (int k = 0; k < inner; ++k) {
            x = x * alpha + beta;
        }
        v = x;
    }

    const std::size_t stride = std::max<std::size_t>(1, a.size() / 1024);
    return Validator::checksum_sampled(a, stride);
}

} // namespace

// Compute microbenchmark runner for --kernel flops / fma.
// Produces one sweep point (bytes = working set size) and fills res.gflops.
void run_compute_bench(const Config& conf, BenchmarkResult& res, const std::string& kind) {
    std::uint64_t size_bytes = 0;
    try {
        size_bytes = parse_size_bytes(conf.size);
    } catch (const std::exception& e) {
        std::cerr << "Error: failed to parse --size '" << conf.size << "': " << e.what() << "\n";
        std::cerr << "Examples: 64MB, 512KiB, 1GiB\n";
        return;
    }

    const std::size_t n = bytes_to_elems(size_bytes);
    if (n == 0) {
        std::cerr << "Error: --size too small (" << size_bytes << " bytes)\n";
        return;
    }

    // Inner work per element. Keep it fixed so GFLOP/s comparisons stay stable.
    // This is intentionally moderate to avoid very long runs on huge sizes.
    const int inner = 64;

    std::vector<double> a(n, 1.0);

    // Optional prefault: touch pages outside measured region.
    if (conf.prefault) {
        const std::size_t page_elems = 4096 / sizeof(double);
        for (std::size_t idx = 0; idx < n; idx += page_elems) {
            volatile double t = a[idx];
            a[idx] = t;
        }
        do_not_optimize_away(a[0]);
    }

    auto one_iter = [&]() -> double {
        if (kind == "fma") return compute_fma_kernel(a, inner);
        return compute_flops_kernel(a, inner);
    };

    // Warmup
    for (int w = 0; w < conf.warmup; ++w) {
        const double chk = one_iter();
        do_not_optimize_away(chk);
    }

    // Measure
    std::vector<long long> samples;
    samples.reserve(conf.iters);
    double checksum = 0.0;

    for (int it = 0; it < conf.iters; ++it) {
        Timer t;
        clobber_memory();
        t.start();

        checksum = one_iter();

        clobber_memory();
        samples.push_back(t.elapsed_ns());
        do_not_optimize_away(checksum);
    }

    if (samples.empty()) return;

    std::sort(samples.begin(), samples.end());
    const long long min_sample = samples.front();
    const long long max_sample = samples.back();

    const double med = percentile_ns(samples, 50.0);
    const double p95 = percentile_ns(samples, 95.0);

    std::vector<double> samples_double(samples.begin(), samples.end());
    const double stddev = compute_stddev(samples_double);

    // FLOP accounting: 1 FMA (or mul+add) per inner step => 2 flops.
    const double flops_per_iter = static_cast<double>(n) * 2.0 * static_cast<double>(inner);
    const double gflops = (med > 0.0) ? (flops_per_iter / med) : 0.0;

    BenchmarkResult::Point pt;
    pt.kernel = kind;
    pt.bytes = static_cast<std::size_t>(size_bytes);
    pt.median_ns = med;
    pt.p95_ns = p95;
    pt.min_ns = static_cast<double>(min_sample);
    pt.max_ns = static_cast<double>(max_sample);
    pt.stddev_ns = stddev;
    pt.bandwidth_gb_s = 0.0;
    pt.checksum = checksum;

    res.sweep_points.push_back(pt);

    res.gflops = gflops;
    res.avg_ns = med;
    res.total_ns = 0;

    std::cout << "[Compute] kind=" << kind << " size=" << size_bytes << " bytes"
              << " median_ns=" << med << " gflops=" << gflops << "\n";
}
