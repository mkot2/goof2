/*
    Goof2 - An optimizing brainfuck VM
    Machine learning optimizer interface
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>

namespace goof2 {
// Global toggle enabled by default, can be disabled via --no-ml-opt
extern bool mlOptimizerEnabled;
// Apply model-based rewrites to the provided Brainfuck code
void applyMlOptimizer(std::string& code);
}  // namespace goof2
