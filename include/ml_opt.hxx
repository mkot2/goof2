/*
    Goof2 - An optimizing brainfuck VM
    Machine learning optimizer interface
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>

namespace goof2 {
// Global toggle enabled via the --ml-opt command-line flag
extern bool mlOptimizerEnabled;
// Number of replacements made by the ML optimizer
extern std::size_t mlOptimizerReplacements;
// Apply model-based rewrites to the provided Brainfuck code
void applyMlOptimizer(std::string& code);
}  // namespace goof2
