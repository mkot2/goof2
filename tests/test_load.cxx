#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

template <typename CellT>
static std::string runFile(const std::string& path, std::vector<CellT>& cells, size_t& cellPtr,
                           const std::string& input = "") {
    std::ifstream file(path);
    std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::istringstream in(input);
    std::ostringstream out;
    auto* cinbuf = std::cin.rdbuf(in.rdbuf());
    auto* coutbuf = std::cout.rdbuf(out.rdbuf());
    std::cin.clear();
    goof2::execute<CellT>(cells, cellPtr, code, true, 0, true, false);
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
    return out.str();
}

template <typename CellT>
static void test_load_file() {
    const char* fname = "test_program.bf";
    {
        std::ofstream f(fname);
        f << ",.";
    }
    std::vector<CellT> cells(1, 0);
    size_t ptr = 0;
    std::string out = runFile<CellT>(fname, cells, ptr, "A");
    assert(out == "A");
    assert(cells[0] == static_cast<CellT>('A'));
    std::remove(fname);
}

template <typename CellT>
static void run_tests() {
    test_load_file<CellT>();
}

int main() {
    run_tests<uint8_t>();
    run_tests<uint16_t>();
    run_tests<uint32_t>();
    run_tests<uint64_t>();
    return 0;
}
