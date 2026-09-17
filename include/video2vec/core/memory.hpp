#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace video2vec::core {

namespace detail {
// Portable aligned allocation. std::aligned_alloc is unavailable in the MSVC
// CRT, and memory from _aligned_malloc must be released with _aligned_free.
inline void* aligned_malloc(size_t alignment, size_t bytes) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, alignment);
#else
    return std::aligned_alloc(alignment, bytes);
#endif
}
inline void aligned_free(void* ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
} // namespace detail

template <typename T>
class AlignedBuffer {
public:
    // alignment must be a power of two. The allocation size is rounded up to
    // a multiple of the alignment, as aligned_alloc requires.
    AlignedBuffer(size_t count, size_t alignment = 64)
        : count_(count), alignment_(alignment) {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            throw std::invalid_argument("AlignedBuffer alignment must be a power of two");
        }
        if (count > std::numeric_limits<size_t>::max() / sizeof(T)) throw std::bad_alloc();
        const size_t bytes = count * sizeof(T);
        if (bytes > std::numeric_limits<size_t>::max() - (alignment - 1)) throw std::bad_alloc();
        size_t rounded = (bytes + alignment - 1) / alignment * alignment;
        if (rounded == 0) rounded = alignment;  // zero-byte requests may legally return null
        data_ = static_cast<T*>(detail::aligned_malloc(alignment, rounded));
        if (!data_) throw std::bad_alloc();
    }
    ~AlignedBuffer() { detail::aligned_free(data_); }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data_(other.data_), count_(other.count_), alignment_(other.alignment_) {
        other.data_ = nullptr;
        other.count_ = 0;
    }
    // Releases the current buffer and takes ownership of other's; the
    // moved-from buffer is left empty (same state as after move construction).
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            detail::aligned_free(data_);
            data_ = other.data_;
            count_ = other.count_;
            alignment_ = other.alignment_;
            other.data_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }
    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] size_t alignment() const noexcept { return alignment_; }
    [[nodiscard]] size_t byte_size() const noexcept { return count_ * sizeof(T); }
    [[nodiscard]] std::span<T> span() { return std::span<T>(data_, count_); }
    [[nodiscard]] std::span<const T> span() const { return std::span<const T>(data_, count_); }
private:
    T* data_ = nullptr;
    size_t count_ = 0;
    size_t alignment_ = 64;
};

class MemoryTracker {
public:
    static MemoryTracker& instance();
    void allocate(void* ptr, size_t bytes, const std::string& tag);
    void deallocate(void* ptr);
    [[nodiscard]] size_t total_allocated() const;
    [[nodiscard]] size_t total_active() const;
    [[nodiscard]] size_t peak_usage() const;
    void reset();
private:
    MemoryTracker() = default;
    mutable std::mutex mutex_;
    std::map<void*, std::pair<size_t, std::string>> allocations_;
    size_t total_allocated_ = 0;
    size_t total_active_ = 0;
    size_t peak_usage_ = 0;
};

} // namespace video2vec::core
