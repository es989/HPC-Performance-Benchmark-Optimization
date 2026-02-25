#ifndef STREAM_KERNELS_HPP
#define STREAM_KERNELS_HPP

#include <cstddef>

/**
 * @brief STREAM-like operations for memory bandwidth measurement.
 *
 * These kernels are designed to stress the memory subsystem by performing
 * simple arithmetic operations on large arrays. They are based on the
 * industry-standard STREAM benchmark.
 *
 * - Copy : A[i] = B[i] (Reads B, Writes A)
 * - Scale: A[i] = s * B[i] (Reads B, Writes A)
 * - Add  : A[i] = B[i] + C[i] (Reads B and C, Writes A)
 * - Triad: A[i] = B[i] + s * C[i] (Reads B and C, Writes A)
 *
 * Effective bytes touched per iteration (per element):
 * - Copy/Scale: read 1 input array + write 1 output array => 2 * size_bytes
 * - Add/Triad : read 2 input arrays + write 1 output array => 3 * size_bytes
 */
enum class StreamOp { Copy, Scale, Add, Triad };

/**
 * @brief Get the string representation of a StreamOp.
 * Useful for logging and JSON output.
 */
inline const char* stream_op_name(StreamOp op) {
    switch (op) {
        case StreamOp::Copy:  return "stream_copy";
        case StreamOp::Scale: return "stream_scale";
        case StreamOp::Add:   return "stream_add";
        case StreamOp::Triad: return "stream_triad";
    }
    return "unknown";
}

/**
 * @brief Get the byte multiplier for a given StreamOp.
 * This is crucial for calculating the effective memory bandwidth.
 * Bandwidth = (Elements * sizeof(double) * multiplier) / Time
 */
inline double bytes_multiplier(StreamOp op) {
    switch (op) {
        case StreamOp::Copy:  return 2.0;
        case StreamOp::Scale: return 2.0;
        case StreamOp::Add:   return 3.0;
        case StreamOp::Triad: return 3.0;
    }
    return 0.0;
}

/**
 * @brief Function pointer signature for STREAM kernels.
 * @param A Output array.
 * @param B First input array.
 * @param C Second input array (can be null for Copy/Scale).
 * @param s Scalar value (used in Scale/Triad).
 * @param n Number of elements to process.
 */
using StreamKernelFn = void(*)(double* A, const double* B, const double* C, double s, std::size_t n);

/**
 * @brief Copy kernel: A[i] = B[i]
 * Measures pure memory read/write bandwidth without arithmetic bottlenecks.
 */
inline void kernel_copy(double* A, const double* B, const double*, double, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i];
}

/**
 * @brief Scale kernel: A[i] = s * B[i]
 * Adds a simple scalar multiplication to the memory copy.
 */
inline void kernel_scale(double* A, const double* B, const double*, double s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = s * B[i];
}

/**
 * @brief Add kernel: A[i] = B[i] + C[i]
 * Measures bandwidth when reading from two separate memory streams and writing to a third.
 */
inline void kernel_add(double* A, const double* B, const double* C, double, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i] + C[i];
}

/**
 * @brief Triad kernel: A[i] = B[i] + s * C[i]
 * The most complex STREAM kernel, combining FMA (Fused Multiply-Add) with 3 memory streams.
 * Often used as the primary metric for system memory bandwidth.
 */
inline void kernel_triad(double* A, const double* B, const double* C, double s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i] + s * C[i];
}

/**
 * @brief Descriptor struct to bundle a kernel function with its metadata.
 * Allows the runner to dynamically dispatch kernels and calculate bandwidth correctly.
 */
struct KernelDesc {
    StreamOp op;
    StreamKernelFn fn;

    const char* name() const { return stream_op_name(op); }
    double bytes_mult() const { return bytes_multiplier(op); }
};

/**
 * @brief Factory function to create a KernelDesc based on the requested StreamOp.
 */
inline KernelDesc make_stream_desc(StreamOp op) {
    switch (op) {
        case StreamOp::Copy:  return {op, &kernel_copy};
        case StreamOp::Scale: return {op, &kernel_scale};
        case StreamOp::Add:   return {op, &kernel_add};
        case StreamOp::Triad: return {op, &kernel_triad};
    }
    return {StreamOp::Copy, &kernel_copy}; // fallback
}

#endif // STREAM_KERNELS_HPP
