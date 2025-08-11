#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vm.hxx"

enum class MemoryModel { Contiguous, Paged, Fibonacci, OSBacked };

template <typename CellT, bool Dynamic, bool Term, bool Sparse>
int executeImpl(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, MemoryModel model);

static void run_model(MemoryModel model, bool sparse, size_t expectedSize) {
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string code = ">";
    int ret = sparse ? executeImpl<uint8_t, true, false, true>(cells, ptr, code, true, 0, model)
                     : executeImpl<uint8_t, true, false, false>(cells, ptr, code, true, 0, model);
    (void)ret;
    assert(ret == 0);
    assert(ptr == 1);
    assert(cells.size() == expectedSize);
    (void)expectedSize;
}

int main() {
    run_model(MemoryModel::Contiguous, false, 2);
    run_model(MemoryModel::Paged, false, 2);
    run_model(MemoryModel::Fibonacci, false, 2);
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
    run_model(MemoryModel::OSBacked, false, 2);
#endif
    run_model(MemoryModel::Contiguous, true, 2);
    run_model(MemoryModel::Paged, true, 2);
    run_model(MemoryModel::Fibonacci, true, 2);
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
    run_model(MemoryModel::OSBacked, true, 2);
#endif
    return 0;
}
