/*
    Goof - An optimizing brainfuck VM
    VM implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vm.hxx"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#define BOOST_REGEX_MAX_STATE_COUNT 1000000000  // Should be enough to parse anything
#include <boost/regex.hpp>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "simde/x86/avx2.h"
#include "simde/x86/sse2.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TZCNT32(x) __builtin_ctz((unsigned)(x))
#define LZCNT32(x) __builtin_clz((unsigned)(x))
#else
static inline unsigned TZCNT32(unsigned x) {
    unsigned i = 0;
    while (((x >> i) & 1u) == 0u) ++i;
    return i;
}
static inline unsigned LZCNT32(unsigned x) {
    unsigned i = 0;
    while (((x >> (31u - i)) & 1u) == 0u) ++i;
    return i;
}
#endif

template <unsigned Bytes>
static inline uint32_t strideMask32(unsigned step, unsigned phase) {
    if constexpr (Bytes == 1) {
        if (step == 2)
            return 0x55555555u << phase;
        if (step == 4)
            return 0x11111111u << phase;
        if (step == 8)
            return 0x01010101u << phase;
    }
    uint32_t m = 0;
    constexpr unsigned lanes = 32 / Bytes;
    for (unsigned i = 0; i < lanes; i++) {
        if (((i + phase) % step) == 0) {
            unsigned bit = i * Bytes;
            if constexpr (Bytes == 1)
                m |= (1u << bit);
            else if constexpr (Bytes == 2)
                m |= (3u << bit);
            else
                m |= (15u << bit);
        }
    }
    return m;
}

template <unsigned Bytes>
static inline uint16_t strideMask16(unsigned step, unsigned phase) {
    if constexpr (Bytes == 1) {
        if (step == 2)
            return uint16_t(0x5555u << phase);
        if (step == 4)
            return uint16_t(0x1111u << phase);
        if (step == 8)
            return uint16_t(0x0101u << phase);
    }
    uint16_t m = 0;
    constexpr unsigned lanes = 16 / Bytes;
    for (unsigned i = 0; i < lanes; i++) {
        if (((i + phase) % step) == 0) {
            unsigned bit = i * Bytes;
            if constexpr (Bytes == 1)
                m |= (uint16_t(1) << bit);
            else if constexpr (Bytes == 2)
                m |= (uint16_t(3) << bit);
            else
                m |= (uint16_t(15) << bit);
        }
    }
    return m;
}

template <unsigned Bytes>
static inline int compressMask32(int m) {
    if constexpr (Bytes == 1)
        return m;
    else if constexpr (Bytes == 2)
        return ((m >> 1) | m) & 0x55555555;
    else {
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        return m & 0x11111111;
    }
}

template <unsigned Bytes>
static inline int compressMask16(int m) {
    if constexpr (Bytes == 1)
        return m;
    else if constexpr (Bytes == 2)
        return ((m >> 1) | m) & 0x5555;
    else {
        m = ((m >> 1) | m);
        m = ((m >> 2) | m);
        return m & 0x1111;
    }
}

template <typename CellT>
static inline size_t simdScan0Fwd(const CellT* p, const CellT* end) {
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
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
        else
            m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
        m = compressMask32<Bytes>(m);
        if (m) {
            unsigned idx = TZCNT32((unsigned)m);
            return (size_t)((x - p) + idx / Bytes);
        }
    }
#else
    constexpr unsigned LANES = 16 / Bytes;
    while (((uintptr_t)x & 15u) && x < end) {
        if (*x == 0) return (size_t)(x - p);
        ++x;
    }
    const simde__m128i vz = simde_mm_setzero_si128();
    for (; x + LANES <= end; x += LANES) {
        simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)x);
        int m;
        if constexpr (Bytes == 1)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
        else if constexpr (Bytes == 2)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi16(v, vz));
        else
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi32(v, vz));
        m = compressMask16<Bytes>(m);
        if (m) {
            unsigned idx = TZCNT32((unsigned)m);
            return (size_t)((x - p) + idx / Bytes);
        }
    }
#endif
    while (x < end) {
        if (*x == 0) return (size_t)(x - p);
        ++x;
    }
    return (size_t)(end - p);
}

/*** step == 1 backward scan: last zero in [base,p], return distance back ***/
template <typename CellT>
static inline size_t simdScan0Back(const CellT* base, const CellT* p) {
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
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
        else
            m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
        m = compressMask32<Bytes>(m);
        if (m) {
            unsigned bit = 31u - (unsigned)LZCNT32((unsigned)m);
            unsigned lane = bit / Bytes;
            return (size_t)(p - (blk + lane));
        }
        x -= LANES;
    }
