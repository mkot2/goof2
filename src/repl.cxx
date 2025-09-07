/*
    Goof2 - An optimizing brainfuck VM
    Simple line-based REPL implementation using linenoise-ng
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifdef GOOF2_ENABLE_REPL
#include "repl.hxx"

#include <linenoise.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int historyLen = 100;
}

template <typename CellT>
int runRepl(std::vector<CellT>& cells, size_t& cellPtr, ReplConfig& cfg) {
    linenoiseHistorySetMaxLen(historyLen);
    while (true) {
        char* line = linenoise("$ ");
        if (line == nullptr) {
            std::cout << std::endl;
            break;  // Ctrl-D or Ctrl-C
        }
        std::string input(line);
        linenoiseHistoryAdd(line);
        std::free(line);
        if (input == ":q" || input == ":quit") {
            break;
        }
        if (input == ":dump") {
            dumpMemory(cells, cellPtr);
            continue;
        }
        executeExcept(cells, cellPtr, input, cfg.optimize, cfg.eof, cfg.dynamicSize, cfg.model,
                      nullptr, true);
    }
    return 0;
}

template int runRepl<uint8_t>(std::vector<uint8_t>&, size_t&, ReplConfig&);
template int runRepl<uint16_t>(std::vector<uint16_t>&, size_t&, ReplConfig&);
template int runRepl<uint32_t>(std::vector<uint32_t>&, size_t&, ReplConfig&);
template int runRepl<uint64_t>(std::vector<uint64_t>&, size_t&, ReplConfig&);
#endif  // GOOF2_ENABLE_REPL
