#ifndef STREAM_KERNELS_HPP
#define STREAM_KERNELS_HPP

#include <cstddef>

/**
 * STREAM-like operations:
 * - Copy : A[i] = B[i]
 * - Scale: A[i] = s * B[i]
 * - Add  : A[i] = B[i] + C[i]
 * - Triad: A[i] = B[i] + s * C[i]
 *
 * Effective bytes touched per iteration (per element):
 * - Copy/Scale: read 1 input array + write 1 output array => 2 * size_bytes
 * - Add/Triad : read 2 input arrays + write 1 output array => 3 * size_bytes
 */
enum class StreamOp { Copy, Scale, Add, Triad };

inline const char* stream_op_name(StreamOp op) {
    switch (op) {
        case StreamOp::Copy:  return "stream_copy";
        case StreamOp::Scale: return "stream_scale";
        case StreamOp::Add:   return "stream_add";
        case StreamOp::Triad: return "stream_triad";
    }
    return "unknown";
}

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
 * @brief Kernel signature:
 * A is output, B/C inputs, s scalar, n number of elements.
 */
using StreamKernelFn = void(*)(double* A, const double* B, const double* C, double s, std::size_t n);

inline void kernel_copy(double* A, const double* B, const double*, double, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i];
}

inline void kernel_scale(double* A, const double* B, const double*, double s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = s * B[i];
}

inline void kernel_add(double* A, const double* B, const double* C, double, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i] + C[i];
}

inline void kernel_triad(double* A, const double* B, const double* C, double s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) A[i] = B[i] + s * C[i];
}

/**
 * @brief Descriptor so the runner can:
 * - call the kernel
 * - know how many bytes were touched per iteration (2x vs 3x)
 */
struct KernelDesc {
    StreamOp op;
    StreamKernelFn fn;

    const char* name() const { return stream_op_name(op); }
    double bytes_mult() const { return bytes_multiplier(op); }
};

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
