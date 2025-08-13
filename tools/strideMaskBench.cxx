/*
    Benchmark for strideMask32
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

template <unsigned Bytes, unsigned Step>
struct StrideMask32Table {
    static constexpr std::array<uint32_t, Step> masks = []() {
        std::array<uint32_t, Step> arr{};
        constexpr unsigned lanes = 32 / Bytes;
        const uint32_t pattern = (uint32_t(1) << Bytes) - 1u;
        for (unsigned phase = 0; phase < Step; ++phase) {
            uint32_t m = 0;
            for (unsigned i = 0; i < lanes; ++i) {
                if (((i + phase) % Step) == 0) {
                    unsigned bit = i * Bytes;
                    m |= (pattern << bit);
                }
            }
            arr[phase] = m;
        }
        return arr;
    }();
};

template <unsigned Bytes, unsigned Step>
static inline uint32_t strideMask32(unsigned phase) {
    return StrideMask32Table<Bytes, Step>::masks[phase & (Step - 1u)];
}

template <unsigned Bytes>
static uint32_t legacyStrideMask32(unsigned step, unsigned phase) {
    if constexpr (Bytes == 1) {
        if (step == 2) return 0x55555555u << phase;
        if (step == 4) return 0x11111111u << phase;
        if (step == 8) return 0x01010101u << phase;
    }
    uint32_t m = 0;
    constexpr unsigned lanes = 32 / Bytes;
    const uint32_t pattern = (uint32_t(1) << Bytes) - 1u;
    for (unsigned i = 0; i < lanes; ++i) {
        if (((i + phase) % step) == 0) {
            unsigned bit = i * Bytes;
            m |= (pattern << bit);
        }
    }
    return m;
}

int main() {
    constexpr unsigned iterations = 10000000;
    uint32_t sum = 0;
    auto startLegacy = std::chrono::high_resolution_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        sum += legacyStrideMask32<1>(8, i & 7u);
    }
    auto endLegacy = std::chrono::high_resolution_clock::now();
    auto startTable = std::chrono::high_resolution_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        sum += strideMask32<1, 8>(i & 7u);
    }
    auto endTable = std::chrono::high_resolution_clock::now();
    auto legacyNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(endLegacy - startLegacy).count();
    auto tableNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(endTable - startTable).count();
    std::cout << "legacy ns: " << legacyNs << "\n";
    std::cout << "table ns: " << tableNs << "\n";
    std::cout << "checksum: " << sum << "\n";
    return 0;
}
