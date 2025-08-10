#pragma once
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

void run_repl(std::vector<uint8_t>& cells, size_t& cellptr, size_t ts, bool optimize, int eof,
              bool dynamicSize);
void dumpMemory(const std::vector<uint8_t>& cells, size_t cellptr, std::ostream& out = std::cout);
void executeExcept(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize,
                   int eof, bool dynamicSize, bool term = false);
