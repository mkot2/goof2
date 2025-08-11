#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vm.hxx"

enum class MemoryModel { Contiguous, Paged, Fibonacci, OSBacked };

template <typename CellT, bool Dynamic, bool Term>
int executeImpl(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, MemoryModel model);

static void run_model(MemoryModel model, size_t expectedSize) {
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string code = ">";
    int ret = executeImpl<uint8_t, true, false>(cells, ptr, code, true, 0, model);
    (void)ret;
    assert(ret == 0);
    assert(ptr == 1);
    assert(cells.size() == expectedSize);
    (void)expectedSize;
}

int main() {
    run_model(MemoryModel::Contiguous, 2);
    run_model(MemoryModel::Paged, 2);
    run_model(MemoryModel::Fibonacci, 2);
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
    run_model(MemoryModel::OSBacked, 2);
#endif
    return 0;
}
