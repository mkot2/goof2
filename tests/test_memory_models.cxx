#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ml_memory_model.hxx"
#include "vm.hxx"

static void run_model(goof2::MemoryModel model, std::size_t expectedSize) {
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string code = ">";
    int ret = goof2::execute<uint8_t>(cells, ptr, code, true, 0, true, false, model);
    assert(ret == 0);
    assert(ptr == 1);
    if (expectedSize) assert(cells.size() == expectedSize);
}

int main() {
    run_model(goof2::MemoryModel::Contiguous, 2);
    run_model(goof2::MemoryModel::Fibonacci, 2);
    run_model(goof2::MemoryModel::Paged, 65536);
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
    run_model(goof2::MemoryModel::OSBacked, 0);
#endif
    const std::string prog = ">[]";
    goof2::ProgramFeatures f = goof2::extract_features(prog);
    goof2::MemoryModel predicted = goof2::predict_memory_model(f);
    assert(predicted == goof2::MemoryModel::Paged);
    assert(predicted != goof2::MemoryModel::Contiguous);
    return 0;
}
