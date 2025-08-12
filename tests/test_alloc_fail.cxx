#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

#if !defined(_WIN32)
#include <sys/mman.h>
#endif

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define GOOF2_HAS_OS_VM 1
#else
#define GOOF2_HAS_OS_VM 0
#endif

#if GOOF2_HAS_OS_VM
static void* (*real_alloc)(size_t) = goof2::os_alloc;

static void* always_fail(size_t) {
#ifdef _WIN32
    return nullptr;
#else
    return MAP_FAILED;
#endif
}

static void test_initial_failure() {
    goof2::os_alloc = always_fail;
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string code;
    std::ostringstream err;
    auto* old = std::cerr.rdbuf(err.rdbuf());
    int ret = goof2::execute<uint8_t>(cells, ptr, code, true, 0, true, false,
                                      goof2::MemoryModel::OSBacked);
    std::cerr.rdbuf(old);
    goof2::os_alloc = real_alloc;
    assert(ret == 0);
    (void)ret;
    assert(err.str().find("OS-backed allocation failed") != std::string::npos);
}

static int call_count = 0;
static void* succeed_once_then_fail(size_t bytes) {
    if (call_count++ == 0) return real_alloc(bytes);
#ifdef _WIN32
    return nullptr;
#else
    return MAP_FAILED;
#endif
}

static void test_growth_failure() {
    call_count = 0;
    goof2::os_alloc = succeed_once_then_fail;
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string code = ">";
    std::ostringstream err;
    auto* old = std::cerr.rdbuf(err.rdbuf());
    int ret = goof2::execute<uint8_t>(cells, ptr, code, true, 0, true, false,
                                      goof2::MemoryModel::OSBacked);
    std::cerr.rdbuf(old);
    goof2::os_alloc = real_alloc;
    assert(ret == 0);
    (void)ret;
    assert(ptr == 1);
    assert(cells.size() == 65536);
    assert(err.str().find("OS-backed allocation failed") != std::string::npos);
}
#endif

int main() {
#if GOOF2_HAS_OS_VM
    test_initial_failure();
    test_growth_failure();
#endif
    return 0;
}
