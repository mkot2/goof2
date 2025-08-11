/*
    Goof - An optimizing brainfuck VM
    REPL API declarations
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

void runRepl(std::vector<uint8_t>& cells, size_t& cellPtr, size_t ts, bool optimize, int eof,
             bool dynamicSize);
void dumpMemory(const std::vector<uint8_t>& cells, size_t cellPtr, std::ostream& out = std::cout);
void executeExcept(std::vector<uint8_t>& cells, size_t& cellPtr, std::string& code, bool optimize,
                   int eof, bool dynamicSize, bool term = false);