#else
    constexpr unsigned LANES = 16 / Bytes;
    while (((uintptr_t)(x - (LANES - 1)) & 15u) && x >= base) {
        if (*x == 0) return (size_t)(p - x);
        --x;
    }
    const simde__m128i vz = simde_mm_setzero_si128();
    while (x + 1 >= base + LANES) {
        const CellT* blk = x - (LANES - 1);
        simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)blk);
        int m;
        if constexpr (Bytes == 1)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
        else if constexpr (Bytes == 2)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi16(v, vz));
        else
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi32(v, vz));
        m = compressMask16<Bytes>(m);
        unsigned um = (unsigned)m & 0xFFFFu;
        if (um) {
            unsigned bit = 31u - (unsigned)LZCNT32(um);
            unsigned lane = bit / Bytes;
            return (size_t)(p - (blk + lane));
        }
        x -= LANES;
    }
#endif
    while (x >= base) {
        if (*x == 0) return (size_t)(p - x);
        --x;
    }
    return (size_t)(p - base + 1);
}

/*** tiny-stride forward scan: step in {2,4,8} ***/
template <unsigned Step, typename CellT>
static inline size_t simdScan0FwdStride(const CellT* p, const CellT* end, unsigned phase) {
    static_assert(Step == 2 || Step == 4 || Step == 8);
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    constexpr unsigned Mask = Step - 1;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
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
        else
            m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
        m = compressMask32<Bytes>(m);
        m &= (int)strideMask32<Bytes>(Step, phase);
        if (m) {
            unsigned idx = TZCNT32((unsigned)m);
            return (size_t)((x - p) + idx / Bytes);
        }
        phase = (phase + LANES) & Mask;
    }
#else
    constexpr unsigned LANES = 16 / Bytes;
    while (((uintptr_t)x & 15u) && x < end) {
        if (phase == 0 && *x == 0) return (size_t)(x - p);
        ++x;
        phase = (phase + 1) & Mask;
    }
    const simde__m128i vz = simde_mm_setzero_si128();
    for (; x + LANES <= end; x += LANES) {
        simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)x);
        int m;
        if constexpr (Bytes == 1)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
        else if constexpr (Bytes == 2)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi16(v, vz));
        else
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi32(v, vz));
        m = compressMask16<Bytes>(m);
        m &= (int)strideMask16<Bytes>(Step, phase);
        if (m) {
            unsigned idx = TZCNT32((unsigned)m);
            return (size_t)((x - p) + idx / Bytes);
        }
        phase = (phase + LANES) & Mask;
    }
#endif
    while (x < end) {
        if (phase == 0 && *x == 0) return (size_t)(x - p);
        ++x;
        phase = (phase + 1) & Mask;
    }
    return (size_t)(end - p);
}

/*** tiny-stride backward scan: step in {2,4,8} ***/
template <unsigned Step, typename CellT>
static inline size_t simdScan0BackStride(const CellT* base, const CellT* p, unsigned phaseAtP) {
    static_assert(Step == 2 || Step == 4 || Step == 8);
    const CellT* x = p;
    constexpr unsigned Bytes = sizeof(CellT);
    constexpr unsigned Mask = Step - 1;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
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
        else
            m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi32(v, vz));
        m = compressMask32<Bytes>(m);
        m &= (int)strideMask32<Bytes>(Step, lane0);
        if (m) {
            unsigned bit = 31u - (unsigned)LZCNT32((unsigned)m);
            unsigned lane = bit / Bytes;
            return (size_t)(p - (blk + lane));
        }
        x -= LANES;
    }
