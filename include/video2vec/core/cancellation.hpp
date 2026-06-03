#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace video2vec::core {

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
    bool sleep_for(std::chrono::milliseconds duration) const;
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point deadline_;
    std::function<void()> callback_;
};

} // namespace video2vec::core
