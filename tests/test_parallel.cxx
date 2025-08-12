#include <cassert>
#include <chrono>
#include <future>
#include <vector>

#include "parallel.hxx"
#include "vm.hxx"

static void testPerformance() {
    const std::size_t n = 1u << 20;  // large enough to benefit from parallelism
    std::vector<int> data(n);
    auto startSeq = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < n; ++i) data[i] = 1;
    auto seqDur = std::chrono::high_resolution_clock::now() - startSeq;
    auto startPar = std::chrono::high_resolution_clock::now();
    goof2::parallelFor(0, n, [&](std::size_t i) { data[i] = 2; });
    auto parDur = std::chrono::high_resolution_clock::now() - startPar;
    assert(parDur <= seqDur);
    for (int v : data) {
        assert(v == 2);
        (void)v;
    }
}

static void testInterpreterParallel() {
    auto worker = []() {
        std::vector<std::uint8_t> cells(2, 0);
        size_t ptr = 0;
        std::string code = "++[>++<-]";
        goof2::execute<std::uint8_t>(cells, ptr, code, true, 0, false, false);
        return cells[1];
    };
    auto f1 = std::async(std::launch::async, worker);
    auto f2 = std::async(std::launch::async, worker);
    assert(f1.get() == 4);
    assert(f2.get() == 4);
}

int main() {
    testPerformance();
    testInterpreterParallel();
    return 0;
}
