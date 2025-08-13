#pragma once
#include <algorithm>
#include <future>
#include <thread>
#include <vector>

namespace goof2 {

// Simple parallel for helper using std::async to dispatch work in blocks.
template <typename Func>
void parallelFor(std::size_t begin, std::size_t end, Func func) {
    const std::size_t length = end - begin;
    const unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());
    // For small workloads the overhead of launching threads outweighs any benefit,
    // so fall back to a simple sequential loop when the range is tiny or only one
    // hardware thread is available.
    if (length < 1024 || numThreads == 1) {
        for (std::size_t i = begin; i < end; ++i) func(i);
        return;
    }

    const std::size_t block = (length + numThreads - 1) / numThreads;
    std::vector<std::future<void>> workers;
    for (std::size_t b = begin; b < end; b += block) {
        const std::size_t bEnd = std::min(b + block, end);
        workers.emplace_back(std::async(std::launch::async, [b, bEnd, &func]() {
            for (std::size_t i = b; i < bEnd; ++i) func(i);
        }));
    }
    for (auto& w : workers) w.get();
}

}  // namespace goof2
