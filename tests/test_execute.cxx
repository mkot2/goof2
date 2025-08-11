#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

template <typename CellT>
static std::string run(std::string code, std::vector<CellT>& cells, size_t& cellPtr,
                       const std::string& input = "", int eof = 0, bool dynamicSize = true,
                       int* retOut = nullptr) {
    std::istringstream in(input);
    std::ostringstream out;
    auto* cinbuf = std::cin.rdbuf(in.rdbuf());
    auto* coutbuf = std::cout.rdbuf(out.rdbuf());
    std::cin.clear();
    int ret = goof2::execute<CellT>(cells, cellPtr, code, true, eof, dynamicSize, false);
    if (retOut) *retOut = ret;
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
    return out.str();
}

template <typename CellT>
static void test_loops() {
    std::vector<CellT> cells(2, 0);
    size_t ptr = 0;
    run<CellT>("++[>++<-]", cells, ptr);
    assert(cells[0] == 0);
    assert(cells[1] == 4);
}

template <typename CellT>
static void test_io() {
    std::vector<CellT> cells(1, 0);
    size_t ptr = 0;
    std::string out = run<CellT>(",.", cells, ptr, "A");
    assert(out == "A");
    assert(cells[0] == static_cast<CellT>('A'));
}

template <typename CellT>
static void test_wrapping() {
    std::vector<CellT> cells(1, 0);
    size_t ptr = 0;
    run<CellT>("-", cells, ptr);
    assert(cells[0] == std::numeric_limits<CellT>::max());
}

template <typename CellT>
static void test_eof_behavior() {
    std::vector<CellT> cells(1, 42);
    size_t ptr = 0;
    run<CellT>(",", cells, ptr, "", 0);
    assert(cells[0] == 42);
    cells[0] = 42;
    run<CellT>(",", cells, ptr, "", 1);
    assert(cells[0] == 0);
    cells[0] = 42;
    run<CellT>(",", cells, ptr, "", 2);
    assert(cells[0] == std::numeric_limits<CellT>::max());
}

template <typename CellT>
static void test_boundary_checks() {
    {
        std::vector<CellT> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run<CellT>("<", cells, ptr, "", 0, true, &ret);
        assert(ret != 0);
    }
    {
        std::vector<CellT> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run<CellT>(">", cells, ptr, "", 0, false, &ret);
        assert(ret != 0);
    }
    {
        std::vector<CellT> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run<CellT>(">", cells, ptr, "", 0, true, &ret);
        assert(ret == 0);
        assert(ptr == 1);
        assert(cells.size() > 1);
    }
}

template <typename CellT>
static void test_mul_cpy() {
    std::vector<CellT> cells(64, 0);
    size_t ptr = 0;
    if constexpr (sizeof(CellT) <= 2) {
        run<CellT>("++++[->+>+>+>+>+>+>+>+>+>+>+>+>+>+>+>+<<<<<<<<<<<<<<<<]", cells, ptr);
        for (int i = 1; i <= 16; ++i) assert(cells[i] == 4);
    } else if constexpr (sizeof(CellT) == 4) {
        run<CellT>("++++[->+>+>+>+>+>+>+>+<<<<<<<<]", cells, ptr);
        for (int i = 1; i <= 8; ++i) assert(cells[i] == 4);
    } else {
        run<CellT>("++++[->+>+>+>+<<<<]", cells, ptr);
        for (int i = 1; i <= 4; ++i) assert(cells[i] == 4);
    }
    assert(cells[0] == 0);
}

template <typename CellT>
static void test_scan_stride() {
    {
        std::vector<CellT> cells(5, 0);
        cells[4] = 1;
        cells[2] = 1;
        size_t ptr = 4;
        run<CellT>("[<<]", cells, ptr);
        assert(ptr == 0);
    }
    {
        std::vector<CellT> cells(9, 0);
        cells[8] = 1;
        cells[4] = 1;
        size_t ptr = 8;
        run<CellT>("[<<<<]", cells, ptr);
        assert(ptr == 0);
    }
    {
        std::vector<CellT> cells(17, 0);
        cells[16] = 1;
        cells[8] = 1;
        size_t ptr = 16;
        run<CellT>("[<<<<<<<<]", cells, ptr);
        assert(ptr == 0);
    }
}

template <typename CellT>
static void run_tests() {
    test_loops<CellT>();
    test_io<CellT>();
    test_wrapping<CellT>();
    test_eof_behavior<CellT>();
    test_boundary_checks<CellT>();
    test_scan_stride<CellT>();
    test_mul_cpy<CellT>();
}

int main() {
    run_tests<uint8_t>();
    run_tests<uint16_t>();
    run_tests<uint32_t>();
    run_tests<uint64_t>();
    return 0;
}
