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
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "cpp-terminal/color.hpp"
#include "cpp-terminal/style.hpp"
#include "vm.hxx"

struct ReplConfig {
    bool optimize;
    bool dynamicSize;
    int eof;
    size_t tapeSize;
    int cellWidth;
    goof2::MemoryModel model;
};

template <typename CellT>
int runRepl(std::vector<CellT>& cells, size_t& cellPtr, ReplConfig& cfg);

template <typename CellT>
inline void dumpMemory(const std::vector<CellT>& cells, size_t cellPtr,
                       std::ostream& out = std::cout) {
    if (cells.empty()) {
        out << "Memory dump:" << '\n' << "<empty>" << std::endl;
        return;
    }
    size_t lastNonEmpty = cells.size() - 1;
    while (lastNonEmpty > cellPtr && lastNonEmpty > 0 && !cells[lastNonEmpty]) {
        --lastNonEmpty;
    }
    out << "Memory dump:" << '\n'
        << Term::Style::Underline << "row+col |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |"
        << Term::Style::Reset << std::endl;
    size_t end = std::max(lastNonEmpty, std::min(cellPtr, cells.size() - 1));
    for (size_t i = 0, row = 0; i <= end; ++i) {
        if (i % 10 == 0) {
            if (row) out << std::endl;
            out << row << std::string(8 - std::to_string(row).length(), ' ') << "|";
            row += 10;
        }
        out << (i == cellPtr ? Term::color_fg(Term::Color::Name::Green)
                             : Term::color_fg(Term::Color::Name::Default))
            << +cells[i] << Term::color_fg(Term::Color::Name::Default)
            << std::string(3 - std::to_string(cells[i]).length(), ' ') << "|";
    }
    out << Term::Style::Reset << std::endl;
}

template <typename CellT>
inline void executeExcept(std::vector<CellT>& cells, size_t& cellPtr, std::string& code,
                          bool optimize, int eof, bool dynamicSize, goof2::MemoryModel model,
                          bool term = false) {
    int ret = goof2::execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, term, model);
    switch (ret) {
        case 1:
            std::cout << Term::color_fg(Term::Color::Name::Red)
                      << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                      << " Unmatched close bracket";
            break;
        case 2:
            std::cout << Term::color_fg(Term::Color::Name::Red)
                      << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                      << " Unmatched open bracket";
            break;
    }
}

extern template int runRepl<uint8_t>(std::vector<uint8_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint16_t>(std::vector<uint16_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint32_t>(std::vector<uint32_t>&, size_t&, ReplConfig&);
extern template int runRepl<uint64_t>(std::vector<uint64_t>&, size_t&, ReplConfig&);
#endif  // GOOF2_ENABLE_REPL
