#include "vm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#define BOOST_REGEX_MAX_STATE_COUNT 1000000000 // Should be enough to parse anything
#include <boost/regex.hpp>
#include "rang.hxx"
#include "simde/x86/sse2.h"
#include "simde/x86/avx2.h"

#if defined(__GNUC__) || defined(__clang__)
#define TZCNT32(x)  __builtin_ctz((unsigned)(x))
#define LZCNT32(x)  __builtin_clz((unsigned)(x))
#else
static inline unsigned TZCNT32(unsigned x){ unsigned i=0; while(((x>>i)&1u)==0u) ++i; return i; }
static inline unsigned LZCNT32(unsigned x){ unsigned i=0; while(((x>>(31u-i))&1u)==0u) ++i; return i; }
#endif

static inline uint32_t stride_mask_32(unsigned step, unsigned phase)
{
    uint32_t m=0; for (unsigned i=0;i<32;i++) if(((i+phase)%step)==0) m|=(1u<<i); return m;
}

static inline uint16_t stride_mask_16(unsigned step, unsigned phase)
{
    uint16_t m=0; for (unsigned i=0;i<16;i++) if(((i+phase)%step)==0) m|=(uint16_t(1)<<i); return m;
}

static inline unsigned posmod(ptrdiff_t x, unsigned m)
{
    ptrdiff_t r = x % (ptrdiff_t)m; if (r<0) r += m; return (unsigned)r;
}

static inline size_t simd_scan0_fwd(const uint8_t* p, const uint8_t* end){
  const uint8_t* x = p;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
  while (((uintptr_t)x & 31u) && x < end){ if (*x==0) return (size_t)(x-p); ++x; }
  const simde__m256i vz = simde_mm256_setzero_si256();
  for (; x + 32 <= end; x += 32){
    simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)x);
    int m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
    if (m){ unsigned idx = TZCNT32((unsigned)m); return (size_t)((x-p)+idx); }
  }
#else
  while (((uintptr_t)x & 15u) && x < end){ if (*x==0) return (size_t)(x-p); ++x; }
  const simde__m128i vz = simde_mm_setzero_si128();
  for (; x + 16 <= end; x += 16){
    simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)x);
    int m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
    if (m){ unsigned idx = TZCNT32((unsigned)m); return (size_t)((x-p)+idx); }
  }
#endif
  while (x < end){ if (*x==0) return (size_t)(x-p); ++x; }
  return (size_t)(end - p);
}

/*** step == 1 backward scan: last zero in [base,p], return distance back ***/
static inline size_t simd_scan0_back(const uint8_t* base, const uint8_t* p){
  const uint8_t* x = p;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
  while (((uintptr_t)(x-31) & 31u) && x >= base){ if (*x==0) return (size_t)(p-x); --x; }
  const simde__m256i vz = simde_mm256_setzero_si256();
  while (x + 1 >= base + 32){
    const uint8_t* blk = x - 31;
    simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)blk);
    int m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
    if (m){ unsigned idx = 31u - (unsigned)LZCNT32((unsigned)m); return (size_t)(p - (blk + idx)); }
    x -= 32;
  }
#else
  while (((uintptr_t)(x-15) & 15u) && x >= base){ if (*x==0) return (size_t)(p-x); --x; }
  const simde__m128i vz = simde_mm_setzero_si128();
  while (x + 1 >= base + 16){
    const uint8_t* blk = x - 15;
    simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)blk);
    int m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
    if (m){ unsigned idx = 31u - (unsigned)LZCNT32((unsigned)m) - 16u; return (size_t)(p - (blk + idx)); }
    x -= 16;
  }
#endif
  while (x >= base){ if (*x==0) return (size_t)(p-x); --x; }
  return (size_t)(p - base + 1);
}

/*** tiny-stride forward scan: step in {2,4,8} ***/
static inline size_t simd_scan0_fwd_stride(const uint8_t* p, const uint8_t* end,
                                           unsigned step, unsigned phase){
  const uint8_t* x = p;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
  while (((uintptr_t)x & 31u) && x < end){
    if ((phase % step)==0 && *x==0) return (size_t)(x-p);
    ++x; if (++phase == step) phase = 0;
  }
  const simde__m256i vz = simde_mm256_setzero_si256();
  for (; x + 32 <= end; x += 32){
    simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)x);
    int m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
    m &= (int)stride_mask_32(step, phase);
    if (m){ unsigned idx = TZCNT32((unsigned)m); return (size_t)((x-p)+idx); }
    phase = (phase + 32) % step;
  }
