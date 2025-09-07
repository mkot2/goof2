#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace goof2 {

class ThreadPool {
   public:
    explicit ThreadPool(std::size_t count = std::thread::hardware_concurrency()) noexcept;
    ~ThreadPool() noexcept;

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) noexcept -> std::future<std::invoke_result_t<F, Args...>>;

   private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};

inline ThreadPool::ThreadPool(std::size_t count) noexcept {
    if (count == 0) count = 1;
    for (std::size_t i = 0; i < count; ++i) {
        try {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        } catch (...) {
            // swallow exception to preserve noexcept
        }
    }
}

inline ThreadPool::~ThreadPool() noexcept {
    try {
        {
            std::lock_guard lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }
    } catch (...) {
        // swallow exception to preserve noexcept
    }
}

template <class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) noexcept
    -> std::future<std::invoke_result_t<F, Args...>> {
    using Ret = std::invoke_result_t<F, Args...>;
    try {
        auto task = std::make_shared<std::packaged_task<Ret()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto fut = task->get_future();
        {
            std::lock_guard lock(queueMutex);
            tasks.emplace([task]() mutable { (*task)(); });
        }
        condition.notify_one();
        return fut;
    } catch (...) {
        std::promise<Ret> promise;
        promise.set_exception(std::current_exception());
        return promise.get_future();
    }
}

}  // namespace goof2
