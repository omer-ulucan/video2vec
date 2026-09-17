#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace video2vec::core {

// Cooperative cancellation with an optional deadline.
//  - cancel() marks the token cancelled, wakes sleepers and delivers the
//    on_cancel callback exactly once (outside the token's lock).
//  - An expired deadline makes is_cancelled() true and interrupts
//    sleep_for(), but does not invoke the callback by itself.
//  - on_cancel() on an already-cancelled token invokes the callback
//    immediately (once).
class CancellationToken {
public:
    CancellationToken();
    explicit CancellationToken(std::chrono::milliseconds timeout);
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    void cancel();
    [[nodiscard]] bool is_cancelled() const;
    void throw_if_cancelled() const;
    void on_cancel(std::function<void()> callback);
    // Sleeps for `duration` or until cancelled/deadline, whichever comes
    // first. Returns true if the full duration elapsed without cancellation.
    bool sleep_for(std::chrono::milliseconds duration) const;
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point deadline_;
    std::function<void()> callback_;  // guarded by mutex_
    bool callback_fired_ = false;      // guarded by mutex_
};

} // namespace video2vec::core