#else
    constexpr unsigned LANES = 16 / Bytes;
    while (((uintptr_t)(x - (LANES - 1)) & 15u) && x >= base) {
        if (phaseAtP == 0 && *x == 0) return (size_t)(p - x);
        --x;
        phaseAtP = (phaseAtP + Step - 1) & Mask;
    }
    const simde__m128i vz = simde_mm_setzero_si128();
    while (x + 1 >= base + LANES) {
        const CellT* blk = x - (LANES - 1);
        unsigned lane0 = (unsigned)(blk - base) & Mask;
        simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)blk);
        int m;
        if constexpr (Bytes == 1)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
        else if constexpr (Bytes == 2)
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi16(v, vz));
        else
            m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi32(v, vz));
        m = compressMask16<Bytes>(m);
        m &= (int)strideMask16<Bytes>(Step, lane0);
        unsigned um = (unsigned)m & 0xFFFFu;
        if (um) {
            unsigned bit = 31u - (unsigned)LZCNT32(um);
            unsigned lane = bit / Bytes;
            return (size_t)(p - (blk + lane));
        }
        x -= LANES;
    }
#endif
    while (x >= base) {
        if (phaseAtP == 0 && *x == 0) return (size_t)(p - x);
        --x;
        phaseAtP = (phaseAtP + Step - 1) & Mask;
    }
    return (size_t)(p - base + 1);
}

struct instruction {
    const void* jump;
    int32_t data;
    int16_t auxData;
    int16_t offset;
};

int32_t fold(std::string_view code, size_t& i, char match) {
    int32_t count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) {
        ++i;
        ++count;
    }
    return count;
}

std::string processBalanced(std::string_view s, char no1, char no2) {
    const auto total = std::count(s.begin(), s.end(), no1) - std::count(s.begin(), s.end(), no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

enum class MemoryModel { Contiguous, Paged, Fibonacci };

template <typename CellT, bool Dynamic, bool Term>
int executeImpl(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, size_t maxTs, MemoryModel model) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    std::vector<instruction> instructions;
    {
        enum insType {
            ADD_SUB,
            SET,
            PTR_MOV,
            JMP_ZER,
            JMP_NOT_ZER,
            PUT_CHR,
            RAD_CHR,

            CLR,
            MUL_CPY,
            SCN_RGT,
            SCN_LFT,

            END,
        };

        static void* jtable[] = {&&_ADD_SUB,     &&_SET,     &&_PTR_MOV, &&_JMP_ZER,
                                 &&_JMP_NOT_ZER, &&_PUT_CHR, &&_RAD_CHR, &&_CLR,
                                 &&_MUL_CPY,     &&_SCN_RGT, &&_SCN_LFT, &&_END};

        int copyloopCounter = 0;
        std::vector<int> copyloopMap;

        int scanloopCounter = 0;
        std::vector<int> scanloopMap;

        if (optimize) {
            code = boost::regex_replace(code, boost::basic_regex(R"([^\+\-\>\<\.\,\]\[])"), "");

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '+', '-');
            });
            code = boost::regex_replace(code, boost::basic_regex(R"([><]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '>', '<');
            });

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]*(?:\[[+-]+\])+)"), "C");

            code =
                boost::regex_replace(code, boost::basic_regex(R"(\[>+\]|\[<+\])"), [&](auto& what) {
                    const auto current = what.str();
                    const auto count = std::count(current.begin(), current.end(), '>') -
                                       std::count(current.begin(), current.end(), '<');
                    scanloopMap.push_back(std::abs(count));
                    if (count > 0)
                        return "R";
                    else
                        return "L";
                });

            code = boost::regex_replace(code, boost::basic_regex(R"([+\-C]+,)"), ",");

            code = boost::regex_replace(
                code,
                boost::basic_regex(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])"),
                [&](auto& what) {
                    int numOfCopies = 0;
                    int offset = 0;
                    const std::string whole = what.str();
                    const std::string current = what[1].str() + what[2].str();

                    if (std::count(whole.begin(), whole.end(), '>') -
                            std::count(whole.begin(), whole.end(), '<') ==
                        0) {
                        boost::match_results<std::string::const_iterator> whatL;
                        auto start = current.cbegin();
                        auto end = current.cend();
                        while (boost::regex_search(start, end, whatL,
                                                   boost::basic_regex(R"([<>]+[+-]+)"))) {
                            offset += -std::count(whatL[0].begin(), whatL[0].end(), '<') +
                                      std::count(whatL[0].begin(), whatL[0].end(), '>');
                            copyloopMap.push_back(offset);
                            copyloopMap.push_back(
                                std::count(whatL[0].begin(), whatL[0].end(), '+') -
                                std::count(whatL[0].begin(), whatL[0].end(), '-'));
                            numOfCopies++;
                            start = whatL[0].second;
                        }
                        return std::string(numOfCopies, 'P') + "C";
                    } else {
                        return whole;
                    }
                });

            if constexpr (!Term)
                code = boost::regex_replace(code,
                                            boost::basic_regex(R"((?:^|(?<=[RL\]])|C+)([\+\-]+))"),
                                            "S${1}");  // We can't really assume in term
        }

        std::vector<size_t> braceStack;
        int16_t offset = 0;
        bool set = false;
        instructions.reserve(code.length());
        std::vector<uint8_t> ops;
        ops.reserve(code.length());

        auto emit = [&](insType op, instruction inst) {
            if (!instructions.empty() && instructions.back().offset == inst.offset) {
                auto& last = instructions.back();
                insType lastOp = static_cast<insType>(ops.back());
                bool lastIsWrite = lastOp == insType::ADD_SUB || lastOp == insType::SET ||
                                   lastOp == insType::CLR;
                bool newIsWrite = op == insType::ADD_SUB || op == insType::SET ||
                                  op == insType::CLR;
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
                            ops.pop_back();
                            instructions.push_back(instruction{nullptr,
                                                              static_cast<int32_t>(
                                                                  static_cast<CellT>(inst.data)),
                                                              0, inst.offset});
                            ops.push_back(static_cast<uint8_t>(insType::SET));
                            return;
                        }
                    } else {
                        instructions.pop_back();
                        ops.pop_back();
                        instructions.push_back(inst);
                        ops.push_back(static_cast<uint8_t>(op));
                        return;
                    }
                }
            }
            instructions.push_back(inst);
            ops.push_back(static_cast<uint8_t>(op));
        };

