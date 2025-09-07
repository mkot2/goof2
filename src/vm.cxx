/*
    Goof2 - An optimizing brainfuck VM
    VM implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "vm.hxx"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

using SvMatch = std::match_results<std::string_view::const_iterator>;

namespace {
constexpr std::size_t kCacheExpectedEntries = 64;
constexpr std::size_t kCacheMaxEntries = 64;
std::uint64_t cacheCounter = 0;
}  // namespace

inline int32_t fold(std::string_view code, size_t& i, char match) {
    int32_t count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) {
        ++i;
        ++count;
    }
    return count;
}

inline std::string processBalanced(std::string_view s, char no1, char no2) {
    const auto total = std::ranges::count(s, no1) - std::ranges::count(s, no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

template <typename Callback>
inline void regexReplaceInplace(std::string& str, const std::regex& re, Callback cb,
                                std::function<size_t(const SvMatch&)> estimate = {}) {
    std::string_view sv{str};
    using Iterator = std::string_view::const_iterator;
    ptrdiff_t predicted = static_cast<ptrdiff_t>(str.size());

    if (estimate) {
        for (std::regex_iterator<Iterator> it(sv.begin(), sv.end(), re), end; it != end; ++it) {
            predicted +=
                static_cast<ptrdiff_t>(estimate(*it)) - static_cast<ptrdiff_t>((*it)[0].length());
        }
        if (predicted < 0) predicted = 0;
    }

    std::string result;
    if (estimate) {
        result.reserve(static_cast<size_t>(predicted));
    } else {
        result.reserve(str.size());
    }

    Iterator last = sv.begin();
    for (std::regex_iterator<Iterator> it(sv.begin(), sv.end(), re), end; it != end; ++it) {
        const SvMatch& m = *it;
        result.append(last, m[0].first);
        result += cb(m);
        last = m[0].second;
    }
    result.append(last, sv.end());
    str = std::move(result);
}

struct RegexReplacement {
    size_t start;
    size_t end;
    std::string text;
    std::function<void()> sideEffect;
};

template <typename Callback>
std::vector<RegexReplacement> regexCollect(const std::string& str, const std::regex& re,
                                           Callback cb) {
    std::vector<RegexReplacement> reps;
    std::string_view sv{str};
    using Iterator = std::string_view::const_iterator;
    for (std::regex_iterator<Iterator> it(sv.begin(), sv.end(), re), end; it != end; ++it) {
        const SvMatch& m = *it;
        auto [rep, eff] = cb(m);
        reps.push_back({static_cast<size_t>(m[0].first - sv.begin()),
                        static_cast<size_t>(m[0].second - sv.begin()), std::move(rep),
                        std::move(eff)});
    }
    return reps;
}

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define GOOF2_HAS_OS_VM 1
#else
#define GOOF2_HAS_OS_VM 0
#endif

#if GOOF2_HAS_OS_VM
namespace goof2 {
#ifdef _WIN32
static void* default_os_alloc(size_t bytes) {
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
static void default_os_free(void* ptr, size_t) { VirtualFree(ptr, 0, MEM_RELEASE); }
#else
static void* default_os_alloc(size_t bytes) {
    return mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
static void default_os_free(void* ptr, size_t bytes) { munmap(ptr, bytes); }
#endif
void* (*os_alloc)(size_t) = default_os_alloc;
void (*os_free)(void*, size_t) = default_os_free;
}  // namespace goof2
#endif

using goof2::MemoryModel;

namespace goof2::vmRegex {
using namespace std::regex_constants;
static const std::regex nonInstructionRe(R"([^+\-<>\.,\]\[])", optimize | nosubs);
static const std::regex balanceSeqRe(R"([+-]{2,}|[><]{2,})", optimize | nosubs);
static const std::regex clearLoopRe(R"([+-]*\[[+-]+\](?:\[[+-]+\])*)", optimize | nosubs);
static const std::regex scanLoopClrRe(R"(\[-[<>]+\]|\[[<>]\[-\]\])", optimize | nosubs);
static const std::regex scanLoopRe(R"(\[[<>]+\])", optimize | nosubs);
static const std::regex commaTrimRe(R"([+\-C]+,)", optimize | nosubs);
static const std::regex clearThenSetRe(R"(C([+-]+))", optimize);
static const std::regex copyLoopRe(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])",
                                   optimize);
static const std::regex leadingSetRe(R"((?:^|([RL\]]))C*([\+\-]+))", optimize);
static const std::regex copyLoopInnerRe(R"((?:<+|>+)[+-]+)", optimize | nosubs);
static const std::regex clearSeqRe(R"(C{2,})", optimize | nosubs);
}  // namespace goof2::vmRegex

#include "simde/x86/avx2.h"
#include "simde/x86/avx512.h"

#define TZCNT32(x) __builtin_ctz((unsigned)(x))
#define LZCNT32(x) __builtin_clz((unsigned)(x))
#define TZCNT64(x) __builtin_ctzll((unsigned long long)(x))
#define LZCNT64(x) __builtin_clzll((unsigned long long)(x))

static inline bool runtimeHasAvx512() {
#if defined(SIMDE_ARCH_X86) && !defined(__EMSCRIPTEN__)
    static int cached = -1;
    if (cached < 0) cached = __builtin_cpu_supports("avx512f");
    return cached;
#else
    return false;
#endif
}

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

template <unsigned Bytes, unsigned Step>
struct StrideMask16Table {
    static constexpr std::array<uint16_t, Step> masks = []() {
        std::array<uint16_t, Step> arr{};
        constexpr unsigned lanes = 16 / Bytes;
        const uint16_t pattern = (uint16_t(1) << Bytes) - uint16_t(1);
        for (unsigned phase = 0; phase < Step; ++phase) {
            uint16_t m = 0;
            for (unsigned i = 0; i < lanes; ++i) {
                if (((i + phase) % Step) == 0) {
                    unsigned bit = i * Bytes;
                    m |= static_cast<uint16_t>(pattern << bit);
                }
            }
            arr[phase] = m;
        }
        return arr;
    }();
};

template <unsigned Bytes, unsigned Step>
struct StrideMask64Table {
    static constexpr std::array<uint64_t, Step> masks = []() {
        std::array<uint64_t, Step> arr{};
        constexpr unsigned lanes = 64 / Bytes;
        for (unsigned phase = 0; phase < Step; ++phase) {
            uint64_t m = 0;
            for (unsigned i = 0; i < lanes; ++i) {
                if (((i + phase) % Step) == 0) {
                    m |= (uint64_t(1) << i);
                }
            }
            arr[phase] = m;
        }
        return arr;
    }();
};

template <unsigned Bytes>
static inline uint16_t strideMask16(unsigned step, unsigned phase) {
    switch (step) {
        case 2:
            return StrideMask16Table<Bytes, 2>::masks[phase & 1u];
        case 4:
            return StrideMask16Table<Bytes, 4>::masks[phase & 3u];
        case 8:
            return StrideMask16Table<Bytes, 8>::masks[phase & 7u];
        default: {
            uint16_t m = 0;
            constexpr unsigned lanes = 16 / Bytes;
            const uint16_t pattern = (uint16_t(1) << Bytes) - uint16_t(1);
            for (unsigned i = 0; i < lanes; ++i) {
                if (((i + phase) % step) == 0) {
                    unsigned bit = i * Bytes;
                    m |= static_cast<uint16_t>(pattern << bit);
                }
            }
            return m;
        }
    }
}

template <unsigned Bytes>
static inline uint64_t strideMask64(unsigned step, unsigned phase) {
    switch (step) {
        case 2:
            return StrideMask64Table<Bytes, 2>::masks[phase & 1u];
        case 4:
            return StrideMask64Table<Bytes, 4>::masks[phase & 3u];
        case 8:
            return StrideMask64Table<Bytes, 8>::masks[phase & 7u];
        default: {
            uint64_t m = 0;
            constexpr unsigned lanes = 64 / Bytes;
            for (unsigned i = 0; i < lanes; ++i) {
                if (((i + phase) % step) == 0) {
                    m |= (uint64_t(1) << i);
                }
            }
            return m;
        }
    }
}

template <unsigned Bytes>
static inline int compressMask32(int m) {
    if constexpr (Bytes == 1)
        return m;
    else if constexpr (Bytes == 2)
        return ((m >> 1) | m) & 0x55555555;
    else if constexpr (Bytes == 4) {
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        return m & 0x11111111;
    } else {  // Bytes == 8
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        m = ((m >> 4) | m);
        return m & 0x01010101;
    }
}

template <unsigned Bytes>
static inline int compressMask16(int m) {
    if constexpr (Bytes == 1)
        return m;
    else if constexpr (Bytes == 2)
        return ((m >> 1) | m) & 0x5555;
    else if constexpr (Bytes == 4) {
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        return m & 0x1111;
    } else {  // Bytes == 8
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        m = ((m >> 4) | m);
        return m & 0x0101;
    }
}

template <typename CellT>
static inline size_t simdScan0Fwd(const CellT* p, const CellT* end) {
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    if constexpr (Bytes > 8) {
        while (x < end) {
            if (*x == 0) return (size_t)(x - p);
            ++x;
        }
        return (size_t)(end - p);
    } else {
        if (runtimeHasAvx512()) {
            constexpr unsigned LANES512 = 64 / Bytes;
            while (((uintptr_t)x & 63u) && x < end) {
                if (*x == 0) return (size_t)(x - p);
                ++x;
            }
            const simde__m512i vz512 = simde_mm512_setzero_si512();
            for (; x + LANES512 <= end; x += LANES512) {
                simde__m512i v = simde_mm512_loadu_si512((const void*)x);
                simde__mmask64 m;
                if constexpr (Bytes == 1)
                    m = simde_mm512_cmpeq_epi8_mask(v, vz512);
                else if constexpr (Bytes == 2)
                    m = simde_mm512_cmpeq_epu16_mask(v, vz512);
                else if constexpr (Bytes == 4)
                    m = simde_mm512_cmpeq_epi32_mask(v, vz512);
                else
                    m = simde_mm512_cmpeq_epi64_mask(v, vz512);
                if (m) {
                    unsigned idx = TZCNT64((std::uint64_t)m);
                    return (size_t)((x - p) + idx);
                }
            }
        }
        constexpr unsigned LANES = 32 / Bytes;
        while (((uintptr_t)x & 31u) && x < end) {
            if (*x == 0) return (size_t)(x - p);
            ++x;
        }
        const simde__m256i vz = simde_mm256_setzero_si256();
        for (; x + LANES <= end; x += LANES) {
            simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)x);
            int m;
            if constexpr (Bytes == 1)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
            else if constexpr (Bytes == 2)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi16(v, vz));
            else if constexpr (Bytes == 4)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
            else
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi64(v, vz));
            m = compressMask32<Bytes>(m);
            if (m) {
                unsigned idx = TZCNT32((unsigned)m);
                return (size_t)((x - p) + idx / Bytes);
            }
        }
        while (x < end) {
            if (*x == 0) return (size_t)(x - p);
            ++x;
        }
        return (size_t)(end - p);
    }
}

/*** step == 1 backward scan: last zero in [base,p], return distance back ***/
template <typename CellT>
static inline size_t simdScan0Back(const CellT* base, const CellT* p) {
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    if constexpr (Bytes > 8) {
        while (x >= base) {
            if (*x == 0) return (size_t)(p - x);
            --x;
        }
        return (size_t)(p - base + 1);
    } else {
        constexpr unsigned LANES = 32 / Bytes;
        while (((uintptr_t)(x - (LANES - 1)) & 31u) && x >= base) {
            if (*x == 0) return (size_t)(p - x);
            --x;
        }
        const simde__m256i vz = simde_mm256_setzero_si256();
        while (x + 1 >= base + LANES) {
            const CellT* blk = x - (LANES - 1);
            simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)blk);
            int m;
            if constexpr (Bytes == 1)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
            else if constexpr (Bytes == 2)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi16(v, vz));
            else if constexpr (Bytes == 4)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
            else
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi64(v, vz));
            m = compressMask32<Bytes>(m);
            if (m) {
                unsigned bit = 31u - (unsigned)LZCNT32((unsigned)m);
                unsigned lane = bit / Bytes;
                return (size_t)(p - (blk + lane));
            }
            x -= LANES;
        }
        while (x >= base) {
            if (*x == 0) return (size_t)(p - x);
            --x;
        }
        return (size_t)(p - base + 1);
    }
}

