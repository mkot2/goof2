/*
    Goof2 - An optimizing brainfuck VM
    REPL API declarations
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#ifdef GOOF2_ENABLE_REPL
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "ansi.hxx"
#include "vm.hxx"

struct ReplConfig {
    bool optimize;
    bool dynamicSize;
    int eof;
    size_t tapeSize;
    int cellWidth;
    goof2::MemoryModel model;
    bool highlightChanges;
    bool searchActive;
    uint64_t searchValue;
};

template <typename CellT>
int runRepl(std::vector<CellT>& cells, size_t& cellPtr, ReplConfig& cfg);

template <typename CellT>
inline void dumpMemory(const std::vector<CellT>& cells, size_t cellPtr,
                       std::ostream& out = std::cout, const std::vector<size_t>* changed = nullptr,
                       bool highlight = false, bool searchActive = false,
                       uint64_t searchValue = 0) {
    if (cells.empty()) {
        out << "Memory dump:" << '\n' << "<empty>" << std::endl;
        return;
    }
    size_t lastNonEmpty = cells.size() - 1;
    while (lastNonEmpty > cellPtr && lastNonEmpty > 0 && !cells[lastNonEmpty]) {
        --lastNonEmpty;
    }
    out << "Memory dump:" << '\n'
        << ansi::underline << "row+col |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |" << ansi::reset
        << std::endl;
    size_t end = std::max(lastNonEmpty, std::min(cellPtr, cells.size() - 1));
    for (size_t i = 0, row = 0; i <= end; ++i) {
        if (i % 10 == 0) {
            if (row) out << std::endl;
            std::string rowStr = std::to_string(row);
            size_t rowPad = rowStr.length() < 8 ? 8 - rowStr.length() : 0;
            out << rowStr << std::string(rowPad, ' ') << "|";
            row += 10;
        }
        bool changedCell = highlight && changed &&
                           std::find(changed->begin(), changed->end(), i) != changed->end();
        bool match = searchActive && static_cast<uint64_t>(cells[i]) == searchValue;
        const auto& color = i == cellPtr  ? ansi::green
                            : match       ? ansi::red
                            : changedCell ? ansi::yellow
                                          : ansi::reset;
        std::string cellStr = std::to_string(cells[i]);
        size_t cellPad = cellStr.length() < 3 ? 3 - cellStr.length() : 0;
        out << color << cellStr << ansi::reset << std::string(cellPad, ' ') << "|";
    }
    out << ansi::reset << std::endl;
}

template <typename CellT>
inline void executeExcept(std::vector<CellT>& cells, size_t& cellPtr, std::string& code,
                          bool optimize, int eof, bool dynamicSize, goof2::MemoryModel model,
                          goof2::ProfileInfo* profile = nullptr, bool term = false) {
    int ret = goof2::execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, term, model,
                                    profile);
    switch (ret) {
        case 1:
            std::cout << ansi::red << "ERROR:" << ansi::reset << " Unmatched close bracket"
                      << std::endl;
            break;
        case 2:
            std::cout << ansi::red << "ERROR:" << ansi::reset << " Unmatched open bracket"
                      << std::endl;
            break;
    }
}

extern template int runRepl<uint8_t>(std::vector<uint8_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint16_t>(std::vector<uint16_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint32_t>(std::vector<uint32_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint64_t>(std::vector<uint64_t>&, size_t&, ReplConfig&);
#endif  // GOOF2_ENABLE_REPL
