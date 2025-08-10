/*
    Goof - An optimizing brainfuck VM
    Version 1.4.0

    Made by M.K.
    Co-vibed by ChatGPT 5 Thinking :3
    2023-2025
    Published under the CC0-1.0 license
*/
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "argh.hxx"
#include "rang.hxx"
#include "vm.hpp"

using namespace rang;

void dumpMemory(const std::vector<uint8_t>& cells, size_t cellptr)
{
    size_t lastNonEmpty = 0;
    for (lastNonEmpty = cells.size() - 1; lastNonEmpty > cellptr; lastNonEmpty--)
        if (cells[lastNonEmpty])
            break;
    std::cout << "Memory dump:" << '\n'
              << style::underline << "row+col |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |" << std::endl;
    for (size_t i = 0, row = 0; i <= std::max(lastNonEmpty, cellptr); i++) {
        if (i % 10 == 0) {
            if (row) std::cout << std::endl;
            std::cout << row << std::string(8 - std::to_string(row).length(), ' ') << "|";
            row += 10;
        }
        std::cout << (i == cellptr ? fg::green : fg::reset)
                  << +cells[i] << fg::reset << std::string(3 - std::to_string(cells[i]).length(), ' ') << "|";
    }
    std::cout << style::reset << std::endl;
}

int main(int argc, char* argv[])
{
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename; cmdl("i", "") >> filename;
    const bool optimize = !cmdl["nopt"];
    const bool sdumpMemory = cmdl["dm"];
    const bool help = cmdl["h"];
    const bool dynamicSize = cmdl["dts"];
    int eof = 0; cmdl("eof", 0) >> eof;
    int ts = 0; cmdl("ts", 30000) >> ts;

    size_t cellptr = 0;
    std::vector<uint8_t> cells;
    cells.assign(ts, 0);
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    } else if (!filename.empty()) {
        std::ifstream in(filename);
        if (std::string code; in.good()) {
            code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            executeExcept(cells, cellptr, code, optimize, eof, dynamicSize);
            if (sdumpMemory) dumpMemory(cells, cellptr);
        } else {
            std::cout << fg::red << "ERROR:" << fg::reset << " File could not be read";
        }
    } else {
        std::cout << R"(   _____  ____   ____  ______ )" << '\n'
                  << R"(  / ____|/ __ \ / __ \|  ____|)" << '\n'
                  << R"( | |  __| |  | | |  | | |__   )" << '\n'
                  << R"( | | |_ | |  | | |  | |  __|  )" << '\n'
                  << R"( | |__| | |__| | |__| | |     )" << '\n'
                  << R"(  \_____|\____/ \____/|_|     )" << '\n'
                  << "Goof v1.4.0 - an optimizing brainfuck VM" << '\n'
                  << "Type " << fg::cyan << "help" << fg::reset << " to see available commands."
                  << std::endl;

        while (true) {
            std::cout << "$ ";
            std::string repl;
            std::cin >> repl;
            std::getchar();

            //TODO: Add a visualiser?
            if (repl.starts_with("help")) {
                std::cout << style::underline << "General commands:" << style::reset << '\n'
                    << fg::cyan << "help" << fg::reset << " - Displays this list" << '\n'
                    << fg::cyan << "exit" << fg::reset << "/" << fg::cyan << "quit" << fg::reset << " - Exits Goof" << '\n'
                    << style::underline << "Memory commands:" << style::reset << '\n'
                    << fg::cyan << "clear" << fg::reset << " - Clears memory cells" << '\n'
                    << fg::cyan << "dump" << fg::reset << " - Displays values of memory cells, cell highlighted in " << fg::green << "green" << fg::reset << " is the cell currently pointed to" << '\n';
            } else if (repl.starts_with("clear")) {
                cellptr = 0;
                cells.assign(ts, 0);
            } else if (repl.starts_with("dump")) {
                dumpMemory(cells, cellptr);
            } else if (repl.starts_with("exit") || repl.starts_with("quit")) {
                return 0;
            } else {
                executeExcept(cells, cellptr, repl, optimize, eof, dynamicSize, true);
            }
        }
    }
}
