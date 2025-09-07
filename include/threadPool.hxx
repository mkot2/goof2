#pragma once

#include <condition_variable>
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
    explicit ThreadPool(std::size_t count = std::thread::hardware_concurrency());
    ~ThreadPool();

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

   private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};

inline ThreadPool::ThreadPool(std::size_t count) {
    if (count == 0) count = 1;
    for (std::size_t i = 0; i < count; ++i) {
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
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
}

template <class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using Ret = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<Ret()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    auto fut = task->get_future();
    {
        std::lock_guard lock(queueMutex);
        tasks.emplace([task]() mutable { (*task)(); });
    }
    condition.notify_one();
    return fut;
}

}  // namespace goof2
