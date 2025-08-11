/*
    Goof - An optimizing brainfuck VM
    VM API declarations
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#define BFVMCPP_DEFAULT_EOF_BEHAVIOUR 0
#define BFVMCPP_DYNAMIC_CELLS_SIZE 1
#define BFVMCPP_OPTIMIZE 1
#define BFVMCPP_DEFAULT_SAVE_STATE 0

#include <cstdint>
#include <string>
#include <vector>

namespace bfvmcpp {
/// @brief Only function you should use in your code. For now, it always prints to stdout.
/// @tparam CellT Cell width type (uint8_t, uint16_t, uint32_t)
/// @param cells Vector of cells of type CellT.
/// @param cellPtr
/// @param code Remember that this will be modified, so if you need to do something else with your
/// plaintext code, make a copy.
/// @param optimize Enable optimizations (highly recommended). Set it here as an override, for the
/// default state, check the BFVMCPP_OPTIMIZE define.
/// @param eof EOF behaviour. 0 = cell unchanged, 1 = set to 0, 2 = set to 255. Check
/// BFVMCPP_DEFAULT_EOF_BEHAVIOUR.
/// @param dynamicSize Allow dynamic resizing of the cells vector. Disable if you want, for example,
/// a constant number of decimals for a calculation, constant cell vector size. OOB on fixed size is
/// currently not handled.
/// @param term A few tweaks necessary to make it operable multiple times on the same cells. Check
/// BFVMCPP_DEFAULT_SAVE_STATE.
///
/// When dynamicSize is enabled the engine heuristically selects between a contiguous
/// growth strategy, a Fibonacci-sized expansion scheme and a page-sized allocation
/// model for better performance on large tape sizes.
/// @return
template <typename CellT>
int execute(std::vector<CellT>& cells, size_t& cellPtr, std::string& code,
            bool optimize = BFVMCPP_OPTIMIZE, int eof = BFVMCPP_DEFAULT_EOF_BEHAVIOUR,
            bool dynamicSize = BFVMCPP_DYNAMIC_CELLS_SIZE, bool term = BFVMCPP_DEFAULT_SAVE_STATE);
}  // namespace bfvmcpp
