#ifndef UTILS_HPP
#define UTILS_HPP

#include <cstddef>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * @brief Prevent the compiler from optimizing away a value.
 *
 * This is a COMPILER barrier (not a CPU fence).
 * It forces the compiler to treat the value as "observed/used".
 */
template <typename T>
inline void do_not_optimize_away(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
    // "g" constraint: value can be in register or memory; "memory" clobber blocks reordering.
    asm volatile("" : : "g"(value) : "memory");
#else
    // MSVC fallback: store into volatile to force a side-effect.
    volatile T sink = value;
    (void)sink;
#endif
}

/**
 * @brief Prevent compiler reordering of memory operations across this point.
 *
 * Helpful around timing boundaries to reduce code motion across start/stop.
 */
inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    // MSVC: no perfect standard equivalent; this is left empty intentionally.
    // If needed, keep timing code in separate translation units and avoid inlining.
#endif
}

/**
 * @brief Compute standard deviation for a vector of samples.
 *
 * @param samples Vector of timing samples (in nanoseconds).
 * @return Standard deviation in the same units as input samples.
 */
inline double compute_stddev(const std::vector<double>& samples) {
    if (samples.size() < 2) return 0.0;

    // Compute mean
    double sum = 0.0;
    for (double s : samples) sum += s;
    const double mean = sum / samples.size();

    // Compute variance
    double variance = 0.0;
    for (double s : samples) {
        const double diff = s - mean;
        variance += diff * diff;
    }
    variance /= samples.size();

    return std::sqrt(variance);
}

/**
 * @brief Compute percentile for timing samples (ns).
 *
 * @param samples Copy of vector (we sort it).
 * @param p Percentile in [0,100], e.g. 50 for median, 95 for p95.
 */
inline double percentile_ns(std::vector<long long> samples, double p) {
    if (samples.empty()) return 0.0;

    std::sort(samples.begin(), samples.end());

    const double idx = (p / 100.0) * (samples.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
    const double frac = idx - static_cast<double>(lo);

    return samples[lo] * (1.0 - frac) + samples[hi] * frac;
}

/**
 * @brief Validation utilities:
 * - checksums (full and sampled)
 * - floating-point comparison with abs+rel tolerance
 */
struct Validator {
    /**
     * @brief Full checksum (O(n)) — use outside timed region.
     */
    static double checksum_full(const std::vector<double>& data) {
        double sum = 0.0;
        for (double v : data) sum += v;
        return sum;
    }

    /**
     * @brief Sampled checksum — cheaper than full sum for huge arrays.
     * @param stride sample every `stride` elements.
     */
    static double checksum_sampled(const std::vector<double>& data, std::size_t stride) {
        if (data.empty()) return 0.0;
        if (stride == 0) stride = 1;

        double sum = 0.0;
        for (std::size_t i = 0; i < data.size(); i += stride) {
            sum += data[i];
        }
        return sum;
    }

    /**
     * @brief |a-b| <= atol + rtol*|b|
     */
    static bool nearly_equal(double a, double b, double rtol = 1e-9, double atol = 1e-9) {
        const double diff = std::fabs(a - b);
        return diff <= (atol + rtol * std::fabs(b));
    }
};

#endif // UTILS_HPP
