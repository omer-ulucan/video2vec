#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace video2vec::core {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    // Queues f(args...) and returns its future. Arguments are copied/moved
    // into the task (no std::bind: it decay-copies through std::result_of,
    // which is deprecated and warns under clang).
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<std::decay_t<F>&, std::decay_t<Args>&...>> {
        using ReturnType = std::invoke_result_t<std::decay_t<F>&, std::decay_t<Args>&...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [fn = std::decay_t<F>(std::forward<F>(f)),
             bound = std::make_tuple(std::decay_t<Args>(std::forward<Args>(args))...)]() mutable -> ReturnType {
                return std::apply(fn, bound);
            });
        std::future<ReturnType> result = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) throw std::runtime_error("cannot submit to stopped thread pool");
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return result;
    }
    [[nodiscard]] size_t size() const noexcept { return workers_.size(); }
    [[nodiscard]] size_t active() const noexcept { return active_.load(); }
    // Stops accepting work, runs queued tasks to completion and joins all
    // workers. Idempotent and safe to call concurrently.
    void shutdown();
private:
    void worker_loop(size_t worker_id);
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::mutex shutdown_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;  // guarded by queue_mutex_
    std::atomic<size_t> active_{0};
};

} // namespace video2vec::core