/*** tiny-stride forward scan: step in {2,4,8} ***/
template <unsigned Step, typename CellT>
static inline size_t simdScan0FwdStride(const CellT* p, const CellT* end, unsigned phase) {
    static_assert(Step == 2 || Step == 4 || Step == 8);
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    constexpr unsigned Mask = Step - 1;
    if constexpr (Bytes > 8) {
        while (x < end) {
            if (phase == 0 && *x == 0) return (size_t)(x - p);
            ++x;
            phase = (phase + 1) & Mask;
        }
        return (size_t)(end - p);
    } else {
        if (runtimeHasAvx512()) {
            constexpr unsigned LANES512 = 64 / Bytes;
            while (((uintptr_t)x & 63u) && x < end) {
                if (phase == 0 && *x == 0) return (size_t)(x - p);
                ++x;
                phase = (phase + 1) & Mask;
            }
            const simde__m512i vz512 = simde_mm512_setzero_si512();
            for (; x + LANES512 <= end; x += LANES512) {
                simde__m512i v = simde_mm512_loadu_si512((const void*)x);
                simde__mmask64 m;
                if constexpr (Bytes == 1)
                    m = simde_mm512_cmpeq_epi8_mask(v, vz512);
                else if constexpr (Bytes == 2)
                    m = simde_mm512_cmpeq_epu16_mask(v, vz512);
                else if constexpr (Bytes == 4)
                    m = simde_mm512_cmpeq_epi32_mask(v, vz512);
                else
                    m = simde_mm512_cmpeq_epi64_mask(v, vz512);
                m &= strideMask64<Bytes>(Step, phase);
                if (m) {
                    unsigned idx = TZCNT64((std::uint64_t)m);
                    return (size_t)((x - p) + idx);
                }
                phase = (phase + LANES512) & Mask;
            }
        }
        constexpr unsigned LANES = 32 / Bytes;
        while (((uintptr_t)x & 31u) && x < end) {
            if (phase == 0 && *x == 0) return (size_t)(x - p);
            ++x;
            phase = (phase + 1) & Mask;
        }
        const simde__m256i vz = simde_mm256_setzero_si256();
        for (; x + LANES <= end; x += LANES) {
            simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)x);
            int m;
            if constexpr (Bytes == 1)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
            else if constexpr (Bytes == 2)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi16(v, vz));
            else if constexpr (Bytes == 4)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
            else
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi64(v, vz));
            m = compressMask32<Bytes>(m);
            m &= (int)strideMask32<Bytes, Step>(phase);
            if (m) {
                unsigned idx = TZCNT32((unsigned)m);
                return (size_t)((x - p) + idx / Bytes);
            }
            phase = (phase + LANES) & Mask;
        }
        while (x < end) {
            if (phase == 0 && *x == 0) return (size_t)(x - p);
            ++x;
            phase = (phase + 1) & Mask;
        }
        return (size_t)(end - p);
    }
}

