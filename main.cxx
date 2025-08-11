/*
    Goof2 - An optimizing brainfuck VM
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
#include "cpp-terminal/color.hpp"
#include "cpp-terminal/style.hpp"
#include "include/vm.hxx"
#include "repl.hxx"

#ifdef _WIN32
#include <windows.h>
// Enable ANSI escape sequences on Windows terminals
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    enable_vt_mode();
#endif
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename;
    cmdl("i", "") >> filename;
    const bool dumpMemoryFlag = cmdl["dm"];
    const bool help = cmdl["h"];
    ReplConfig cfg{!cmdl["nopt"], cmdl["dts"], 0, 30000, 8};
    cmdl("eof", 0) >> cfg.eof;
    cmdl("ts", 30000) >> cfg.tapeSize;
    cmdl("cw", 8) >> cfg.cellWidth;
    if (cfg.tapeSize == 0) {
        std::cout << Term::color_fg(Term::Color::Name::Red)
                  << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                  << " Tape size must be positive; using default 30000" << std::endl;
        cfg.tapeSize = 30000;
    }
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        return 0;
    }
    if (!filename.empty()) {
        size_t cellPtr = 0;
        std::ifstream in(filename, std::ios::binary);
        if (!in.is_open()) {
            std::cout << Term::color_fg(Term::Color::Name::Red)
                      << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                      << " File could not be opened";
            return 1;
        }
        std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            std::cout << Term::color_fg(Term::Color::Name::Red)
                      << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                      << " Error while reading file";
            return 1;
        }
        switch (cfg.cellWidth) {
            case 8: {
                std::vector<uint8_t> cells(cfg.tapeSize, 0);
                executeExcept<uint8_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                       cfg.dynamicSize);
                if (dumpMemoryFlag) dumpMemory<uint8_t>(cells, cellPtr);
                break;
            }
            case 16: {
                std::vector<uint16_t> cells(cfg.tapeSize, 0);
                executeExcept<uint16_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize);
                if (dumpMemoryFlag) dumpMemory<uint16_t>(cells, cellPtr);
                break;
            }
            case 32: {
                std::vector<uint32_t> cells(cfg.tapeSize, 0);
                executeExcept<uint32_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize);
                if (dumpMemoryFlag) dumpMemory<uint32_t>(cells, cellPtr);
                break;
            }
            case 64: {
                std::vector<uint64_t> cells(cfg.tapeSize, 0);
                executeExcept<uint64_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize);
                if (dumpMemoryFlag) dumpMemory<uint64_t>(cells, cellPtr);
                break;
            }
            default:
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                          << " Unsupported cell width; use 8,16,32,64" << std::endl;
                return 1;
        }
        return 0;
    }
    while (true) {
        size_t cellPtr = 0;
        int newCw = 0;
        switch (cfg.cellWidth) {
            case 8: {
                std::vector<uint8_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint8_t>(cells, cellPtr, cfg);
                break;
            }
            case 16: {
                std::vector<uint16_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint16_t>(cells, cellPtr, cfg);
                break;
            }
            case 32: {
                std::vector<uint32_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint32_t>(cells, cellPtr, cfg);
                break;
            }
            case 64: {
                std::vector<uint64_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint64_t>(cells, cellPtr, cfg);
                break;
            }
            default:
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                          << " Unsupported cell width; use 8,16,32,64" << std::endl;
                return 1;
        }
        if (newCw == 0) break;
        cfg.cellWidth = newCw;
    }
    return 0;
}
