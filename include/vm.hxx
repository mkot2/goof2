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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "vm/memory.hxx"

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
std::mutex& getLoopCacheMutex();
void clearLoopCache();
}  // namespace goof2

#include "vm/executor.hxx"
