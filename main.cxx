/*
    Goof - An optimizing brainfuck VM
    Version 1.4.0

    Made by M.K.
    Co-vibed by ChatGPT 5 Thinking :3
    2023-2025
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "argh.hxx"
#include "include/vm.hxx"
#include "cpp-terminal/color.hpp"
#include "cpp-terminal/style.hpp"
#include "repl.hxx"

void dumpMemory(const std::vector<uint8_t>& cells, size_t cellptr, std::ostream& out) {
    if (cells.empty()) {
        out << "Memory dump:" << '\n' << "<empty>" << std::endl;
        return;
    }
    size_t lastNonEmpty = cells.size() - 1;
    while (lastNonEmpty > cellptr && lastNonEmpty > 0 && !cells[lastNonEmpty]) {
        --lastNonEmpty;
    }
    out << "Memory dump:" << '\n'
        << Term::Style::Underline
        << "row+col |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |"
        << Term::Style::Reset << std::endl;
    size_t end = std::max(lastNonEmpty, std::min(cellptr, cells.size() - 1));
    for (size_t i = 0, row = 0; i <= end; ++i) {
        if (i % 10 == 0) {
            if (row) out << std::endl;
            out << row << std::string(8 - std::to_string(row).length(), ' ') << "|";
            row += 10;
        }
        out << (i == cellptr ? Term::color_fg(Term::Color::Name::Green)
                              : Term::color_fg(Term::Color::Name::Default))
            << +cells[i] << Term::color_fg(Term::Color::Name::Default)
            << std::string(3 - std::to_string(cells[i]).length(), ' ') << "|";
    }
    out << Term::Style::Reset << std::endl;
}

void executeExcept(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize,
                   int eof, bool dynamicSize, bool term) {
    int ret = bfvmcpp::execute(cells, cellptr, code, optimize, eof, dynamicSize, term);
    switch (ret) {
        case 1:
            std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                      << Term::color_fg(Term::Color::Name::Default)
                      << " Unmatched close bracket";
            break;
        case 2:
            std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                      << Term::color_fg(Term::Color::Name::Default)
                      << " Unmatched open bracket";
            break;
    }
}

int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename;
    cmdl("i", "") >> filename;
    const bool optimize = !cmdl["nopt"];
    const bool sdumpMemory = cmdl["dm"];
    const bool help = cmdl["h"];
    const bool dynamicSize = cmdl["dts"];
    int eof = 0;
    cmdl("eof", 0) >> eof;
    int tsArg = 0;
    cmdl("ts", 30000) >> tsArg;
    if (tsArg <= 0) {
        std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                  << Term::color_fg(Term::Color::Name::Default)
                  << " Tape size must be positive; using default 30000" << std::endl;
        tsArg = 30000;
    }
    size_t ts = static_cast<size_t>(tsArg);

    size_t cellptr = 0;
    std::vector<uint8_t> cells;
    cells.assign(ts, 0);

    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    } else if (!filename.empty()) {
        std::ifstream in(filename, std::ios::binary);
        if (!in.is_open()) {
            std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                      << Term::color_fg(Term::Color::Name::Default)
                      << " File could not be opened";
        } else {
            std::string code((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            if (!in && !in.eof()) {
                std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                          << Term::color_fg(Term::Color::Name::Default)
                          << " Error while reading file";
            } else {
                executeExcept(cells, cellptr, code, optimize, eof, dynamicSize);
                if (sdumpMemory) dumpMemory(cells, cellptr);
            }
        }
    } else {
        run_repl(cells, cellptr, ts, optimize, eof, dynamicSize);
    }
}