#else
  while (((uintptr_t)x & 15u) && x < end){
    if ((phase % step)==0 && *x==0) return (size_t)(x-p);
    ++x; if (++phase == step) phase = 0;
  }
  const simde__m128i vz = simde_mm_setzero_si128();
  for (; x + 16 <= end; x += 16){
    simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)x);
    int m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
    m &= (int)stride_mask_16(step, phase);
    if (m){ unsigned idx = TZCNT32((unsigned)m); return (size_t)((x-p)+idx); }
    phase = (phase + 16) % step;
  }
#endif
  while (x < end){
    if ((phase % step)==0 && *x==0) return (size_t)(x-p);
    ++x; if (++phase == step) phase = 0;
  }
  return (size_t)(end - p);
}

/*** tiny-stride backward scan: step in {2,4,8} ***/
static inline size_t simd_scan0_back_stride(const uint8_t* base, const uint8_t* p,
                                            unsigned step, unsigned phase_at_p){
  const uint8_t* x = p;
#if SIMDE_NATURAL_VECTOR_SIZE_GE(256)
  while (((uintptr_t)(x-31) & 31u) && x >= base){
    if ((phase_at_p % step)==0 && *x==0) return (size_t)(p-x);
    --x; phase_at_p = (phase_at_p + step - 1) % step;
  }
  const simde__m256i vz = simde_mm256_setzero_si256();
  while (x + 1 >= base + 32){
    const uint8_t* blk = x - 31;
    unsigned lane0 = posmod((ptrdiff_t)(blk - base), step);
    simde__m256i v = simde_mm256_loadu_si256((const simde__m256i*)blk);
    int m = simde_mm256_movemask_epi8(simde_mm256_cmpeq_epi8(v, vz));
    m &= (int)stride_mask_32(step, lane0);
    if (m){ unsigned idx = 31u - (unsigned)LZCNT32((unsigned)m); return (size_t)(p - (blk + idx)); }
    x -= 32;
  }
#else
  while (((uintptr_t)(x-15) & 15u) && x >= base){
    if ((phase_at_p % step)==0 && *x==0) return (size_t)(p-x);
    --x; phase_at_p = (phase_at_p + step - 1) % step;
  }
  const simde__m128i vz = simde_mm_setzero_si128();
  while (x + 1 >= base + 16){
    const uint8_t* blk = x - 15;
    unsigned lane0 = posmod((ptrdiff_t)(blk - base), step);
    simde__m128i v = simde_mm_loadu_si128((const simde__m128i*)blk);
    int m = simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(v, vz));
    m &= (int)stride_mask_16(step, lane0);
    if (m){ unsigned idx = 31u - (unsigned)LZCNT32((unsigned)m) - 16u; return (size_t)(p - (blk + idx)); }
    x -= 16;
  }
#endif
  while (x >= base){
    if ((phase_at_p % step)==0 && *x==0) return (size_t)(p-x);
    --x; phase_at_p = (phase_at_p + step - 1) % step;
  }
  return (size_t)(p - base + 1);
}

using namespace rang;
struct instruction
{
    const void* jump;
    int32_t data;
    const int16_t auxData;
    const int16_t offset;
};

size_t fold(std::string_view code, size_t& i, char match)
{
    size_t count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) { i++; count++; }
    return count;
}

