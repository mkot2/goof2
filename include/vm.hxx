/*
    Goof2 - An optimizing brainfuck VM
    VM API declarations
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#define GOOF2_DEFAULT_EOF_BEHAVIOUR 0
#define GOOF2_DYNAMIC_CELLS_SIZE 1
#define GOOF2_OPTIMIZE 1
#define GOOF2_DEFAULT_SAVE_STATE 0
#define GOOF2_TAPE_WARN_BYTES (1ull << 30)  // 1 GiB
// Hard limit to prevent uncontrolled memory allocation from user inputs.
// Requests exceeding this limit are rejected by the CLI/REPL.
#define GOOF2_TAPE_MAX_BYTES (1ull << 31)  // 2 GiB

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define GOOF2_HAS_OS_VM 1
#else
#define GOOF2_HAS_OS_VM 0
#endif

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

enum class insType : uint8_t {
    ADD_SUB,
    SET,
    PTR_MOV,
    JMP_ZER,
    JMP_NOT_ZER,
    PUT_CHR,
    RAD_CHR,
    CLR,
    CLR_RNG,
    MUL_CPY,
    SCN_RGT,
    SCN_LFT,
    SCN_CLR_RGT,
    SCN_CLR_LFT,
    END,
};

struct instruction {
    const void* jump;
    int32_t data;
    int16_t auxData;
    int16_t offset;
    insType op = insType{};
};

namespace goof2 {
#if GOOF2_HAS_OS_VM
extern void* (*os_alloc)(size_t);
extern void (*os_free)(void*, size_t);
#endif

enum class MemoryModel { Auto, Contiguous, Fibonacci, Paged, OSBacked };

struct ProfileInfo {
    std::uint64_t instructions = 0;
    double seconds = 0.0;
    std::vector<std::uint64_t> loopCounts{};
    std::uint64_t heapBytes = 0;
};

struct CacheEntry {
    std::string source;
    std::vector<instruction> instructions;
    std::list<size_t>::iterator usageIter;
    bool sparse = false;
};

using InstructionCache = std::unordered_map<size_t, CacheEntry>;

using LoopCache = std::unordered_map<std::uint64_t, std::vector<instruction>>;

LoopCache& getLoopCache();
void clearLoopCache();

/// @brief Only function you should use in your code. For now, it always prints to stdout.
/// @tparam CellT Cell width type (uint8_t, uint16_t, uint32_t, uint64_t)
/// @param cells Vector of cells of type CellT.
/// @param cellPtr
/// @param code Remember that this will be modified, so if you need to do something else with your
/// plaintext code, make a copy.
/// @param optimize Enable optimizations (highly recommended). Set it here as an override, for the
/// default state, check the GOOF2_OPTIMIZE define.
/// @param eof EOF behaviour. 0 = cell unchanged, 1 = set to 0, 2 = set to 255. Check
/// GOOF2_DEFAULT_EOF_BEHAVIOUR.
/// @param dynamicSize Allow dynamic resizing of the cells vector. Disable if you want, for example,
/// a constant number of decimals for a calculation, constant cell vector size. OOB on fixed size is
/// currently not handled.
/// @param term A few tweaks necessary to make it operable multiple times on the same cells. Check
/// GOOF2_DEFAULT_SAVE_STATE.
/// @param model Memory allocation strategy. `Auto` selects a model heuristically.
///
/// When dynamicSize is enabled the engine heuristically selects between a contiguous
/// growth strategy, a Fibonacci-sized expansion scheme and a page-sized allocation
/// model for better performance on large tape sizes.
/// @return
template <typename CellT>
int execute(std::vector<CellT>& cells, size_t& cellPtr, std::string& code,
            bool optimize = GOOF2_OPTIMIZE, int eof = GOOF2_DEFAULT_EOF_BEHAVIOUR,
            bool dynamicSize = GOOF2_DYNAMIC_CELLS_SIZE, bool term = GOOF2_DEFAULT_SAVE_STATE,
            MemoryModel model = MemoryModel::Auto, ProfileInfo* profile = nullptr,
            InstructionCache* cache = nullptr);
}  // namespace goof2
