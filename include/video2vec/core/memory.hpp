#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace video2vec::core {

template <typename T>
class AlignedBuffer {
public:
    AlignedBuffer(size_t count, size_t alignment = 64)
        : count_(count), alignment_(alignment) {
        data_ = static_cast<T*>(aligned_alloc(alignment, count * sizeof(T)));
        if (!data_) throw std::bad_alloc();
    }
    ~AlignedBuffer() { free(data_); }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data_(other.data_), count_(other.count_), alignment_(other.alignment_) {
        other.data_ = nullptr; other.count_ = 0;
    }
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            std::swap(data_, other.data_);
            std::swap(count_, other.count_);
            std::swap(alignment_, other.alignment_);
        }
        return *this;
    }
    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return count_; }
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