std::string processBalanced(std::string_view s, char no1, char no2)
{
    const auto total = std::count(s.begin(), s.end(), no1) - std::count(s.begin(), s.end(), no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}
template<bool Dynamic, bool Term>
int _execute(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize, int eof)
{
    std::vector<instruction> instructions;
    {
        enum insType
        {
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

        static void* jtable[] = {
            &&_ADD_SUB, &&_SET, &&_PTR_MOV, &&_JMP_ZER, &&_JMP_NOT_ZER,
            &&_PUT_CHR, &&_RAD_CHR, &&_CLR, &&_MUL_CPY, &&_SCN_RGT, &&_SCN_LFT, &&_END
        };

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

            code = boost::regex_replace(code, boost::basic_regex(R"(\[>+\]|\[<+\])"), [&](auto& what) {
                const auto current = what.str();
                const auto count = std::count(current.begin(), current.end(), '>') - std::count(current.begin(), current.end(), '<');
                scanloopMap.push_back(std::abs(count));
                if (count > 0)
                    return "R";
                else
                    return "L";
                });

            code = boost::regex_replace(code, boost::basic_regex(R"([+\-C]+,)"), ",");

            code = boost::regex_replace(code, boost::basic_regex(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])"), [&](auto& what) {
                int numOfCopies = 0;
                int offset = 0;
                const std::string whole = what.str();
                const std::string current = what[1].str() + what[2].str();

                if (std::count(whole.begin(), whole.end(), '>') - std::count(whole.begin(), whole.end(), '<') == 0) {
                    boost::match_results<std::string::const_iterator> whatL;
                    auto start = current.cbegin();
                    auto end = current.cend();
                    while (boost::regex_search(start, end, whatL, boost::basic_regex(R"([<>]+[+-]+)"))) {
                        offset += -std::count(whatL[0].begin(), whatL[0].end(), '<') + std::count(whatL[0].begin(), whatL[0].end(), '>');
                        copyloopMap.push_back(offset);
                        copyloopMap.push_back(std::count(whatL[0].begin(), whatL[0].end(), '+') - std::count(whatL[0].begin(), whatL[0].end(), '-'));
                        numOfCopies++;
                        start = whatL[0].second;
                    }
                    return std::string(numOfCopies, 'P') + "C";
                } else {
                    return whole;
                }
                });

            if constexpr (!Term) code = boost::regex_replace(code, boost::basic_regex(R"((?:^|(?<=[RL\]])|C+)([\+\-]+))"), "S${1}"); // We can't really assume in term
        }

        std::vector<size_t> braceStack;
        int16_t offset = 0;
        bool set = false;
        instructions.reserve(code.length());
        #define MOVEOFFSET() if (offset) [[likely]] { instructions.push_back(instruction{ jtable[insType::PTR_MOV], offset, 0, 0}); offset = 0;}
        for (size_t i = 0; i < code.length(); i++) {
            switch (code[i]) {
            case '+':
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], fold(code, i, '+'), 0, offset });
                set = false;
                break;
            case '-':
            {               
                const auto folded = -fold(code, i, '-');
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], set ? (int32_t)(uint8_t)folded : folded, 0, offset});
                set = false;
                break;
            }
            case '>':
                offset += fold(code, i, '>');
                break;
            case '<':
                offset -= fold(code, i, '<');
                break;
            case '[':
                MOVEOFFSET();
                braceStack.push_back(instructions.size());
                instructions.push_back(instruction{ jtable[insType::JMP_ZER], 0, 0, 0 });
                break;
            case ']':
            {
                if (!braceStack.size())
                    return 1;

                MOVEOFFSET();
                const int start = braceStack.back();
                const int sizeminstart = instructions.size() - start;
                braceStack.pop_back();
                instructions[start].data = sizeminstart;
                instructions.push_back(instruction{ jtable[insType::JMP_NOT_ZER], sizeminstart, 0, 0 });
                break;
            }
            case '.':
                instructions.push_back(instruction{ jtable[insType::PUT_CHR], fold(code, i, '.'), 0, offset });
                break;
            case ',':
                instructions.push_back(instruction{ jtable[insType::RAD_CHR], 0, 0, offset });
                break;
            case 'C':
                instructions.push_back(instruction{ jtable[insType::CLR], 0, 0, offset });
                break;
            case 'P':
                instructions.push_back(instruction{ jtable[insType::MUL_CPY], copyloopMap[copyloopCounter++], (int16_t)copyloopMap[copyloopCounter++], offset });
                break;        
            case 'R':
                MOVEOFFSET()
                instructions.push_back(instruction{ jtable[insType::SCN_RGT], scanloopMap[scanloopCounter++], 0, 0 });
                break;
            case 'L':
                MOVEOFFSET()
                instructions.push_back(instruction{ jtable[insType::SCN_LFT], scanloopMap[scanloopCounter++], 0, 0 });
                break;
            case 'S':
                set = true;
                break;
            }
        }
        MOVEOFFSET()
        instructions.push_back(instruction{ jtable[insType::END], 0, 0, 0 });

        if (!braceStack.empty())
            return 2;

        instructions.shrink_to_fit();
    }
    
    auto cell = cells.data() + cellptr;
    auto insp = instructions.data();
    auto cellBase = cells.data();
    unsigned long long totalExecuted = 0;

    std::array<char, 1024> buffer = { 0 };

    // Really not my fault if we die here
    goto *insp->jump;

