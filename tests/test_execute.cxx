#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

static std::string run(std::string code, std::vector<uint8_t>& cells, size_t& cellPtr,
                       const std::string& input = "", int eof = 0, bool dynamicSize = true,
                       int* retOut = nullptr) {
    std::istringstream in(input);
    std::ostringstream out;
    auto* cinbuf = std::cin.rdbuf(in.rdbuf());
    auto* coutbuf = std::cout.rdbuf(out.rdbuf());
    std::cin.clear();
    int ret = goof2::execute<uint8_t>(cells, cellPtr, code, true, eof, dynamicSize, false);
    if (retOut) *retOut = ret;
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
    return out.str();
}

static void test_loops() {
    std::vector<uint8_t> cells(2, 0);
    size_t ptr = 0;
    run("++[>++<-]", cells, ptr);
    assert(cells[0] == 0);
    assert(cells[1] == 4);
}

static void test_io() {
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string out = run(",.", cells, ptr, "A");
    assert(out == "A");
    assert(cells[0] == static_cast<uint8_t>('A'));
}

static void test_wrapping() {
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    run("-", cells, ptr);
    assert(cells[0] == 255);
}

static void test_eof_behavior() {
    std::vector<uint8_t> cells(1, 42);
    size_t ptr = 0;
    run(",", cells, ptr, "", 0);
    assert(cells[0] == 42);
    cells[0] = 42;
    run(",", cells, ptr, "", 1);
    assert(cells[0] == 0);
    cells[0] = 42;
    run(",", cells, ptr, "", 2);
    assert(cells[0] == 255);
}

static void test_boundary_checks() {
    {
        std::vector<uint8_t> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run("<", cells, ptr, "", 0, true, &ret);
        assert(ret != 0);
    }
    {
        std::vector<uint8_t> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run(">", cells, ptr, "", 0, false, &ret);
        assert(ret != 0);
    }
    {
        std::vector<uint8_t> cells(1, 0);
        size_t ptr = 0;
        int ret;
        run(">", cells, ptr, "", 0, true, &ret);
        assert(ret == 0);
        assert(ptr == 1);
        assert(cells.size() > 1);
    }
}

int main() {
    test_loops();
    test_io();
    test_wrapping();
    test_eof_behavior();
    test_boundary_checks();
    return 0;
}