#define MOVEOFFSET()                                                          \
    if (offset) [[likely]] {                                                  \
        emit(insType::PTR_MOV, instruction{nullptr, offset, 0, 0}); \
        offset = 0;                                                           \
    }

        for (size_t i = 0; i < code.length(); i++) {
            switch (code[i]) {
                case '+': {
                    const insType op = set ? insType::SET : insType::ADD_SUB;
                    emit(op, instruction{nullptr, fold(code, i, '+'), 0, offset});
                    set = false;
                    break;
                }
                case '-': {
                    const int32_t folded = -fold(code, i, '-');
                    const insType op = set ? insType::SET : insType::ADD_SUB;
                    emit(op, instruction{nullptr,
                                         set ? static_cast<int32_t>(static_cast<CellT>(folded))
                                             : folded,
                                         0, offset});
                    set = false;
                    break;
                }
                case '>':
                    offset += static_cast<int16_t>(fold(code, i, '>'));
                    break;
                case '<':
                    offset -= static_cast<int16_t>(fold(code, i, '<'));
                    break;
                case '[':
                    MOVEOFFSET();
                    braceStack.push_back(instructions.size());
                    emit(insType::JMP_ZER, instruction{nullptr, 0, 0, 0});
                    break;
                case ']': {
                    if (!braceStack.size()) return 1;

                    MOVEOFFSET();
                    const int start = braceStack.back();
                    const int sizeminstart = instructions.size() - start;
                    braceStack.pop_back();
                    instructions[start].data = sizeminstart;
                    emit(insType::JMP_NOT_ZER,
                         instruction{nullptr, sizeminstart, 0, 0});
                    break;
                }
                case '.':
                    emit(insType::PUT_CHR,
                         instruction{nullptr, fold(code, i, '.'), 0, offset});
                    break;
                case ',':
                    emit(insType::RAD_CHR, instruction{nullptr, 0, 0, offset});
                    break;
                case 'C':
                    emit(insType::CLR, instruction{nullptr, 0, 0, offset});
                    break;
                case 'P':
                    emit(insType::MUL_CPY,
                         instruction{nullptr, copyloopMap[copyloopCounter++],
                                     static_cast<int16_t>(copyloopMap[copyloopCounter++]), offset});
                    break;
                case 'R':
                    MOVEOFFSET();
                    emit(insType::SCN_RGT,
                         instruction{nullptr, scanloopMap[scanloopCounter++], 0, 0});
                    break;
                case 'L':
                    MOVEOFFSET();
                    emit(insType::SCN_LFT,
                         instruction{nullptr, scanloopMap[scanloopCounter++], 0, 0});
                    break;
                case 'S':
                    set = true;
                    break;
            }
        }
        MOVEOFFSET();
        emit(insType::END, instruction{nullptr, 0, 0, 0});

        if (!braceStack.empty()) return 2;

        instructions.shrink_to_fit();
        for (size_t i = 0; i < instructions.size(); ++i) {
            instructions[i].jump = jtable[ops[i]];
        }
    }

    auto cell = cells.data() + cellPtr;
    auto insp = instructions.data();
    auto cellBase = cells.data();

    std::array<char, 1024> buffer = {0};

    constexpr size_t PAGE_SIZE = 1u << 16;  // 64KB pages for paged growth
    size_t fibA = cells.size(), fibB = cells.size();
    auto ensure = [&](ptrdiff_t currentCell, ptrdiff_t neededIndex) {
        size_t needed = static_cast<size_t>(neededIndex + 1);
        if (maxTs && needed > maxTs) return false;
        switch (model) {
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
            default:
                while (cells.size() < needed) cells.resize(cells.size() * 2);
                break;
        }
        cellBase = cells.data();
        cell = cellBase + currentCell;
        return true;
    };

    // Really not my fault if we die here
    goto * insp->jump;

