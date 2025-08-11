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

int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename;
    cmdl("i", "") >> filename;
    const bool optimize = !cmdl["nopt"];
    const bool dumpMemoryFlag = cmdl["dm"];
    const bool help = cmdl["h"];
    const bool dynamicSize = cmdl["dts"];
    int eof = 0;
    cmdl("eof", 0) >> eof;
    int tsArg = 0;
    cmdl("ts", 30000) >> tsArg;
    int cwArg = 8;
    cmdl("cw", 8) >> cwArg;
    if (tsArg <= 0) {
        std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                  << Term::color_fg(Term::Color::Name::Default)
                  << " Tape size must be positive; using default 30000" << std::endl;
        tsArg = 30000;
    }
    size_t ts = static_cast<size_t>(tsArg);
    size_t cellPtr = 0;

    auto run = [&](auto dummy) {
        using CellT = decltype(dummy);
        std::vector<CellT> cells(ts, 0);
        if (help) {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            return;  // no execution
        }
        if (!filename.empty()) {
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
                    executeExcept<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize);
                    if (dumpMemoryFlag) dumpMemory<CellT>(cells, cellPtr);
                }
            }
        } else {
            runRepl<CellT>(cells, cellPtr, ts, optimize, eof, dynamicSize);
        }
    };

    switch (cwArg) {
        case 8:
            run(uint8_t{});
            break;
        case 16:
            run(uint16_t{});
            break;
        case 32:
            run(uint32_t{});
            break;
        default:
            std::cout << Term::color_fg(Term::Color::Name::Red) << "ERROR:"
                      << Term::color_fg(Term::Color::Name::Default)
                      << " Unsupported cell width; use 8,16,32" << std::endl;
            return 1;
    }
}
