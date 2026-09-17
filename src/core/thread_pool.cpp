#include "video2vec/core/thread_pool.hpp"

namespace video2vec::core {

ThreadPool::ThreadPool(size_t num_threads) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::shutdown() {
    // Serialize concurrent shutdown() calls so a second caller waits for the
    // joins instead of returning while workers may still be running.
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
        // stop_ is part of the condition variable's predicate: it must be
        // written under queue_mutex_, otherwise a worker that has evaluated
        // the predicate but not yet blocked misses the notification and
        // join() below hangs forever.
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void ThreadPool::worker_loop(size_t worker_id) {
    (void)worker_id;
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        active_.fetch_add(1);
        try {
            task();
        } catch (...) {
            // Tasks submitted through submit() are packaged_tasks, which
            // capture exceptions into their future. Anything that still
            // escapes is dropped here: rethrowing from a thread entry
            // function would call std::terminate.
        }
        active_.fetch_sub(1);
    }
}

} // namespace video2vec::core
