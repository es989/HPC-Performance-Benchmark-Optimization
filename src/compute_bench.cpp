#include "config.hpp"
#include "results.hpp"
#include "size_parse.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "aligned_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

double checksum_sampled_ptr(const double* data, std::size_t n, std::size_t stride) {
    if (!data || n == 0) return 0.0;
    if (stride == 0) stride = 1;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; i += stride) {
        sum += data[i];
    }
    return sum;
}

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

double compute_dot_kernel(const double* x, const double* y, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}

void compute_saxpy_kernel(double a, const double* x, const double* y, double* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a * x[i] + y[i];
    }
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

    // Inner work per element (for flops/fma). Keep it fixed so comparisons stay stable.
    // Intentionally moderate to avoid very long runs on huge sizes.
    const int inner = 64;

    const bool use_aligned = conf.aligned;
    const std::size_t alignment = 64;

    // Allocate inputs based on kernel kind.
    std::vector<double> a;
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> out;

    benchmark::AlignedBuffer<double> a_aligned;
    benchmark::AlignedBuffer<double> x_aligned;
    benchmark::AlignedBuffer<double> y_aligned;
    benchmark::AlignedBuffer<double> out_aligned;

    double* a_ptr = nullptr;
    double* x_ptr = nullptr;
    double* y_ptr = nullptr;
    double* out_ptr = nullptr;

    auto init_arrays = [&]() {
        if (kind == "flops" || kind == "fma") {
            if (use_aligned) {
                a_aligned = benchmark::AlignedBuffer<double>(n, alignment);
                a_ptr = a_aligned.data();
                for (std::size_t i = 0; i < n; ++i) a_ptr[i] = 1.0;
            } else {
                a.assign(n, 1.0);
                a_ptr = a.data();
            }
            return;
        }

        // dot / saxpy use 2 inputs.
        if (use_aligned) {
            x_aligned = benchmark::AlignedBuffer<double>(n, alignment);
            y_aligned = benchmark::AlignedBuffer<double>(n, alignment);
            x_ptr = x_aligned.data();
            y_ptr = y_aligned.data();
            for (std::size_t i = 0; i < n; ++i) {
                x_ptr[i] = 1.0;
                y_ptr[i] = 2.0;
            }
            if (kind == "saxpy") {
                out_aligned = benchmark::AlignedBuffer<double>(n, alignment);
                out_ptr = out_aligned.data();
                for (std::size_t i = 0; i < n; ++i) out_ptr[i] = 0.0;
            }
        } else {
            x.assign(n, 1.0);
            y.assign(n, 2.0);
            x_ptr = x.data();
            y_ptr = y.data();
            if (kind == "saxpy") {
                out.assign(n, 0.0);
                out_ptr = out.data();
            }
        }
    };

    init_arrays();

    // Optional prefault: touch pages outside measured region.
    if (conf.prefault) {
        const std::size_t page_elems = 4096 / sizeof(double);
        auto prefault_ptr = [&](double* p) {
            if (!p) return;
            for (std::size_t idx = 0; idx < n; idx += page_elems) {
                volatile double t = p[idx];
                p[idx] = t;
            }
            do_not_optimize_away(p[0]);
        };

        prefault_ptr(a_ptr);
        prefault_ptr(x_ptr);
        prefault_ptr(y_ptr);
        prefault_ptr(out_ptr);
    }

    auto one_iter = [&]() -> double {
        if (kind == "fma") {
            // fma/flops operate in-place in a std::vector to keep the benchmark simple.
            // For aligned mode we fall back to a manual loop with std::fma.
            if (!use_aligned) return compute_fma_kernel(a, inner);

            const double alpha = 1.0000000001;
            const double beta = 0.0000000001;
            for (std::size_t i = 0; i < n; ++i) {
                double v = a_ptr[i];
                for (int k = 0; k < inner; ++k) {
                    v = std::fma(v, alpha, beta);
                }
                a_ptr[i] = v;
            }
            const std::size_t stride = std::max<std::size_t>(1, n / 1024);
            return checksum_sampled_ptr(a_ptr, n, stride);
        }
        if (kind == "flops") {
            if (!use_aligned) return compute_flops_kernel(a, inner);

            const double alpha = 1.0000000001;
            const double beta = 0.0000000001;
            for (std::size_t i = 0; i < n; ++i) {
                double v = a_ptr[i];
                for (int k = 0; k < inner; ++k) {
                    v = v * alpha + beta;
                }
                a_ptr[i] = v;
            }
            const std::size_t stride = std::max<std::size_t>(1, n / 1024);
            return checksum_sampled_ptr(a_ptr, n, stride);
        }
        if (kind == "dot") {
            return compute_dot_kernel(x_ptr, y_ptr, n);
        }

        // saxpy
        const double a_coeff = 3.0;
        compute_saxpy_kernel(a_coeff, x_ptr, y_ptr, out_ptr, n);
        const std::size_t stride = std::max<std::size_t>(1, n / 1024);
        return checksum_sampled_ptr(out_ptr, n, stride);
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
    double flops_per_iter = 0.0;
    if (kind == "flops" || kind == "fma") {
        flops_per_iter = static_cast<double>(n) * 2.0 * static_cast<double>(inner);
    } else if (kind == "dot" || kind == "saxpy") {
        flops_per_iter = static_cast<double>(n) * 2.0;
    }
    const double gflops = (med > 0.0) ? (flops_per_iter / med) : 0.0;

    // Minimal correctness checks (outside timed region).
    if (kind == "dot") {
        const double expected = 2.0 * static_cast<double>(n);
        if (!Validator::nearly_equal(checksum, expected, 1e-9, 1e-6)) {
            std::cerr << "CRITICAL: dot validation failed: got=" << checksum
                      << " expected=" << expected << "\n";
        }
    } else if (kind == "saxpy") {
        // checksum is sampled; validate sampled expectation.
        const std::size_t stride = std::max<std::size_t>(1, n / 1024);
        const std::size_t samples_n = (n + stride - 1) / stride;
        const double expected = 5.0 * static_cast<double>(samples_n);
        if (!Validator::nearly_equal(checksum, expected, 1e-9, 1e-6)) {
            std::cerr << "CRITICAL: saxpy validation failed (sampled): got=" << checksum
                      << " expected=" << expected << "\n";
        }
    }

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