/*** tiny-stride backward scan: step in {2,4,8} ***/
template <unsigned Step, typename CellT>
static inline size_t simdScan0BackStride(const CellT* base, const CellT* p, unsigned phaseAtP) {
    static_assert(Step == 2 || Step == 4 || Step == 8);
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    constexpr unsigned Mask = Step - 1;
    if constexpr (Bytes > 8) {
        while (x >= base) {
            if (phaseAtP == 0 && *x == 0) return (size_t)(p - x);
            --x;
            phaseAtP = (phaseAtP + Step - 1) & Mask;
        }
        return (size_t)(p - base + 1);
    } else {
        constexpr unsigned LANES = 32 / Bytes;
        while (((uintptr_t)(x - (LANES - 1)) & 31u) && x >= base) {
            if (phaseAtP == 0 && *x == 0) return (size_t)(p - x);
            --x;
            phaseAtP = (phaseAtP + Step - 1) & Mask;
        }
        const simde__m256i vz = simde_mm256_setzero_si256();
        while (x + 1 >= base + LANES) {
            const CellT* blk = x - (LANES - 1);
            unsigned lane0 = (unsigned)(blk - base) & Mask;
            simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)blk);
            int m;
            if constexpr (Bytes == 1)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
            else if constexpr (Bytes == 2)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi16(v, vz));
            else if constexpr (Bytes == 4)
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
            else
                m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi64(v, vz));
            m = compressMask32<Bytes>(m);
            m &= (int)strideMask32<Bytes, Step>(lane0);
            if (m) {
                unsigned bit = 31u - (unsigned)LZCNT32((unsigned)m);
                unsigned lane = bit / Bytes;
                return (size_t)(p - (blk + lane));
            }
            x -= LANES;
        }
        phaseAtP = static_cast<unsigned>(x - base) & Mask;
        while (x >= base) {
            if (phaseAtP == 0 && *x == 0) return (size_t)(p - x);
            --x;
            phaseAtP = (phaseAtP + Step - 1) & Mask;
        }
        return (size_t)(p - base + 1);
    }
}

template <typename CellT>
static inline void simdClear(CellT* start, size_t count) {
    std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(start);
    size_t byteCount = count * sizeof(CellT);
    const simde__m256i zero = simde_mm256_setzero_si256();
    size_t simdBytes = byteCount & ~static_cast<size_t>(31);
    for (size_t i = 0; i < simdBytes; i += 32) {
        simde_mm256_storeu_si256(reinterpret_cast<simde__m256i*>(bytes + i), zero);
    }
    std::memset(bytes + simdBytes, 0, byteCount - simdBytes);
}

constexpr std::array<insType, 256> charToOpcode = [] {
    std::array<insType, 256> table{};
    table.fill(insType::END);
    table[static_cast<unsigned char>('+')] = insType::ADD_SUB;
    table[static_cast<unsigned char>('-')] = insType::ADD_SUB;
    table[static_cast<unsigned char>('>')] = insType::PTR_MOV;
    table[static_cast<unsigned char>('<')] = insType::PTR_MOV;
    table[static_cast<unsigned char>('[')] = insType::JMP_ZER;
    table[static_cast<unsigned char>(']')] = insType::JMP_NOT_ZER;
    table[static_cast<unsigned char>('.')] = insType::PUT_CHR;
    table[static_cast<unsigned char>(',')] = insType::RAD_CHR;
    table[static_cast<unsigned char>('C')] = insType::CLR;
    table[static_cast<unsigned char>('P')] = insType::MUL_CPY;
    table[static_cast<unsigned char>('R')] = insType::SCN_RGT;
    table[static_cast<unsigned char>('L')] = insType::SCN_LFT;
    return table;
}();

