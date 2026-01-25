//if file is included twice it wont throw redefinition
#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>
#include <cstddef> // Required for size_t

/**
 *   @brief High-resolution monotonic timer for HPC microbenchmarking.
 * * Uses std::chrono::steady_clock to ensure measurements are not affected
 * by system clock adjustments (wall clock changes).
 */
class Timer {
    // Alias for the monotonic clock 
    using clock = std::chrono::steady_clock;

    // Stores the time point when start() was called
    clock::time_point start_point{};

public:
    /**
     *  Records the current time point.
     */
    void start() {
        start_point = clock::now();
    }

    /**
     * @brief Calculates the time elapsed since start() in nanoseconds.
     * @return Elapsed time as long long.
     * Note: 'const' ensures this method doesn't modify the Timer state.
     */
    long long elapsed_ns() const {
        const auto end_point = clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end_point - start_point).count();
    }

    /**
     * @brief Static helper to calculate average cost per operation.
     * @param total_ns Total time recorded for all iterations.
     * @param iterations Number of times the operation was performed.
     * @return Average nanoseconds per operation (ns/op).
     * Includes a guard against division by zero.
     */
    static double ns_per_op(long long total_ns, std::size_t iterations) {
        return iterations ? (static_cast<double>(total_ns) / iterations) : 0.0;
    }
};

#endif