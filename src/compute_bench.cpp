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

/**
 * @brief Calculate a sampled checksum of an array.
 *
 * This is used to verify the correctness of the compute kernels without
 * adding significant overhead to the measurement. By sampling every `stride`
 * elements, we ensure the compiler cannot optimize away the computation
 * (Dead Code Elimination) because the result is "used" by the checksum.
 *
 * @param data Pointer to the array data.
 * @param n Total number of elements in the array.
 * @param stride The step size for sampling (e.g., every 1024th element).
 * @return The sum of the sampled elements.
 */
double checksum_sampled_ptr(const double* data, std::size_t n, std::size_t stride) {
    if (!data || n == 0) return 0.0;
    if (stride == 0) stride = 1;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; i += stride) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief Convert a size in bytes to the number of `double` elements.
 *
 * @param bytes The total size in bytes.
 * @return The number of `double` elements (bytes / 8).
 */
std::size_t bytes_to_elems(std::uint64_t bytes) {
    return static_cast<std::size_t>(bytes / sizeof(double));
}

/**
 * @brief High-arithmetic-intensity per-element kernel using FMA.
 *
 * This kernel is designed to test the CPU's floating-point throughput.
 * It performs a tight loop of Fused Multiply-Add (FMA) operations on each
 * element of the array. FMA computes `(x * alpha) + beta` in a single
 * instruction, which is highly optimized on modern CPUs (e.g., AVX2/AVX-512).
 *
 * Each inner iteration is 1 FMA ~= 2 flops (multiply + add).
 *
 * @param a The array of elements to process.
 * @param inner The number of FMA operations to perform per element.
 * @return A sampled checksum of the array to prevent Dead Code Elimination.
 */
double compute_fma_kernel(std::vector<double>& a, int inner) {
    const double alpha = 1.0000000001;
    const double beta = 0.0000000001;

    for (double& v : a) {
        double x = v;
        for (int k = 0; k < inner; ++k) {
            // std::fma maps to hardware FMA instructions if available.
            x = std::fma(x, alpha, beta);
        }
        v = x;
    }

    const std::size_t stride = std::max<std::size_t>(1, a.size() / 1024);
    return Validator::checksum_sampled(a, stride);
}

/**
 * @brief High-arithmetic-intensity kernel using explicit multiply and add.
 *
 * Similar to `compute_fma_kernel`, but expressed as separate multiplication
 * and addition operations. Depending on the compiler and optimization flags
 * (e.g., `-ffp-contract=fast`), the compiler may or may not fuse these into
 * a single FMA instruction. This is useful for testing compiler behavior.
 *
 * @param a The array of elements to process.
 * @param inner The number of operations to perform per element.
 * @return A sampled checksum of the array.
 */
double compute_flops_kernel(std::vector<double>& a, int inner) {
    const double alpha = 1.0000000001;
    const double beta = 0.0000000001;

    for (double& v : a) {
        double x = v;
        for (int k = 0; k < inner; ++k) {
            // Explicit mul + add. May be fused by the compiler.
            x = x * alpha + beta;
        }
        v = x;
    }

    const std::size_t stride = std::max<std::size_t>(1, a.size() / 1024);
    return Validator::checksum_sampled(a, stride);
}

/**
 * @brief Standard Dot Product kernel.
 *
 * Computes the dot product of two vectors: sum(x[i] * y[i]).
 * This is a fundamental BLAS Level 1 operation, heavily reliant on memory
 * bandwidth but also capable of utilizing SIMD instructions (AVX/AVX2) for
 * the multiplication and reduction.
 *
 * @param x Pointer to the first vector.
 * @param y Pointer to the second vector.
 * @param n Number of elements in the vectors.
 * @return The scalar dot product result.
 */
double compute_dot_kernel(const double* x, const double* y, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}

/**
 * @brief Standard SAXPY (Single-precision A*X Plus Y) kernel.
 *
 * Computes `out[i] = a * x[i] + y[i]`.
 * Another fundamental BLAS Level 1 operation. It reads two arrays and writes
 * to a third, making it very similar to the STREAM Triad benchmark, but
 * typically used in a compute context to measure vectorization efficiency.
 *
 * @param a The scalar multiplier.
 * @param x Pointer to the first input vector.
 * @param y Pointer to the second input vector.
 * @param out Pointer to the output vector.
 * @param n Number of elements to process.
 */
void compute_saxpy_kernel(double a, const double* x, const double* y, double* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a * x[i] + y[i];
    }
}

} // namespace

/**
 * @brief Compute microbenchmark runner for --kernel flops / fma.
 *
 * This function sets up and runs the compute-bound benchmarks. Unlike the
 * STREAM sweep which tests multiple sizes, this runs a single size (specified
 * by `--size`) and focuses on measuring GFLOP/s (Giga Floating-Point Operations
 * Per Second).
 *
 * It handles memory allocation, warmup iterations, and the timed measurement loop.
 *
 * @param conf The parsed configuration (size, warmup, iters, etc.).
 * @param res The result object to populate with performance metrics.
 * @param kind The specific compute kernel to run ("flops" or "fma").
 */
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

    std::vector<double> samples_double;
    samples_double.reserve(samples.size());
    for (auto ns : samples) {
        samples_double.push_back(static_cast<double>(ns));
    }
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
