#include "video2vec/core/cancellation.hpp"

#include <algorithm>
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
        cancelled_.store(true);
        // The registered callback is delivered exactly once, even if the
        // token was already marked cancelled by an expired deadline. A
        // callback registered later (see on_cancel) still fires once.
        if (callback_ && !callback_fired_) {
            callback_fired_ = true;
            cb = callback_;
        }
    }
    cv_.notify_all();
    if (cb) cb();  // invoked outside the lock so the callback may use the token
}

bool CancellationToken::is_cancelled() const {
    if (cancelled_.load()) return true;
    if (std::chrono::steady_clock::now() >= deadline_) {
        // Deadline expiry marks the token cancelled without invoking the
        // callback; an explicit cancel() afterwards still delivers it.
        cancelled_.store(true);
        return true;
    }
    return false;
}

void CancellationToken::throw_if_cancelled() const {
    if (is_cancelled()) throw std::runtime_error("operation cancelled");
}

void CancellationToken::on_cancel(std::function<void()> callback) {
    std::function<void()> fire_now;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
        if (cancelled_.load() && !callback_fired_ && callback_) {
            callback_fired_ = true;
            fire_now = callback_;
        }
    }
    if (fire_now) fire_now();  // outside the lock: the callback may call back into the token
}

bool CancellationToken::sleep_for(std::chrono::milliseconds duration) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto wake_at = std::chrono::steady_clock::now() + duration;
    if (deadline_ < wake_at) wake_at = deadline_;  // never sleep past the deadline
    cv_.wait_until(lock, wake_at, [this] { return cancelled_.load(); });
    lock.unlock();
    return !is_cancelled();
}

} // namespace video2vec::core