template <typename CellT, bool Dynamic, bool Term, bool Sparse>
int executeImpl(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, MemoryModel model, bool adaptive, size_t span, goof2::ProfileInfo* profile,
                std::vector<instruction>* cached) {
    std::vector<instruction> localInstructions;
    auto* instructionsPtr = cached ? cached : &localInstructions;
    bool hasInstructions = cached && !cached->empty();
    auto& instructions = *instructionsPtr;
    if (!hasInstructions) {
        static void* jtable[] = {&&_ADD_SUB,     &&_SET,         &&_PTR_MOV, &&_JMP_ZER,
                                 &&_JMP_NOT_ZER, &&_PUT_CHR,     &&_RAD_CHR, &&_CLR,
                                 &&_CLR_RNG,     &&_MUL_CPY,     &&_SCN_RGT, &&_SCN_LFT,
                                 &&_SCN_CLR_RGT, &&_SCN_CLR_LFT, &&_END};

        int copyloopCounter = 0;
        std::vector<int> copyloopMap;
        copyloopMap.reserve(code.size() / 2);

        int scanloopCounter = 0;
        std::vector<int> scanloopMap;
        scanloopMap.reserve(code.size() / 2);
        std::vector<bool> scanloopClrMap;
        scanloopClrMap.reserve(code.size() / 2);

        if (optimize) {
            regexReplaceInplace(
                code, goof2::vmRegex::nonInstructionRe,
                [](const SvMatch&) { return std::string{}; }, [](const SvMatch&) { return 0u; });
            regexReplaceInplace(code, goof2::vmRegex::balanceSeqRe, [&](const SvMatch& what) {
                std::string_view current{what[0].first, static_cast<size_t>(what.length())};
                const char first = current[0];
                return (first == '+' || first == '-') ? processBalanced(current, '+', '-')
                                                      : processBalanced(current, '>', '<');
            });

            regexReplaceInplace(
                code, goof2::vmRegex::clearLoopRe, [](const SvMatch&) { return std::string("C"); },
                [](const SvMatch&) { return 1u; });

            const std::string baseCode = code;
            auto scanFuture = std::async(std::launch::async, [baseCode, &scanloopMap,
                                                              &scanloopClrMap]() {
                std::vector<RegexReplacement> reps;
                auto collect = [&](const std::regex& re, bool clrFlag) {
                    auto vec = regexCollect(baseCode, re, [&](const SvMatch& what) {
                        std::string_view current{what[0].first, static_cast<size_t>(what.length())};
                        const auto count =
                            std::ranges::count(current, '>') - std::ranges::count(current, '<');
                        std::string rep;
                        if (count > 0)
                            rep = "R";
                        else if (count == 0)
                            rep = std::string(current);
                        else
                            rep = "L";
                        return std::pair<std::string, std::function<void()>>{
                            std::move(rep), [&, clrFlag, step = std::abs(count)]() {
                                scanloopMap.push_back(step);
                                scanloopClrMap.push_back(clrFlag);
                            }};
                    });
                    reps.insert(reps.end(), vec.begin(), vec.end());
                };
                collect(goof2::vmRegex::scanLoopClrRe, true);
                collect(goof2::vmRegex::scanLoopRe, false);
                return reps;
            });

            auto commaFuture = std::async(std::launch::async, [baseCode]() {
                return regexCollect(baseCode, goof2::vmRegex::commaTrimRe, [](const SvMatch&) {
                    return std::pair<std::string, std::function<void()>>{std::string(","), {}};
                });
            });

            auto scanReps = scanFuture.get();
            auto commaReps = commaFuture.get();
            std::vector<RegexReplacement> allReps;
            allReps.reserve(scanReps.size() + commaReps.size());
            allReps.insert(allReps.end(), scanReps.begin(), scanReps.end());
            allReps.insert(allReps.end(), commaReps.begin(), commaReps.end());
            std::sort(allReps.begin(), allReps.end(),
                      [](const auto& a, const auto& b) { return a.start > b.start; });
            for (auto& r : allReps) {
                code.replace(r.start, r.end - r.start, r.text);
                if (r.sideEffect) r.sideEffect();
            }
            regexReplaceInplace(
                code, goof2::vmRegex::clearThenSetRe,
                [](const SvMatch& what) {
                    std::string result{"S"};
                    result.append(what[1].first, what[1].second);
                    return result;
                },
                [](const SvMatch& what) { return 1u + static_cast<size_t>(what[1].length()); });

            regexReplaceInplace(code, goof2::vmRegex::copyLoopRe, [&](const SvMatch& what) {
                int offset = 0;
                std::string_view whole{what[0].first, static_cast<size_t>(what.length())};
                std::string current{what[1].first, what[1].second};
                current.append(what[2].first, what[2].second);

                if (std::count(whole.begin(), whole.end(), '>') -
                        std::count(whole.begin(), whole.end(), '<') ==
                    0) {
                    SvMatch whatL;
                    std::string_view currentView{current};
                    auto start = currentView.cbegin();
                    auto end = currentView.cend();
                    std::unordered_map<int, int> deltaMap;
                    std::vector<int> order;
                    while (std::regex_search(start, end, whatL, goof2::vmRegex::copyLoopInnerRe)) {
                        offset += -std::count(whatL[0].first, whatL[0].second, '<') +
                                  std::count(whatL[0].first, whatL[0].second, '>');
                        int delta = std::count(whatL[0].first, whatL[0].second, '+') -
                                    std::count(whatL[0].first, whatL[0].second, '-');
                        if (deltaMap.insert({offset, delta}).second) {
                            order.push_back(offset);
                        } else {
                            deltaMap[offset] += delta;
                        }
                        start = whatL[0].second;
                    }
                    // Check if every target's accumulated delta is zero.
                    const bool allZero = std::ranges::all_of(
                        deltaMap, [](const auto& it) { return it.second == 0; });
                    if (!allZero) {
                        for (const auto& off : order) {
                            copyloopMap.push_back(off);
                            copyloopMap.push_back(deltaMap[off]);
                        }
                        return std::string(order.size(), 'P') + "C";
                    }
                    // When all deltas are zero, drop the P instructions and only clear.
                    return std::string("C");
                } else {
                    return std::string(whole);
                }
            });

            if constexpr (!Term)
                regexReplaceInplace(
                    code, goof2::vmRegex::leadingSetRe,
                    [](const SvMatch& what) {
                        std::string result;
                        result.append(what[1].first, what[1].second);
                        result += 'S';
                        result.append(what[2].first, what[2].second);
                        return result;
                    },
                    [](const SvMatch& what) {
                        return static_cast<size_t>(what[1].length() + 1 + what[2].length());
                    });  // We can't really assume in term

            regexReplaceInplace(
                code, goof2::vmRegex::clearSeqRe, [](const SvMatch&) { return std::string("C"); },
                [](const SvMatch&) { return 1u; });
        }

        std::vector<size_t> braceTable(code.length());
        {
            std::vector<size_t> stack;
            for (size_t i = 0; i < code.length(); ++i) {
                const char ch = code[i];
                if (ch == '[') {
                    stack.push_back(i);
                } else if (ch == ']') {
                    if (stack.empty()) return 1;
                    const size_t start = stack.back();
                    stack.pop_back();
                    braceTable[start] = i;
                    braceTable[i] = start;
                }
            }
            if (!stack.empty()) return 2;
        }
        std::vector<size_t> braceInst(code.length());
        int16_t offset = 0;
        bool set = false;
        ptrdiff_t compilePos = 0, compileMin = 0, compileMax = 0;
        instructions.reserve(code.length());

        auto emit = [&](insType op, instruction inst) {
            inst.op = op;
            if (op == insType::CLR && !instructions.empty()) {
                auto& last = instructions.back();
                insType lastOp = last.op;
                if (lastOp == insType::CLR) {
                    if (inst.offset == last.offset + 1) {
                        last.data = 2;
                        last.op = insType::CLR_RNG;
                        return;
                    } else if (inst.offset + 1 == last.offset) {
                        last.data = 2;
                        last.offset = inst.offset;
                        last.op = insType::CLR_RNG;
                        return;
                    }
                } else if (lastOp == insType::CLR_RNG) {
                    if (inst.offset == last.offset + last.data) {
                        last.data++;
                        return;
                    } else if (inst.offset + 1 == last.offset) {
                        last.offset = inst.offset;
                        last.data++;
                        return;
                    }
                }
            }
            if (!instructions.empty() && instructions.back().offset == inst.offset) {
                auto& last = instructions.back();
                insType lastOp = last.op;
                bool lastIsWrite = lastOp == insType::ADD_SUB || lastOp == insType::SET ||
                                   lastOp == insType::CLR || lastOp == insType::CLR_RNG;
                bool newIsWrite = op == insType::ADD_SUB || op == insType::SET ||
                                  op == insType::CLR || op == insType::CLR_RNG;
                if (lastIsWrite && newIsWrite) {
                    if (op == insType::ADD_SUB) {
                        if (lastOp == insType::ADD_SUB) {
                            last.data += inst.data;
                            return;
                        } else if (lastOp == insType::SET) {
                            last.data = static_cast<CellT>(last.data + inst.data);
                            return;
                        } else if (lastOp == insType::CLR) {
                            instructions.pop_back();
                            instructions.push_back(instruction{
                                nullptr, static_cast<int32_t>(static_cast<CellT>(inst.data)), 0,
                                inst.offset, insType::SET});
                            return;
                        }
                    } else {
                        instructions.pop_back();
                        instructions.push_back(inst);
                        return;
                    }
                }
            }
            instructions.push_back(inst);
        };

#define MOVEOFFSET()                                                \
    if (offset) [[likely]] {                                        \
        emit(insType::PTR_MOV, instruction{nullptr, offset, 0, 0}); \
        offset = 0;                                                 \
    }

        for (size_t i = 0; i < code.length(); i++) {
            const unsigned char ch = static_cast<unsigned char>(code[i]);
            const insType op = charToOpcode[ch];
            switch (op) {
                case insType::ADD_SUB: {
                    if (code[i] == '+') {
                        const insType actual = set ? insType::SET : insType::ADD_SUB;
                        emit(actual, instruction{nullptr, fold(code, i, '+'), 0, offset});
                    } else {
                        const int32_t folded = -fold(code, i, '-');
                        const insType actual = set ? insType::SET : insType::ADD_SUB;
                        emit(actual,
                             instruction{
                                 nullptr,
                                 set ? static_cast<int32_t>(static_cast<CellT>(folded)) : folded, 0,
                                 offset});
                    }
                    set = false;
                    break;
                }
                case insType::PTR_MOV: {
                    if (code[i] == '>') {
                        int16_t amt = static_cast<int16_t>(fold(code, i, '>'));
                        offset += amt;
                        compilePos += amt;
                    } else {
                        int16_t amt = static_cast<int16_t>(fold(code, i, '<'));
                        offset -= amt;
                        compilePos -= amt;
                    }
                    if (compilePos > compileMax) compileMax = compilePos;
                    if (compilePos < compileMin) compileMin = compilePos;
                    break;
                }
                case insType::JMP_ZER:
                    MOVEOFFSET();
                    braceInst[i] = instructions.size();
                    emit(insType::JMP_ZER, instruction{nullptr, 0, 0, 0});
                    break;
                case insType::JMP_NOT_ZER: {
                    MOVEOFFSET();
                    const size_t startCode = braceTable[i];
                    const size_t startInst = braceInst[startCode];
                    const int sizeminstart = instructions.size() - startInst;
                    instructions[startInst].data = sizeminstart;
                    emit(insType::JMP_NOT_ZER, instruction{nullptr, sizeminstart, 0, 0});
                    break;
                }
                case insType::PUT_CHR:
                    emit(insType::PUT_CHR, instruction{nullptr, fold(code, i, '.'), 0, offset});
                    break;
                case insType::RAD_CHR:
                    emit(insType::RAD_CHR, instruction{nullptr, 0, 0, offset});
                    break;
                case insType::CLR:
                    emit(insType::CLR, instruction{nullptr, 0, 0, offset});
                    break;
                case insType::MUL_CPY:
                    emit(insType::MUL_CPY,
                         instruction{nullptr, copyloopMap[copyloopCounter++],
                                     static_cast<int16_t>(copyloopMap[copyloopCounter++]), offset});
                    break;
                case insType::SCN_RGT:
                case insType::SCN_LFT: {
                    MOVEOFFSET();
                    const auto step = scanloopMap[scanloopCounter];
                    const bool clr = scanloopClrMap[scanloopCounter++];
                    emit(op == insType::SCN_RGT ? (clr ? insType::SCN_CLR_RGT : insType::SCN_RGT)
                                                : (clr ? insType::SCN_CLR_LFT : insType::SCN_LFT),
                         instruction{nullptr, step, 0, 0});
                    break;
                }
                default:
                    if (code[i] == 'S') set = true;
                    break;
            }
        }
        if (static_cast<size_t>(compileMax - compileMin + 1) > span)
            span = static_cast<size_t>(compileMax - compileMin + 1);
        MOVEOFFSET();
        emit(insType::END, instruction{nullptr, 0, 0, 0});

        instructions.shrink_to_fit();
        for (auto& inst : instructions) {
            inst.jump = jtable[static_cast<size_t>(inst.op)];
        }
    }

    auto insp = instructions.data();
    [[maybe_unused]] std::unordered_map<size_t, CellT> sparseTape;
    [[maybe_unused]] size_t sparseIndex = cellPtr;
    [[maybe_unused]] size_t sparseMaxIndex = 0;
    CellT* __restrict cellBase = cells.data();
    CellT* __restrict cell = cellBase + cellPtr;
    size_t osSize = cells.size();
    if constexpr (Sparse) {
        for (size_t i = 0; i < cells.size(); ++i) {
            if (cells[i] != 0) {
                sparseTape[i] = cells[i];
                if (i > sparseMaxIndex) sparseMaxIndex = i;
            }
        }
    }
    if constexpr (!Sparse) {
#if GOOF2_HAS_OS_VM
        if (model == MemoryModel::OSBacked) {
            void* ptr = goof2::os_alloc(osSize * sizeof(CellT));
#ifdef _WIN32
            if (ptr)
#else
            if (ptr != MAP_FAILED)
#endif
            {
                CellT* osMem = static_cast<CellT*>(ptr);
                std::memcpy(osMem, cellBase, osSize * sizeof(CellT));
                cellBase = osMem;
                cell = cellBase + cellPtr;
            } else {
                std::cerr << "warning: OS-backed allocation failed, falling back to contiguous "
                             "memory model"
                          << std::endl;
                model = MemoryModel::Contiguous;
            }
        }
#endif
    }

    constexpr size_t PAGE_SIZE = 1u << 16;  // 64KB pages for paged growth
    size_t fibA = cells.size(), fibB = cells.size();
    auto chooseModel = [&](size_t size) {
#if GOOF2_HAS_OS_VM
        if (size > (1u << 28)) return MemoryModel::OSBacked;
#endif
        if (size > (1u << 24)) return MemoryModel::Paged;
        if (size > (1u << 16)) return MemoryModel::Fibonacci;
        return MemoryModel::Contiguous;
    };
    auto ensure = [&](ptrdiff_t currentCell, ptrdiff_t neededIndex) {
        if constexpr (Sparse) return;  // sparse tape allocates on demand
        size_t needed = static_cast<size_t>(neededIndex + 1);
        if (adaptive && needed > span) span = needed;
        MemoryModel target = adaptive ? chooseModel(needed) : model;
        if (adaptive && target != model) {
            if (target == MemoryModel::OSBacked) {
#if GOOF2_HAS_OS_VM
                size_t newSize = ((needed + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
                void* nptr = goof2::os_alloc(newSize * sizeof(CellT));
#ifdef _WIN32
                bool ok = nptr != nullptr;
#else
                bool ok = nptr != MAP_FAILED;
#endif
                if (ok) {
                    CellT* newMem = static_cast<CellT*>(nptr);
                    std::memcpy(
                        newMem, cellBase,
                        (model == MemoryModel::OSBacked ? osSize : cells.size()) * sizeof(CellT));
                    if (model == MemoryModel::OSBacked) {
                        goof2::os_free(cellBase, osSize * sizeof(CellT));
                    } else {
                        std::vector<CellT>().swap(cells);
                    }
                    cellBase = newMem;
                    osSize = newSize;
                    model = MemoryModel::OSBacked;
                    fibA = fibB = osSize;
                } else {
                    std::cerr << "warning: OS-backed allocation failed, falling back to contiguous "
                                 "memory model"
                              << std::endl;
                    model = MemoryModel::Contiguous;
                }
#endif
            } else {
                if (model == MemoryModel::OSBacked) {
                    size_t oldSize = osSize;
                    cells.resize(oldSize);
                    std::memcpy(cells.data(), cellBase, oldSize * sizeof(CellT));
                    goof2::os_free(cellBase, osSize * sizeof(CellT));
                    cellBase = cells.data();
                }
                model = target;
                if (model == MemoryModel::Fibonacci) fibA = fibB = cells.size();
            }
        }
        switch (model) {
            case MemoryModel::OSBacked: {
#if GOOF2_HAS_OS_VM
                size_t newSize = ((needed + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
                if (newSize > osSize) {
                    bool resized = false;
#if defined(__linux__)
                    void* mptr = mremap(cellBase, osSize * sizeof(CellT), newSize * sizeof(CellT),
                                        MREMAP_MAYMOVE);
                    if (mptr != MAP_FAILED) {
                        cellBase = static_cast<CellT*>(mptr);
                        osSize = newSize;
                        resized = true;
                    }
#elif defined(_WIN32)
                    auto basePtr = reinterpret_cast<uint8_t*>(cellBase);
                    void* mptr = VirtualAlloc(basePtr + osSize * sizeof(CellT),
                                              (newSize - osSize) * sizeof(CellT), MEM_COMMIT,
                                              PAGE_READWRITE);
                    if (mptr) {
                        osSize = newSize;
                        resized = true;
                    }
#endif
                    if (!resized) {
                        void* nptr = goof2::os_alloc(newSize * sizeof(CellT));
#ifdef _WIN32
                        bool ok = nptr != nullptr;
#else
                        bool ok = nptr != MAP_FAILED;
#endif
                        if (ok) {
                            CellT* newMem = static_cast<CellT*>(nptr);
                            std::memcpy(newMem, cellBase, osSize * sizeof(CellT));
                            goof2::os_free(cellBase, osSize * sizeof(CellT));
                            cellBase = newMem;
                            osSize = newSize;
                        } else {
                            std::cerr << "warning: OS-backed allocation failed, falling back to "
                                         "contiguous "
                                         "memory model"
                                      << std::endl;
                            cells.resize(newSize);
                            std::memcpy(cells.data(), cellBase, osSize * sizeof(CellT));
                            goof2::os_free(cellBase, osSize * sizeof(CellT));
                            cellBase = cells.data();
                            osSize = cells.size();
                            model = MemoryModel::Contiguous;
                        }
                    }
                }
                break;
#else
                while (cells.size() < needed) cells.resize(cells.size() * 2);
                break;
#endif
            }
            case MemoryModel::Paged: {
                size_t newSize = ((needed + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
                if (newSize > cells.size()) cells.resize(newSize);
                break;
            }
            case MemoryModel::Fibonacci: {
                while (cells.size() < needed) {
                    size_t next = fibA + fibB;
                    fibA = fibB;
                    fibB = next;
                    cells.resize(next);
                }
                break;
            }
            default: {
                while (cells.size() < needed) cells.resize(cells.size() * 2);
                break;
            }
        }
        if (model != MemoryModel::OSBacked) cellBase = cells.data();
        cell = cellBase + currentCell;
    };

    auto cellRef = [&](ptrdiff_t off) -> CellT& {
        if constexpr (Sparse) {
            size_t idx = sparseIndex + off;
            auto& ref = sparseTape[idx];
            if (idx > sparseMaxIndex) sparseMaxIndex = idx;
            return ref;
        } else {
            return *(cell + off);
        }
    };

    goto * insp->jump;

#define LOOP()                            \
    insp++;                               \
    if (profile) ++profile->instructions; \
    goto * insp->jump
#define EXPAND_IF_NEEDED()                                                               \
    if constexpr (!Sparse) {                                                             \
        if (insp->offset > 0) {                                                          \
            const ptrdiff_t currentCell = cell - cellBase;                               \
            const ptrdiff_t neededIndex = currentCell + insp->offset;                    \
            size_t totalSize = (model == MemoryModel::OSBacked ? osSize : cells.size()); \
            size_t needed = static_cast<size_t>(neededIndex + 1);                        \
            if (needed > totalSize || (adaptive && needed > span)) {                     \
                ensure(currentCell, neededIndex);                                        \
            }                                                                            \
        }                                                                                \
    }

#define OFFCELL() cellRef(insp->offset)
#define OFFCELLP() cellRef(insp->offset + insp->data)
_ADD_SUB:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() += insp->data;
    LOOP();

_SET:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() = insp->data;
    LOOP();

_PTR_MOV: {
    if constexpr (Sparse) {
        const ptrdiff_t newIndex = static_cast<ptrdiff_t>(sparseIndex) + insp->data;
        if (newIndex < 0) {
            cellPtr = sparseIndex;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        sparseIndex = static_cast<size_t>(newIndex);
        LOOP();
    } else {
        const ptrdiff_t currentCell = cell - cellBase;
        const ptrdiff_t newIndex = currentCell + insp->data;
        if (newIndex < 0) {
            cellPtr = currentCell;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        size_t needed = static_cast<size_t>(newIndex + 1);
        if (newIndex >= static_cast<ptrdiff_t>(cells.size()) || (adaptive && needed > span)) {
            if constexpr (Dynamic) {
                ensure(currentCell, newIndex);
            } else if (newIndex >= static_cast<ptrdiff_t>(cells.size())) {
                cellPtr = currentCell;
                std::cerr << "cell pointer moved beyond end" << std::endl;
                return -1;
            }
        }
        cell = cellBase + newIndex;
        LOOP();
    }
}

_JMP_ZER:
    if (!cellRef(0)) [[unlikely]]
        insp += insp->data;
    LOOP();

_JMP_NOT_ZER:
    if (cellRef(0)) [[likely]]
        insp -= insp->data;
    LOOP();

_PUT_CHR:
    if (size_t count = static_cast<size_t>(insp->data); count) {
        const char ch = static_cast<char>(OFFCELL());
        char buf[256];
        std::memset(buf, static_cast<int>(ch), sizeof(buf));
        while (count >= sizeof(buf)) {
            std::cout.write(buf, sizeof(buf));
            count -= sizeof(buf);
        }
        if (count) {
            std::cout.write(buf, count);
        }
        std::cout.flush();
    }
    LOOP();

_RAD_CHR:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    int in;
    in = std::cin.get();
    if (in == EOF) {
        switch (eof) {
            case 0:
                break;
            case 1:
                OFFCELL() = 0;
                break;
            case 2:
                OFFCELL() = static_cast<CellT>(255);
                break;
            default:
                __builtin_unreachable();
        }
    } else {
        OFFCELL() = static_cast<CellT>(in);
    }
    LOOP();

_CLR:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() = 0;
    LOOP();

_CLR_RNG:
    if constexpr (Dynamic) {
        if constexpr (!Sparse) {
            if (insp->data > 0) {
                ptrdiff_t maxOffset = insp->offset + insp->data - 1;
                if (maxOffset > 0) {
                    const ptrdiff_t currentCell = cell - cellBase;
                    const ptrdiff_t neededIndex = currentCell + maxOffset;
                    size_t totalSize = (model == MemoryModel::OSBacked ? osSize : cells.size());
                    size_t needed = static_cast<size_t>(neededIndex + 1);
                    if (needed > totalSize || (adaptive && needed > span)) {
                        ensure(currentCell, neededIndex);
                    }
                }
            }
        }
    }
    if constexpr (Sparse) {
        for (int32_t i = 0; i < insp->data; ++i) {
            cellRef(insp->offset + i) = 0;
        }
    } else {
        CellT* start = cell + insp->offset;
        simdClear<CellT>(start, static_cast<size_t>(insp->data));
    }
    LOOP();

_MUL_CPY:
    if constexpr (Sparse) {
        OFFCELLP() += OFFCELL() * insp->auxData;
        LOOP();
    }
    if constexpr (Dynamic) {
        const ptrdiff_t currentCell = cell - cellBase;
        const ptrdiff_t neededIndex =
            currentCell + insp->offset + insp->data;  // ensure target exists
        size_t totalSize = (model == MemoryModel::OSBacked ? osSize : cells.size());
        size_t needed = static_cast<size_t>(neededIndex + 1);
        if (needed > totalSize || (adaptive && needed > span)) {
            ensure(currentCell, neededIndex);
        }
    }
    if constexpr (std::is_same_v<CellT, uint8_t> || std::is_same_v<CellT, uint16_t> ||
                  std::is_same_v<CellT, uint32_t> || std::is_same_v<CellT, uint64_t>) {
        constexpr int lanes = std::is_same_v<CellT, uint8_t> ? 16 : (32 / sizeof(CellT));
        // Attempt to process consecutive MUL_CPY instructions at once
        const instruction* base = insp;
        bool canSimd = true;
        for (int i = 1; i < lanes; ++i) {
            if (base[i].jump != &&_MUL_CPY || base[i].offset != base[0].offset ||
                base[i].data != base[0].data + i) {
                canSimd = false;
                break;
            }
        }
        if (canSimd) {
            if constexpr (Dynamic) {
                const ptrdiff_t currentCell = cell - cellBase;
                const ptrdiff_t neededIndex =
                    currentCell + base[lanes - 1].offset + base[lanes - 1].data;
                size_t totalSize = (model == MemoryModel::OSBacked ? osSize : cells.size());
                size_t needed = static_cast<size_t>(neededIndex + 1);
                if (needed > totalSize || (adaptive && needed > span)) {
                    ensure(currentCell, neededIndex);
                }
            }
            CellT src = *(cell + base[0].offset);
            CellT* dst = cell + base[0].offset + base[0].data;
            alignas(32) int16_t factors[lanes];
            for (int i = 0; i < lanes; ++i) factors[i] = base[i].auxData;
            if constexpr (std::is_same_v<CellT, uint8_t>) {
                simde__m256i dstv =
                    simde_mm256_cvtepu8_epi16(simde_mm_loadu_si128((const simde__m128i*)dst));
                simde__m256i srcv = simde_mm256_set1_epi16(src);
                simde__m256i facv = simde_mm256_loadu_si256((const simde__m256i*)factors);
                simde__m256i prod = simde_mm256_mullo_epi16(srcv, facv);
                simde__m256i sum = simde_mm256_add_epi16(dstv, prod);
                sum = simde_mm256_and_si256(sum, simde_mm256_set1_epi16(0xFF));
                simde__m256i packed = simde_mm256_packus_epi16(sum, simde_mm256_setzero_si256());
                simde_mm_storeu_si128((simde__m128i*)dst, simde_mm256_castsi256_si128(packed));
            } else if constexpr (std::is_same_v<CellT, uint16_t>) {
                simde__m256i dstv = simde_mm256_loadu_si256((const simde__m256i*)dst);
                simde__m256i srcv = simde_mm256_set1_epi16(src);
                simde__m256i facv = simde_mm256_loadu_si256((const simde__m256i*)factors);
                simde__m256i prod = simde_mm256_mullo_epi16(srcv, facv);
                simde__m256i sum = simde_mm256_add_epi16(dstv, prod);
                simde_mm256_storeu_si256((simde__m256i*)dst, sum);
            } else if constexpr (std::is_same_v<CellT, uint32_t>) {
                simde__m256i dstv = simde_mm256_loadu_si256((const simde__m256i*)dst);
                simde__m256i srcv = simde_mm256_set1_epi32(static_cast<int32_t>(src));
                simde__m256i facv =
                    simde_mm256_cvtepi16_epi32(simde_mm_loadu_si128((const simde__m128i*)factors));
                simde__m256i prod = simde_mm256_mullo_epi32(srcv, facv);
                simde__m256i sum = simde_mm256_add_epi32(dstv, prod);
                simde_mm256_storeu_si256((simde__m256i*)dst, sum);
            } else {
                using SignedT = std::make_signed_t<CellT>;
                alignas(32) SignedT prod[lanes];
                SignedT srcs = static_cast<SignedT>(src);
                for (int i = 0; i < lanes; ++i) prod[i] = srcs * static_cast<SignedT>(factors[i]);
                simde__m256i dstv = simde_mm256_loadu_si256((const simde__m256i*)dst);
                simde__m256i prodv = simde_mm256_loadu_si256((const simde__m256i*)prod);
                simde__m256i sum = simde_mm256_add_epi64(dstv, prodv);
                simde_mm256_storeu_si256((simde__m256i*)dst, sum);
            }
            insp += lanes - 1;  // skip processed instructions
            LOOP();
        }
    }
    OFFCELLP() += OFFCELL() * insp->auxData;
    LOOP();

_SCN_RGT: {
    const unsigned step = static_cast<unsigned>(insp->data);
    if constexpr (Sparse) {
        if (cellRef(0) == 0) {
            LOOP();
        }
        do {
            sparseIndex += step;
        } while (cellRef(0) != 0);
        LOOP();
    }

    if (*cell == 0) {
        LOOP();
    }

    // small pre-grow to cut resize churn during long scans
    if constexpr (Dynamic) {
        while ((cell - cellBase) + 64 >= static_cast<ptrdiff_t>(cells.size()) ||
               (adaptive && static_cast<size_t>((cell - cellBase) + 65) > span)) {
            const ptrdiff_t rel = cell - cellBase;
            ensure(rel, rel + 64);
        }
    }

    for (;;) {
        CellT* const end = cellBase + cells.size();
        size_t off;
        if (step == 1) {
            off = simdScan0Fwd<CellT>(cell, end);
        } else if (step == 2) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 1u;
            if (phase == 0)
                off = simdScan0FwdStride<2, CellT>(cell, end, 0);
            else {
                off = 2;
                while (cell + off < end && *(cell + off) != 0) off += 2;
            }
        } else if (step == 4) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 3u;
            if (phase == 0)
                off = simdScan0FwdStride<4, CellT>(cell, end, 0);
            else {
                off = 4;
                while (cell + off < end && *(cell + off) != 0) off += 4;
            }
        } else if (step == 8) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 7u;
            if (phase == 0)
                off = simdScan0FwdStride<8, CellT>(cell, end, 0);
            else {
                off = 8;
                while (cell + off < end && *(cell + off) != 0) off += 8;
            }
        } else {
            // scalar fallback for arbitrary step
            off = step;
            while (cell + off + step * 3 < end && *(cell + off) != 0 && *(cell + off + step) != 0 &&
                   *(cell + off + step * 2) != 0 && *(cell + off + step * 3) != 0) {
                off += step * 4;
            }
            while (cell + off < end && *(cell + off) != 0) off += step;
        }

        cell += off;

        if (adaptive && static_cast<size_t>((cell - cellBase) + 1) > span) {
            const ptrdiff_t rel = cell - cellBase;
            ensure(rel, rel);
        }

        if (cell < end) {
            // found zero
            LOOP();
        }

        if constexpr (Dynamic) {
            // grow and continue the scan into new space
            const ptrdiff_t rel = cell - cellBase;
            ensure(rel, rel);
            continue;
        } else {
            cell = end - 1;
            cellPtr = cell - cellBase;
            std::cerr << "cell pointer moved beyond end" << std::endl;
            return -1;
        }
    }
}

_SCN_LFT: {
    const unsigned step = static_cast<unsigned>(insp->data);
    if constexpr (Sparse) {
        if (cellRef(0) == 0) {
            LOOP();
        }
        while (sparseIndex >= step && cellRef(0) != 0) {
            sparseIndex -= step;
        }
        if (sparseIndex < step && cellRef(0) != 0) {
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    }

    if (cell < cellBase) {
        cellPtr = 0;
        std::cerr << "cell pointer moved before start" << std::endl;
        return -1;
    }

    if (*cell == 0) {
        LOOP();
    }

    if (step == 1) {
        size_t back = simdScan0Back<CellT>(cellBase, cell);
        cell -= back;
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    } else if (step == 2) {
        unsigned phaseAtP = static_cast<unsigned>(cell - cellBase) & 1u;
        size_t back;
        if (phaseAtP == 0)
            back = simdScan0BackStride<2, CellT>(cellBase, cell, 0);
        else {
            back = 2;
            while (cell >= cellBase + back && *(cell - back) != 0) back += 2;
        }
        cell -= back;
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    } else if (step == 4) {
        unsigned phaseAtP = static_cast<unsigned>(cell - cellBase) & 3u;
        size_t back;
        if (phaseAtP == 0)
            back = simdScan0BackStride<4, CellT>(cellBase, cell, 0);
        else {
            back = 4;
            while (cell >= cellBase + back && *(cell - back) != 0) back += 4;
        }
        cell -= back;
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    } else if (step == 8) {
        unsigned phaseAtP = static_cast<unsigned>(cell - cellBase) & 7u;
        size_t back;
        if (phaseAtP == 0)
            back = simdScan0BackStride<8, CellT>(cellBase, cell, 0);
        else {
            back = 8;
            while (cell >= cellBase + back && *(cell - back) != 0) back += 8;
        }
        cell -= back;
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    } else {
        // scalar fallback for arbitrary step
        size_t back = step;
        while (cell >= cellBase + back + step * 3 && *(cell - back) != 0 &&
               *(cell - back - step) != 0 && *(cell - back - step * 2) != 0 &&
               *(cell - back - step * 3) != 0) {
            back += step * 4;
        }
        while (cell >= cellBase + back && *(cell - back) != 0) back += step;
        cell -= back;
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    }
}

_SCN_CLR_RGT: {
    const unsigned step = static_cast<unsigned>(insp->data);
    if constexpr (Sparse) {
        while (cellRef(0) != 0) {
            cellRef(0) = 0;
            sparseIndex += step;
        }
        LOOP();
    }

    if constexpr (Dynamic) {
        while ((cell - cellBase) + 64 >= static_cast<ptrdiff_t>(cells.size()) ||
               (adaptive && static_cast<size_t>((cell - cellBase) + 65) > span)) {
            const ptrdiff_t rel = cell - cellBase;
            ensure(rel, rel + 64);
        }
    }

    if (step == 1) {
        for (;;) {
            CellT* end = cellBase + cells.size();
            size_t off = simdScan0Fwd<CellT>(cell, end);
            if (off == 0) {
                LOOP();
            }
            simdClear<CellT>(cell, off);
            cell += off;
            if (adaptive && static_cast<size_t>((cell - cellBase) + 1) > span) {
                const ptrdiff_t rel = cell - cellBase;
                ensure(rel, rel);
            }
            if (cell < end) {
                LOOP();
            }
            if constexpr (Dynamic) {
                const ptrdiff_t rel = cell - cellBase;
                ensure(rel, rel);
            } else {
                cell = end - 1;
                cellPtr = cell - cellBase;
                std::cerr << "cell pointer moved beyond end" << std::endl;
                return -1;
            }
        }
    } else {
        for (;;) {
            if (*cell == 0) {
                LOOP();
            }
            *cell = 0;
            cell += step;
            if (adaptive && static_cast<size_t>((cell - cellBase) + 1) > span) {
                const ptrdiff_t rel = cell - cellBase;
                ensure(rel, rel);
            }
            if (cell < cellBase + cells.size()) {
                continue;
            }
            if constexpr (Dynamic) {
                const ptrdiff_t rel = cell - cellBase;
                ensure(rel, rel);
            } else {
                cell = cellBase + cells.size() - 1;
                cellPtr = cell - cellBase;
                std::cerr << "cell pointer moved beyond end" << std::endl;
                return -1;
            }
        }
    }
}

_SCN_CLR_LFT: {
    const unsigned step = static_cast<unsigned>(insp->data);
    if constexpr (Sparse) {
        while (cellRef(0) != 0) {
            if (sparseIndex < step) {
                cellPtr = 0;
                std::cerr << "cell pointer moved before start" << std::endl;
                return -1;
            }
            sparseIndex -= step;
            cellRef(0) = 0;
        }
        LOOP();
    }

    for (;;) {
        if (*cell == 0) {
            LOOP();
        }
        if (cell - cellBase < static_cast<ptrdiff_t>(step)) {
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        cell -= step;
        *cell = 0;
    }
}

_END: {
    ptrdiff_t finalIndex;
    if constexpr (Sparse) {
        finalIndex = static_cast<ptrdiff_t>(sparseIndex);
        size_t needed = sparseMaxIndex + 1;
        if constexpr (Dynamic) {
            if (needed > cells.size()) cells.resize(needed, 0);
        } else {
            if (needed > cells.size()) {
                cellPtr = finalIndex;
                std::cerr << "cell pointer moved beyond end" << std::endl;
                return -1;
            }
        }
        for (const auto& kv : sparseTape) {
            if (kv.first < cells.size()) cells[kv.first] = kv.second;
        }
    } else {
        finalIndex = cell - cellBase;
#if GOOF2_HAS_OS_VM
        if (model == MemoryModel::OSBacked && cellBase != cells.data()) {
            cells.assign(cellBase, cellBase + osSize);
            goof2::os_free(cellBase, osSize * sizeof(CellT));
            cellBase = cells.data();
        }
#endif
    }
    cellPtr = finalIndex;
}
    return 0;
}

struct SpanInfo {
    bool sparse;
    size_t span;
};

static SpanInfo analyzeSpan(std::string_view code) {
    ptrdiff_t pos = 0, minPos = 0, maxPos = 0;
    for (char c : code) {
        if (c == '>') {
            ++pos;
            if (pos > maxPos) maxPos = pos;
        } else if (c == '<') {
            --pos;
            if (pos < minPos) minPos = pos;
        }
    }
    size_t span = static_cast<size_t>(maxPos - minPos + 1);
    bool sparse = span > 100000;
    return {sparse, span};
}

template <typename CellT>
int goof2::execute(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                   int eof, bool dynamicSize, bool term, MemoryModel model, ProfileInfo* profile,
                   InstructionCache* cache) {
    int ret = 0;
    std::chrono::steady_clock::time_point start;
    if (profile) {
        profile->instructions = 0;
        start = std::chrono::steady_clock::now();
    }
    SpanInfo spanInfo = analyzeSpan(code);
    bool sparse = spanInfo.sparse;
    size_t key = 0;
    std::vector<instruction>* cacheVec = nullptr;
    if (cache) {
        if (cache->empty()) cache->reserve(kCacheExpectedEntries);
        key = std::hash<std::string>{}(code);
        key ^= static_cast<size_t>(optimize) << 1;
        key ^= static_cast<size_t>(term) << 2;
        auto it = cache->find(key);
        if (it != cache->end() && it->second.source == code) {
            cacheVec = &it->second.instructions;
            it->second.lastUsed = ++cacheCounter;
            sparse = it->second.sparse;
        } else {
            auto& entry = (*cache)[key];
            entry.source = code;
            entry.instructions.clear();
            entry.lastUsed = ++cacheCounter;
            entry.sparse = sparse;
            cacheVec = &entry.instructions;
            if (cache->size() > kCacheMaxEntries) {
                auto victim = cache->begin();
                for (auto iter = cache->begin(); iter != cache->end(); ++iter) {
                    if (iter->second.lastUsed < victim->second.lastUsed) victim = iter;
                }
                cache->erase(victim);
            }
        }
    }
    bool adaptive = (model == MemoryModel::Auto);
    if (adaptive) model = MemoryModel::Contiguous;
    size_t predictedSpan = std::max(spanInfo.span, cells.size());

    // Heuristic: small tapes use contiguous doubling, medium tapes use
    // Fibonacci growth to trade memory for fewer reallocations, large tapes
    // switch to fixed-size paged allocation, and very large tapes use
    // OS-backed virtual memory when available.
    if (dynamicSize && adaptive) {
#if GOOF2_HAS_OS_VM
        if (predictedSpan > (1u << 28))
            model = MemoryModel::OSBacked;
        else
#endif
            if (predictedSpan > (1u << 24))
            model = MemoryModel::Paged;
        else if (predictedSpan > (1u << 16))
            model = MemoryModel::Fibonacci;
    }
    if (dynamicSize) {
        if (sparse) {
            term ? ret = executeImpl<CellT, true, true, true>(cells, cellPtr, code, optimize, eof,
                                                              model, adaptive, predictedSpan,
                                                              profile, cacheVec)
                 : ret = executeImpl<CellT, true, false, true>(cells, cellPtr, code, optimize, eof,
                                                               model, adaptive, predictedSpan,
                                                               profile, cacheVec);
        } else {
            term ? ret = executeImpl<CellT, true, true, false>(cells, cellPtr, code, optimize, eof,
                                                               model, adaptive, predictedSpan,
                                                               profile, cacheVec)
                 : ret = executeImpl<CellT, true, false, false>(cells, cellPtr, code, optimize, eof,
                                                                model, adaptive, predictedSpan,
                                                                profile, cacheVec);
        }
    } else {
        if (sparse) {
            term ? ret = executeImpl<CellT, false, true, true>(cells, cellPtr, code, optimize, eof,
                                                               model, adaptive, predictedSpan,
                                                               profile, cacheVec)
                 : ret = executeImpl<CellT, false, false, true>(cells, cellPtr, code, optimize, eof,
                                                                model, adaptive, predictedSpan,
                                                                profile, cacheVec);
        } else {
            term ? ret = executeImpl<CellT, false, true, false>(cells, cellPtr, code, optimize, eof,
                                                                model, adaptive, predictedSpan,
                                                                profile, cacheVec)
                 : ret = executeImpl<CellT, false, false, false>(cells, cellPtr, code, optimize,
                                                                 eof, model, adaptive,
                                                                 predictedSpan, profile, cacheVec);
        }
    }
    if (profile)
        profile->seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return ret;
}

template int goof2::execute<uint8_t>(std::vector<uint8_t>&, size_t&, std::string&, bool, int, bool,
                                     bool, goof2::MemoryModel, goof2::ProfileInfo*,
                                     goof2::InstructionCache*);
template int goof2::execute<uint16_t>(std::vector<uint16_t>&, size_t&, std::string&, bool, int,
                                      bool, bool, goof2::MemoryModel, goof2::ProfileInfo*,
                                      goof2::InstructionCache*);
template int goof2::execute<uint32_t>(std::vector<uint32_t>&, size_t&, std::string&, bool, int,
                                      bool, bool, goof2::MemoryModel, goof2::ProfileInfo*,
                                      goof2::InstructionCache*);
template int goof2::execute<uint64_t>(std::vector<uint64_t>&, size_t&, std::string&, bool, int,
                                      bool, bool, goof2::MemoryModel, goof2::ProfileInfo*,
                                      goof2::InstructionCache*);