#define LOOP() totalExecuted++; insp++; goto *insp->jump
// This is hell, and also, it probably would've been easier to not use pointers as i see now, but oh well
#define EXPAND_IF_NEEDED() if (const ptrdiff_t currentCell = cell - cellBase; insp->offset > 0 && currentCell+insp->offset >= cells.size()) {cells.resize(cells.size() * 2);cellBase = cells.data();cell = &cells[currentCell];}
#define OFFCELL() *(cell+insp->offset)
#define OFFCELLP() *(cell+insp->offset+insp->data)
    _ADD_SUB:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() += insp->data;
    LOOP();

    _SET:
    if constexpr (Dynamic) EXPAND_IF_NEEDED()
    OFFCELL() = insp->data;
    LOOP();

    _PTR_MOV:
    if constexpr (Dynamic) {
        if (const ptrdiff_t currentCell = cell - cellBase; insp->data > 0 && (currentCell + insp->data) >= cells.size()) {
            cells.resize(cells.size() * 2);
            cellBase = cells.data();
            cell = &cells[currentCell];
        }
    }
    cell += insp->data;
    LOOP();

    _JMP_ZER:
    if (!*cell) [[unlikely]] insp += insp->data;
    LOOP();

    _JMP_NOT_ZER:
    if (*cell) [[likely]] insp -= insp->data;
    LOOP();

    _PUT_CHR:
    std::memset(buffer.data(), OFFCELL(), buffer.size());

    size_t left = insp->data;
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
            OFFCELL() = 255;
            break;
        default:
            __builtin_unreachable();
        }
    } else {
        OFFCELL() = in;
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

    _SCN_RGT:
    {
        const unsigned step = (unsigned)insp->data;

        // small pre-grow to cut resize churn during long scans
        if constexpr (Dynamic) {
            while ((cell - cellBase) + 64 >= (ptrdiff_t)cells.size()) {
                const ptrdiff_t rel = cell - cellBase;
                cells.resize(cells.size() * 2);
                cellBase = cells.data();
                cell = cellBase + rel;
            }
        }

        for (;;) {
            uint8_t* const end = cells.data() + cells.size();
            size_t off;
            if (step == 1) {
                off = simd_scan0_fwd(cell, end);
            } else if (step == 2 || step == 4 || step == 8) {
                const unsigned phase = posmod((ptrdiff_t)(cell - cellBase), step);
                off = simd_scan0_fwd_stride(cell, end, step, phase);
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
                cells.resize(cells.size() * 2);
                cellBase = cells.data();
                cell = cellBase + rel;
                continue;
            } else {
                LOOP();
            }
        }
    }

    _SCN_LFT:
    {
        const unsigned step = (unsigned)insp->data;

        if (cell < cellBase) { LOOP(); }

        if (step == 1) {
            size_t back = simd_scan0_back(cellBase, cell);
            cell -= back;
            LOOP();
        } else if (step == 2 || step == 4 || step == 8) {
            const unsigned phase_at_p = posmod((ptrdiff_t)(cell - cellBase), step);
            size_t back = simd_scan0_back_stride(cellBase, cell, step, phase_at_p);
            cell -= back;
            LOOP();
        } else {
            // scalar fallback for arbitrary step
            while (cell >= cellBase && *cell != 0) {
                if ((cell - cellBase) < (ptrdiff_t)step) break;
                cell -= step;
            }
            LOOP();
        }
    }

    _END:
    cellptr = cell - cellBase;
    return 0;
}

int bfvmcpp::execute(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize, int eof, bool dynamicSize, bool term = false)
{
    int ret = 0;
    if (dynamicSize) term ? ret = _execute<true, true>(cells, cellptr, code, optimize, eof)
                          : ret = _execute<true, false>(cells, cellptr, code, optimize, eof);
    else             term ? ret = _execute<false, true>(cells, cellptr, code, optimize, eof)
                          : ret = _execute<false, false>(cells, cellptr, code, optimize, eof);
    return ret;
}
