/*
    Goof2 - An optimizing brainfuck VM
    Simple line-based REPL implementation using linenoise-ng
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifdef GOOF2_ENABLE_REPL
#include "repl.hxx"

#include <linenoise.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr int historyLen = 100;
}

template <typename CellT>
int runRepl(std::vector<CellT>& cells, size_t& cellPtr, ReplConfig& cfg) {
    linenoiseHistorySetMaxLen(historyLen);
    std::vector<CellT> prevCells;
    std::vector<size_t> changed;
    while (true) {
        char* line = linenoise("$ ");
        if (line == nullptr) {
            std::cout << std::endl;
            break;  // Ctrl-D or Ctrl-C
        }
        std::string input(line);
        linenoiseHistoryAdd(line);
        std::free(line);
        if (input.empty()) continue;
        if (input[0] == ':') {
            std::istringstream iss(input.substr(1));
            std::string cmd;
            iss >> cmd;
            if (cmd == "q" || cmd == "quit") {
                break;
            } else if (cmd == "dump") {
                dumpMemory(cells, cellPtr, std::cout, &changed, cfg.highlightChanges,
                           cfg.searchActive, cfg.searchValue);
            } else if (cmd == "help") {
                std::cout << "Commands:\n"
                          << ":dump              show memory\n"
                          << ":size N           resize tape to N cells\n"
                          << ":eof N            set EOF value\n"
                          << ":opt on|off       toggle optimization\n"
                          << ":dyn on|off       toggle dynamic tape\n"
                          << ":model auto|contig|fib|paged|os\n"
                          << ":highlight on|off highlight changed cells\n"
                          << ":search off|VAL   highlight cells equal to VAL\n"
                          << ":reset            clear memory and pointer\n"
                          << ":bits 8|16|32|64  switch cell width\n"
                          << ":q                quit" << std::endl;
            } else if (cmd == "size") {
                size_t n{};
                if (iss >> n && n > 0) {
                    cfg.tapeSize = n;
                    cells.resize(n, 0);
                    if (cellPtr >= n) cellPtr = n - 1;
                } else {
                    std::cout << "Invalid size" << std::endl;
                }
            } else if (cmd == "eof") {
                int v{};
                if (iss >> v) {
                    cfg.eof = v;
                } else {
                    std::cout << "Invalid EOF" << std::endl;
                }
            } else if (cmd == "opt") {
                std::string val;
                iss >> val;
                if (val == "on")
                    cfg.optimize = true;
                else if (val == "off")
                    cfg.optimize = false;
            } else if (cmd == "dyn") {
                std::string val;
                iss >> val;
                if (val == "on")
                    cfg.dynamicSize = true;
                else if (val == "off")
                    cfg.dynamicSize = false;
            } else if (cmd == "highlight") {
                std::string val;
                iss >> val;
                if (val == "on")
                    cfg.highlightChanges = true;
                else if (val == "off") {
                    cfg.highlightChanges = false;
                    changed.clear();
                }
            } else if (cmd == "search") {
                std::string val;
                if (!(iss >> val) || val == "off") {
                    cfg.searchActive = false;
                } else {
                    try {
                        cfg.searchValue = std::stoull(val);
                        cfg.searchActive = true;
                    } catch (...) {
                        std::cout << "Invalid search value" << std::endl;
                        cfg.searchActive = false;
                    }
                }
            } else if (cmd == "model") {
                std::string val;
                iss >> val;
                if (val == "auto")
                    cfg.model = goof2::MemoryModel::Auto;
                else if (val == "contig")
                    cfg.model = goof2::MemoryModel::Contiguous;
                else if (val == "fib")
                    cfg.model = goof2::MemoryModel::Fibonacci;
                else if (val == "paged")
                    cfg.model = goof2::MemoryModel::Paged;
                else if (val == "os")
                    cfg.model = goof2::MemoryModel::OSBacked;
            } else if (cmd == "reset") {
                std::fill(cells.begin(), cells.end(), 0);
                cellPtr = 0;
                changed.clear();
            } else if (cmd == "bits") {
                int w{};
                if (iss >> w && (w == 8 || w == 16 || w == 32 || w == 64)) {
                    return w;
                } else {
                    std::cout << "Unsupported width" << std::endl;
                }
            } else {
                std::cout << "Unknown command" << std::endl;
            }
            continue;
        }
        if (cfg.highlightChanges) prevCells = cells;
        executeExcept(cells, cellPtr, input, cfg.optimize, cfg.eof, cfg.dynamicSize, cfg.model,
                      nullptr, true);
        if (cfg.highlightChanges) {
            changed.clear();
            size_t limit = std::min(prevCells.size(), cells.size());
            for (size_t i = 0; i < limit; ++i) {
                if (cells[i] != prevCells[i]) changed.push_back(i);
            }
            for (size_t i = limit; i < cells.size(); ++i) {
                if (cells[i]) changed.push_back(i);
            }
        }
    }
    return 0;
}

template int runRepl<uint8_t>(std::vector<uint8_t>&, size_t&, ReplConfig&);
template int runRepl<uint16_t>(std::vector<uint16_t>&, size_t&, ReplConfig&);
template int runRepl<uint32_t>(std::vector<uint32_t>&, size_t&, ReplConfig&);
template int runRepl<uint64_t>(std::vector<uint64_t>&, size_t&, ReplConfig&);
#endif  // GOOF2_ENABLE_REPL
