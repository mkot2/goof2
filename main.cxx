/*
    Goof2 - An optimizing brainfuck VM
    Main standalone file
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "vm.hxx"
#ifdef GOOF2_ENABLE_REPL
#include "cpp-terminal/color.hpp"
#include "cpp-terminal/style.hpp"
#include "repl.hxx"
#endif

#ifdef _WIN32
#ifdef GOOF2_ENABLE_REPL
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
#endif

namespace {
struct CmdArgs {
    std::string filename;
    bool dumpMemory = false;
    bool help = false;
    bool optimize = true;
    bool dynamicTape = false;
    int eof = 0;
    std::size_t tapeSize = 30000;
    int cellWidth = 8;
};

CmdArgs parseArgs(int argc, char* argv[]) {
    CmdArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-i" && i + 1 < argc) {
            args.filename = argv[++i];
        } else if (arg == "-dm") {
            args.dumpMemory = true;
        } else if (arg == "-h") {
            args.help = true;
        } else if (arg == "-nopt") {
            args.optimize = false;
        } else if (arg == "-dts") {
            args.dynamicTape = true;
        } else if (arg == "-eof" && i + 1 < argc) {
            args.eof = std::stoi(argv[++i]);
        } else if (arg == "-ts" && i + 1 < argc) {
            args.tapeSize = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "-cw" && i + 1 < argc) {
            args.cellWidth = std::stoi(argv[++i]);
        }
    }
    return args;
}
}  // namespace

#ifdef GOOF2_ENABLE_REPL
int main(int argc, char* argv[]) {
#ifdef _WIN32
    enable_vt_mode();
#endif
    CmdArgs opts = parseArgs(argc, argv);
    std::string filename = opts.filename;
    const bool dumpMemoryFlag = opts.dumpMemory;
    const bool help = opts.help;
    ReplConfig cfg{opts.optimize, opts.dynamicTape, opts.eof, opts.tapeSize, opts.cellWidth};
    if (cfg.tapeSize == 0) {
        std::cout << Term::color_fg(Term::Color::Name::Red)
                  << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                  << " Tape size must be positive; using default 30000" << std::endl;
        cfg.tapeSize = 30000;
    }
    std::size_t requiredMem = cfg.tapeSize * (cfg.cellWidth / 8);
    if (requiredMem > GOOF2_TAPE_WARN_BYTES) {
        std::cout << Term::color_fg(Term::Color::Name::Yellow)
                  << "WARNING:" << Term::color_fg(Term::Color::Name::Default)
                  << " Tape allocation ~" << (requiredMem >> 20) << " MiB may exceed system memory"
                  << std::endl;
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
#else   // !GOOF2_ENABLE_REPL
namespace {
template <typename CellT>
void dumpMemory(const std::vector<CellT>& cells, size_t cellPtr) {
    if (cells.empty()) {
        std::cout << "Memory dump:\n<empty>" << std::endl;
        return;
    }
    size_t lastNonEmpty = cells.size() - 1;
    while (lastNonEmpty > cellPtr && lastNonEmpty > 0 && !cells[lastNonEmpty]) {
        --lastNonEmpty;
    }
    std::cout << "Memory dump:" << std::endl;
    for (size_t i = 0; i <= lastNonEmpty; ++i) {
        if (i == cellPtr) std::cout << '[';
        std::cout << +cells[i];
        if (i == cellPtr) std::cout << ']';
        std::cout << (i % 10 == 9 ? '\n' : ' ');
    }
    if (lastNonEmpty % 10 != 9) std::cout << std::endl;
}

template <typename CellT>
void executeExcept(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                   int eof, bool dynamicSize) {
    int ret = goof2::execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, false);
    switch (ret) {
        case 1:
            std::cerr << "ERROR: Unmatched close bracket";
            break;
        case 2:
            std::cerr << "ERROR: Unmatched open bracket";
            break;
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    CmdArgs opts = parseArgs(argc, argv);

    std::string filename = opts.filename;
    const bool dumpMemoryFlag = opts.dumpMemory;
    const bool help = opts.help;
    bool optimize = opts.optimize;
    bool dynamicSize = opts.dynamicTape;
    int eof = opts.eof;
    std::size_t tapeSize = opts.tapeSize;
    int cellWidth = opts.cellWidth;
    if (tapeSize == 0) {
        std::cout << "ERROR: Tape size must be positive; using default 30000" << std::endl;
        tapeSize = 30000;
    }
    std::size_t requiredMem = tapeSize * (cellWidth / 8);
    if (requiredMem > GOOF2_TAPE_WARN_BYTES) {
        std::cout << "WARNING: Tape allocation ~" << (requiredMem >> 20)
                  << " MiB may exceed system memory" << std::endl;
    }
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        return 0;
    }
    if (filename.empty()) {
        std::cout << "REPL disabled; use -i <file> to run a program" << std::endl;
        return 0;
    }
    size_t cellPtr = 0;
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "ERROR: File could not be opened";
        return 1;
    }
    std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) {
        std::cerr << "ERROR: Error while reading file";
        return 1;
    }
    switch (cellWidth) {
        case 8: {
            std::vector<uint8_t> cells(tapeSize, 0);
            executeExcept<uint8_t>(cells, cellPtr, code, optimize, eof, dynamicSize);
            if (dumpMemoryFlag) dumpMemory<uint8_t>(cells, cellPtr);
            break;
        }
        case 16: {
            std::vector<uint16_t> cells(tapeSize, 0);
            executeExcept<uint16_t>(cells, cellPtr, code, optimize, eof, dynamicSize);
            if (dumpMemoryFlag) dumpMemory<uint16_t>(cells, cellPtr);
            break;
        }
        case 32: {
            std::vector<uint32_t> cells(tapeSize, 0);
            executeExcept<uint32_t>(cells, cellPtr, code, optimize, eof, dynamicSize);
            if (dumpMemoryFlag) dumpMemory<uint32_t>(cells, cellPtr);
            break;
        }
        case 64: {
            std::vector<uint64_t> cells(tapeSize, 0);
            executeExcept<uint64_t>(cells, cellPtr, code, optimize, eof, dynamicSize);
            if (dumpMemoryFlag) dumpMemory<uint64_t>(cells, cellPtr);
            break;
        }
        default:
            std::cerr << "ERROR: Unsupported cell width; use 8,16,32,64" << std::endl;
            return 1;
    }
    return 0;
}
#endif  // GOOF2_ENABLE_REPL
