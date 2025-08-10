#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Execute Brainfuck code. Template parameters control dynamic tape size and terminal mode.
template<bool Dynamic, bool Term>
int execute(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code,
            bool optimize, int eof);

// Wrapper that reports unmatched bracket errors.
void executeExcept(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code,
                   bool optimize, int eof, bool dynamicSize, bool term = false);
