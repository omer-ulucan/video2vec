#include "video2vec/core/memory.hpp"

#include <cstdlib>

namespace video2vec::core {

void MemoryTracker::allocate(void* ptr, size_t bytes, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    allocations_[ptr] = {bytes, tag};
    total_allocated_ += bytes;
    total_active_ += bytes;
    if (total_active_ > peak_usage_) peak_usage_ = total_active_;
}

void MemoryTracker::deallocate(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        total_active_ -= it->second.first;
        allocations_.erase(it);
    }
}

size_t MemoryTracker::total_allocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_allocated_;
}

size_t MemoryTracker::total_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_active_;
}

size_t MemoryTracker::peak_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_usage_;
}

void MemoryTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    allocations_.clear();
    total_allocated_ = 0;
    total_active_ = 0;
    peak_usage_ = 0;
}

MemoryTracker& MemoryTracker::instance() {
    static MemoryTracker tracker;
    return tracker;
}

} // namespace video2vec::core
