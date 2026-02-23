#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <malloc.h> // _aligned_malloc/_aligned_free
#endif

namespace benchmark {

// Simple owning aligned allocation helper.
// - Cross-platform (Windows uses _aligned_malloc)
// - Minimal surface area (data/size/operator[])
// - Moves but does not copy
template <class T>
class AlignedBuffer {
public:
    AlignedBuffer() = default;

    AlignedBuffer(std::size_t n, std::size_t alignment)
        : ptr_(nullptr), n_(n), alignment_(alignment) {
        if (n_ == 0) return;
        if (alignment_ == 0) alignment_ = alignof(T);

        const std::size_t bytes = n_ * sizeof(T);

#if defined(_WIN32)
        ptr_ = static_cast<T*>(_aligned_malloc(bytes, alignment_));
        if (!ptr_) throw std::bad_alloc();
#else
        // posix_memalign requires alignment to be a power-of-two multiple of sizeof(void*)
        void* p = nullptr;
        const int rc = posix_memalign(&p, alignment_, bytes);
        if (rc != 0 || !p) throw std::bad_alloc();
        ptr_ = static_cast<T*>(p);
#endif
    }

    ~AlignedBuffer() { reset(); }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept { *this = std::move(other); }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this == &other) return *this;
        reset();
        ptr_ = other.ptr_;
        n_ = other.n_;
        alignment_ = other.alignment_;
        other.ptr_ = nullptr;
        other.n_ = 0;
        other.alignment_ = 0;
        return *this;
    }

    T* data() { return ptr_; }
    const T* data() const { return ptr_; }

    std::size_t size() const { return n_; }

    T& operator[](std::size_t i) { return ptr_[i]; }
    const T& operator[](std::size_t i) const { return ptr_[i]; }

    std::size_t alignment() const { return alignment_; }

    void reset() {
        if (!ptr_) return;
#if defined(_WIN32)
        _aligned_free(ptr_);
#else
        std::free(ptr_);
#endif
        ptr_ = nullptr;
        n_ = 0;
        alignment_ = 0;
    }

private:
    T* ptr_ = nullptr;
    std::size_t n_ = 0;
    std::size_t alignment_ = 0;
};

} // namespace benchmark
