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

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define GOOF2_HAS_OS_VM 1
#else
#define GOOF2_HAS_OS_VM 0
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace goof2 {
#if GOOF2_HAS_OS_VM
extern void* (*os_alloc)(size_t);
extern void (*os_free)(void*, size_t);
#endif

enum class MemoryModel { Auto, Contiguous, Fibonacci, Paged, OSBacked };
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
            MemoryModel model = MemoryModel::Auto);
}  // namespace goof2
