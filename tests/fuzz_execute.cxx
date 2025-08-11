#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "vm.hxx"

static std::string random_program(std::mt19937 &gen) {
    static const char ops[] = "+-<>.,";
    std::uniform_int_distribution<int> lenDist(0, 16);
    std::uniform_int_distribution<int> opDist(0, sizeof(ops) - 2);
    int len = lenDist(gen);
    std::string program;
    program.reserve(len);
    int ptr = 0;
    for (int i = 0; i < len; ++i) {
        char op = ops[opDist(gen)];
        if (op == '>' && ptr >= 31) {
            --i;
            continue;
        }
        if (op == '<' && ptr <= 0) {
            --i;
            continue;
        }
        program += op;
        if (op == '>')
            ++ptr;
        else if (op == '<')
            --ptr;
    }
    return program;
}

static std::string random_input(std::mt19937 &gen) {
    std::uniform_int_distribution<int> lenDist(0, 8);
    std::uniform_int_distribution<int> byteDist(0, 255);
    int len = lenDist(gen);
    std::string input;
    input.reserve(len);
    for (int i = 0; i < len; ++i) {
        input += static_cast<char>(byteDist(gen));
    }
    return input;
}

int main() {
    std::mt19937 gen(std::random_device{}());
    for (int i = 0; i < 100; ++i) {
        std::string code = random_program(gen);
        std::string input = random_input(gen);
        std::vector<uint8_t> cells(32, 0);
        size_t ptr = 0;
        std::istringstream in(input);
        std::ostringstream out;
        auto *cinbuf = std::cin.rdbuf(in.rdbuf());
        auto *coutbuf = std::cout.rdbuf(out.rdbuf());
        std::cin.clear();
        try {
            goof2::execute<uint8_t>(cells, ptr, code, true, 0, true, false);
        } catch (...) {
        }
        std::cin.rdbuf(cinbuf);
        std::cout.rdbuf(coutbuf);
    }
    return 0;
}