#define LOOP()  \
    insp++;   \
    goto *insp->jump
// This is hell, and also, it probably would've been easier to not use pointers as i see now, but oh
// well
#define EXPAND_IF_NEEDED()                                                \
    if (insp->offset > 0) {                                               \
        const ptrdiff_t currentCell = cell - cellBase;                    \
        const ptrdiff_t neededIndex = currentCell + insp->offset;         \
        if (neededIndex >= static_cast<ptrdiff_t>(cells.size())) {        \
            if (!ensure(currentCell, neededIndex)) {                      \
                cellPtr = static_cast<size_t>(currentCell);               \
                std::cerr << "cell pointer moved beyond limit"           \
                          << std::endl;                                   \
                return -1;                                                \
            }                                                             \
        }                                                                 \
    }
#define OFFCELL() *(cell + insp->offset)
#define OFFCELLP() *(cell + insp->offset + insp->data)
_ADD_SUB:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() += insp->data;
    LOOP();

_SET:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() = insp->data;
    LOOP();

_PTR_MOV: {
    const ptrdiff_t currentCell = cell - cellBase;
    const ptrdiff_t newIndex = currentCell + insp->data;
    if (newIndex < 0) {
        cellPtr = currentCell;
        std::cerr << "cell pointer moved before start" << std::endl;
        return -1;
    }
    if (newIndex >= static_cast<ptrdiff_t>(cells.size())) {
        if constexpr (Dynamic) {
            if (!ensure(currentCell, newIndex)) {
                cellPtr = currentCell;
                std::cerr << "cell pointer moved beyond limit" << std::endl;
                return -1;
            }
        } else {
            cellPtr = currentCell;
            std::cerr << "cell pointer moved beyond end" << std::endl;
            return -1;
        }
    }
    cell = cellBase + newIndex;
    LOOP();
}

_JMP_ZER:
    if (!*cell) [[unlikely]]
        insp += insp->data;
    LOOP();

_JMP_NOT_ZER:
    if (*cell) [[likely]]
        insp -= insp->data;
    LOOP();

_PUT_CHR:
    std::memset(buffer.data(), static_cast<unsigned char>(OFFCELL()), buffer.size());

    size_t left = static_cast<size_t>(insp->data);
    while (left) {
        const size_t chunk = std::min(left, buffer.size());
        std::cout.write(buffer.data(), chunk);
        left -= chunk;
    }
    LOOP();

