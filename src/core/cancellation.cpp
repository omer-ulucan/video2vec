#include "video2vec/core/cancellation.hpp"

#include <stdexcept>

namespace video2vec::core {

CancellationToken::CancellationToken()
    : deadline_(std::chrono::steady_clock::time_point::max()) {}

CancellationToken::CancellationToken(std::chrono::milliseconds timeout)
    : deadline_(std::chrono::steady_clock::now() + timeout) {}

void CancellationToken::cancel() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_.exchange(true)) return;
        cb = callback_;
    }
    cv_.notify_all();
    if (cb) cb();
}

bool CancellationToken::is_cancelled() const {
    if (cancelled_.load()) return true;
    if (std::chrono::steady_clock::now() >= deadline_) {
        // Deadline expired: atomically mark cancelled without invoking callbacks.
        // Callbacks are only invoked for explicit cancellation.
        cancelled_.store(true);
        return true;
    }
    return false;
}

void CancellationToken::throw_if_cancelled() const {
    if (is_cancelled()) throw std::runtime_error("operation cancelled");
}

void CancellationToken::on_cancel(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
    if (cancelled_.load() && callback_) callback_();
}

bool CancellationToken::sleep_for(std::chrono::milliseconds duration) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return !cv_.wait_for(lock, duration, [this] { return cancelled_.load(); });
}

} // namespace video2vec::core
