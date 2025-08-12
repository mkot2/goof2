#include <cassert>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

template <typename CellT>
static std::string run_interp(std::string code, std::vector<CellT>& cells, size_t& cellPtr) {
    std::ostringstream out;
    auto* coutbuf = std::cout.rdbuf(out.rdbuf());
    goof2::execute<CellT>(cells, cellPtr, code, true, 0, true, false);
    std::cout.rdbuf(coutbuf);
    return out.str();
}

template <typename CellT>
static std::string run_jit(std::string code, std::vector<CellT>& cells, size_t& cellPtr) {
    std::ostringstream out;
    auto* coutbuf = std::cout.rdbuf(out.rdbuf());
    goof2::execute_jit<CellT>(cells, cellPtr, code, true, 0, true, false);
    std::cout.rdbuf(coutbuf);
    return out.str();
}

template <typename CellT>
static void compare_program(const std::string& prog) {
    std::vector<CellT> cells1(64, 0), cells2(64, 0);
    size_t p1 = 0, p2 = 0;
    std::string out1 = run_interp<CellT>(prog, cells1, p1);
    std::string out2 = run_jit<CellT>(prog, cells2, p2);
    assert(out1 == out2);
    assert(cells1 == cells2);
    assert(p1 == p2);
}

int main() {
    compare_program<uint8_t>("++[>+<-]>.");
    compare_program<uint8_t>("+[>>>]<+.");
    return 0;
}