_RAD_CHR:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    if (const int in = std::cin.get(); in == EOF) {
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

_MUL_CPY:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELLP() += OFFCELL() * insp->auxData;
    LOOP();

_SCN_RGT: {
    const unsigned step = static_cast<unsigned>(insp->data);

    // small pre-grow to cut resize churn during long scans
    if constexpr (Dynamic) {
        while ((cell - cellBase) + 64 >= static_cast<ptrdiff_t>(cells.size())) {
            const ptrdiff_t rel = cell - cellBase;
            if (!ensure(rel, rel + 64)) {
                cellPtr = static_cast<size_t>(rel);
                std::cerr << "cell pointer moved beyond limit" << std::endl;
                return -1;
            }
        }
    }

    for (;;) {
        CellT* const end = cells.data() + cells.size();
        size_t off;
        if (step == 1) {
            off = simdScan0Fwd<CellT>(cell, end);
        } else if (step == 2) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 1u;
            off = simdScan0FwdStride<2, CellT>(cell, end, phase);
        } else if (step == 4) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 3u;
            off = simdScan0FwdStride<4, CellT>(cell, end, phase);
        } else if (step == 8) {
            unsigned phase = static_cast<unsigned>(cell - cellBase) & 7u;
            off = simdScan0FwdStride<8, CellT>(cell, end, phase);
        } else {
            // scalar fallback for arbitrary step
            off = 0;
            while (cell + off < end && *(cell + off) != 0) off += step;
        }

        cell += off;

        if (cell < end) {
            // found zero
            LOOP();
        }

        if constexpr (Dynamic) {
            // grow and continue the scan into new space
            const ptrdiff_t rel = cell - cellBase;
            if (!ensure(rel, rel)) {
                cellPtr = static_cast<size_t>(rel);
                std::cerr << "cell pointer moved beyond limit" << std::endl;
                return -1;
            }
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

    if (cell < cellBase) {
        cellPtr = 0;
        std::cerr << "cell pointer moved before start" << std::endl;
        return -1;
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
        size_t back = simdScan0BackStride<2, CellT>(cellBase, cell, phaseAtP);
        cell -= back;
        LOOP();
    } else if (step == 4) {
        unsigned phaseAtP = static_cast<unsigned>(cell - cellBase) & 3u;
        size_t back = simdScan0BackStride<4, CellT>(cellBase, cell, phaseAtP);
        cell -= back;
        LOOP();
    } else if (step == 8) {
        unsigned phaseAtP = static_cast<unsigned>(cell - cellBase) & 7u;
        size_t back = simdScan0BackStride<8, CellT>(cellBase, cell, phaseAtP);
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
        while (cell >= cellBase && *cell != 0) {
            if ((cell - cellBase) < static_cast<ptrdiff_t>(step)) break;
            cell -= step;
        }
        if (cell < cellBase) {
            cell = cellBase;
            cellPtr = 0;
            std::cerr << "cell pointer moved before start" << std::endl;
            return -1;
        }
        LOOP();
    }
}

_END:
    cellPtr = cell - cellBase;
    return 0;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

template <typename CellT>
int bfvmcpp::execute(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                     int eof, bool dynamicSize, size_t maxTs, bool term) {
    int ret = 0;
    MemoryModel model = MemoryModel::Contiguous;
    // Heuristic: small tapes use contiguous doubling, medium tapes use
    // Fibonacci growth to trade memory for fewer reallocations, and very
    // large tapes switch to fixed-size paged allocation.
    if (dynamicSize) {
        if (cells.size() > (1u << 24))
            model = MemoryModel::Paged;
        else if (cells.size() > (1u << 16))
            model = MemoryModel::Fibonacci;
    }
    if (dynamicSize) {
        term ? ret = executeImpl<CellT, true, true>(cells, cellPtr, code, optimize, eof, maxTs,
                                                   model)
             : ret = executeImpl<CellT, true, false>(cells, cellPtr, code, optimize, eof, maxTs,
                                                    model);
    } else {
        term ? ret = executeImpl<CellT, false, true>(cells, cellPtr, code, optimize, eof, maxTs,
                                                    model)
             : ret = executeImpl<CellT, false, false>(cells, cellPtr, code, optimize, eof, maxTs,
                                                     model);
    }
    return ret;
}

template int bfvmcpp::execute<uint8_t>(std::vector<uint8_t>&, size_t&, std::string&, bool, int,
                                       bool, size_t, bool);
template int bfvmcpp::execute<uint16_t>(std::vector<uint16_t>&, size_t&, std::string&, bool, int,
                                        bool, size_t, bool);
template int bfvmcpp::execute<uint32_t>(std::vector<uint32_t>&, size_t&, std::string&, bool, int,
                                        bool, size_t, bool);
