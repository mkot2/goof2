#include <simde/x86/avx2.h>
#include <simde/x86/avx512.h>
#include <simde/x86/sse2.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

template <typename T>
double bench(size_t n, size_t iterations) {
    std::vector<T> cells(n), prev(n);
    std::vector<size_t> changed;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < n; ++i) {
        cells[i] = static_cast<T>(dist(rng));
        prev[i] = cells[i];
    }
    // introduce differences
    for (size_t i = 0; i < n; i += 16) {
        cells[i] = static_cast<T>(dist(rng));
    }
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t it = 0; it < iterations; ++it) {
        changed.clear();
        size_t limit = std::min(prev.size(), cells.size());
#if defined(SIMDE_X86_AVX512F_NATIVE) || (SIMDE_NATURAL_VECTOR_SIZE >= 512)
        constexpr size_t simdBytes = 64;
#elif defined(SIMDE_X86_AVX2_NATIVE) || (SIMDE_NATURAL_VECTOR_SIZE >= 256)
        constexpr size_t simdBytes = 32;
#else
        constexpr size_t simdBytes = 16;
#endif
        size_t elemsPerVec = simdBytes / sizeof(T);
        size_t vecEnd = (limit / elemsPerVec) * elemsPerVec;
        const uint8_t* curr = reinterpret_cast<const uint8_t*>(cells.data());
        const uint8_t* old = reinterpret_cast<const uint8_t*>(prev.data());
        for (size_t off = 0; off < vecEnd * sizeof(T); off += simdBytes) {
#if defined(SIMDE_X86_AVX512F_NATIVE) || (SIMDE_NATURAL_VECTOR_SIZE >= 512)
            simde__m512i a = simde_mm512_loadu_si512(curr + off);
            simde__m512i b = simde_mm512_loadu_si512(old + off);
            simde__mmask64 mask = simde_mm512_cmpeq_epi8_mask(a, b);
            if (mask != UINT64_MAX) {
                size_t base = off / sizeof(T);
                for (size_t j = 0; j < simdBytes; j += sizeof(T)) {
                    uint64_t lane = (mask >> j) & ((1ull << sizeof(T)) - 1ull);
                    if (lane != ((1ull << sizeof(T)) - 1ull))
                        changed.push_back(base + j / sizeof(T));
                }
            }
#elif defined(SIMDE_X86_AVX2_NATIVE) || (SIMDE_NATURAL_VECTOR_SIZE >= 256)
            auto a = simde_mm256_loadu_si256(reinterpret_cast<const simde__m256i*>(curr + off));
            auto b = simde_mm256_loadu_si256(reinterpret_cast<const simde__m256i*>(old + off));
            auto eq = simde_mm256_cmpeq_epi8(a, b);
            uint32_t mask = simde_mm256_movemask_epi8(eq);
            if (mask != 0xFFFFFFFFu) {
                size_t base = off / sizeof(T);
                for (size_t j = 0; j < simdBytes; j += sizeof(T)) {
                    uint32_t lane = (mask >> j) & ((1u << sizeof(T)) - 1u);
                    if (lane != ((1u << sizeof(T)) - 1u)) changed.push_back(base + j / sizeof(T));
                }
            }
#else
            auto a = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(curr + off));
            auto b = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(old + off));
            auto eq = simde_mm_cmpeq_epi8(a, b);
            uint32_t mask = simde_mm_movemask_epi8(eq);
            if (mask != 0xFFFFu) {
                size_t base = off / sizeof(T);
                for (size_t j = 0; j < simdBytes; j += sizeof(T)) {
                    uint32_t lane = (mask >> j) & ((1u << sizeof(T)) - 1u);
                    if (lane != ((1u << sizeof(T)) - 1u)) changed.push_back(base + j / sizeof(T));
                }
            }
#endif
        }
        for (size_t i = vecEnd; i < limit; ++i) {
            if (cells[i] != prev[i]) changed.push_back(i);
        }
        for (size_t i = limit; i < cells.size(); ++i) {
            if (cells[i]) changed.push_back(i);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count();
    return us / iterations;
}

int main() {
    constexpr size_t n = 1 << 16;
    constexpr size_t iterations = 100;
    std::cout << "u8 " << bench<uint8_t>(n, iterations) << " us\n";
    std::cout << "u16 " << bench<uint16_t>(n, iterations) << " us\n";
    std::cout << "u32 " << bench<uint32_t>(n, iterations) << " us\n";
    std::cout << "u64 " << bench<uint64_t>(n, iterations) << " us\n";
    return 0;
}
