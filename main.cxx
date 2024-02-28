#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <ostream>
#include <iostream>
#include <string>
#include <string_view>

#define BOOST_REGEX_MAX_STATE_COUNT 1000000000 // Should be enough to parse anything
#include <boost/regex.hpp>
#include "argh.hxx"
#include "rang.hxx"

#define BFVM_TAPE_SIZE 65535

using namespace rang;

int fold(std::string_view code, size_t& i, char match)
{
    int count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) { i++; count++; }
    return count;
}

std::string processBalanced(std::string_view s, char no1, char no2)
{
    const int total = std::count(s.begin(), s.end(), no1) - std::count(s.begin(), s.end(), no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

void dumpMemory(const std::array<std::uint8_t, BFVM_TAPE_SIZE>& cells, size_t cellptr)
{
    size_t lastNonEmpty = 0;
    for (size_t i = BFVM_TAPE_SIZE - 1; i > 0; i--)
        if (cells[i]) {
            lastNonEmpty = i;
            break;
        }
    std::cout << "Memory dump:" << '\n'
              << style::underline << "         0   1   2   3   4   5   6   7   8   9" << style::reset << std::endl;
    for (size_t i = 0, row = 0; i <= std::max(lastNonEmpty, cellptr); i++) {
        if (i % 10 == 0) {
            if (row) std::cout << std::endl;
            std::cout << row << std::string(9 - std::to_string(row).length(), ' ');
            row += 10;
        }
        std::cout << (i == cellptr ? fg::green : fg::reset) << static_cast<unsigned>(cells[i]) << fg::reset << std::string(4 - std::to_string(cells[i]).length(), ' ');
    }
    std::cout << std::endl;
}

__attribute__((hot, aligned(64))) int execute(std::array<uint8_t, BFVM_TAPE_SIZE>& cells, size_t& cellptr, std::string& code, bool optimize, int eof = 0)
{
    struct instruction
    {
        const void* jump;
        int32_t data;
        const int16_t auxData;
        const int16_t offset;
    };

    std::vector<instruction> instructions;
    {
        enum class insType
        {
            ADD_SUB,
            SET,
            PTR_MOV,
            JMP_ZER,
            JMP_NOT_ZER,
            PUT_CHR,
            RAD_CHR,

            CLR,
            MUL_CPY,
            MUL_CPY_VEC,
            SCN_RGT,
            SCN_LFT,

            INT_END,
        };

        std::map<insType, void*> jtable;
        jtable[insType::ADD_SUB] = &&_ADD_SUB;
        jtable[insType::SET] = &&_SET;
        jtable[insType::PTR_MOV] = &&_PTR_MOV;
        jtable[insType::JMP_ZER] = &&_JMP_ZER;
        jtable[insType::JMP_NOT_ZER] = &&_JMP_NOT_ZER;
        jtable[insType::PUT_CHR] = &&_PUT_CHR;
        jtable[insType::RAD_CHR] = &&_RAD_CHR;

        jtable[insType::CLR] = &&_CLR;
        jtable[insType::MUL_CPY] = &&_MUL_CPY;
        jtable[insType::MUL_CPY_VEC] = &&_MUL_CPY_VEC;
        jtable[insType::SCN_RGT] = &&_SCN_RGT;
        jtable[insType::SCN_LFT] = &&_SCN_LFT;

        jtable[insType::INT_END] = &&_INT_END;

        int copyloopCounter = 0;
        std::vector<int> copyloopMap;
        std::vector<int> copyloopMulMap;

        int scanloopCounter = 0;
        std::vector<int> scanloopMap;

        int vectorCounter = 0;
        std::vector<int> vectorMap;

        if (optimize) {
            code = boost::regex_replace(code, boost::basic_regex(R"([^\+\-\>\<\.\,\]\[])"), "");

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '+', '-');
                });
            code = boost::regex_replace(code, boost::basic_regex(R"([><]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '>', '<');
                });

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]*(?:\[[+-]+\])+\.*)"), "C");

            code = boost::regex_replace(code, boost::basic_regex(R"(\[>+\])"), [&](auto& what) {
                const std::string current = what.str();
                scanloopMap.push_back(std::count(current.begin(), current.end(), '>'));
                return "R";
                });

            code = boost::regex_replace(code, boost::basic_regex(R"(\[<+\])"), [&](auto& what) {
                const std::string current = what.str();
                scanloopMap.push_back(std::count(current.begin(), current.end(), '<'));
                return "L";
                });

            code = boost::regex_replace(code, boost::basic_regex(R"(([RL]+)C|([CRL]+)\.+)"), "${1}${2}");

            code = boost::regex_replace(code, boost::basic_regex(R"([+\-C]+,)"), ",");

            code = boost::regex_replace(code, boost::basic_regex(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])"), [&](auto& what) {
                int numOfCopies = 0;
                int offset = 0;
                const std::string whole = what.str();
                const std::string current = what[1].str() + what[2].str();

                if (std::count(whole.begin(), whole.end(), '>') - std::count(whole.begin(), whole.end(), '<') == 0) {
                    boost::match_results<std::string::const_iterator> whatL;
                    auto start = current.cbegin();
                    auto end = current.cend();
                    while (boost::regex_search(start, end, whatL, boost::basic_regex(R"([<>]+[+-]+)"))) {
                        offset += -std::count(whatL[0].begin(), whatL[0].end(), '<') + std::count(whatL[0].begin(), whatL[0].end(), '>');
                        copyloopMap.push_back(offset);
                        copyloopMulMap.push_back(std::count(whatL[0].begin(), whatL[0].end(), '+') - std::count(whatL[0].begin(), whatL[0].end(), '-'));
                        numOfCopies++;
                        start = whatL[0].second;
                    }
                    return std::string(numOfCopies, 'P') + "C";
                } else {
                    return whole;
                }
                });

            code = boost::regex_replace(code, boost::basic_regex(R"((?:^|(?<=[RL\]*]))C*([\+\-]+))"), "S${1}");

            code = boost::regex_replace(code, boost::basic_regex(R"(P{3,})"), [&](auto& what) {
                const std::string whole(what.str());
                vectorMap.push_back(whole.length());
                return "V" + whole;
            });
        }

        std::vector<size_t> braceStack;
        int16_t offset = 0;
        bool set = false;
        #define MOVEOFFSET() if (offset) [[likely]] { instructions.push_back(instruction{ jtable[insType::PTR_MOV], offset, 0, 0}); offset = 0;}
        for (size_t i = 0; i < code.length(); i++) {
            switch (code[i]) {
            case '+':
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], fold(code, i, '+'), 0, offset });
                set = false;
                break;
            case '-':
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], -fold(code, i, '-'), 0, offset });
                set = false;
                break;
            case '>':
                offset += fold(code, i, '>');
                break;
            case '<':
                offset -= fold(code, i, '<');
                break;
            case '[':
                MOVEOFFSET();
                braceStack.push_back(instructions.size());
                instructions.push_back(instruction{ jtable[insType::JMP_ZER], 0, 0, 0 });
                break;
            case ']':
            {
                if (!braceStack.size())
                    return 1;

                MOVEOFFSET();
                const int start = braceStack.back();
                braceStack.pop_back();
                instructions[start].data = instructions.size();
                instructions.push_back(instruction{ jtable[insType::JMP_NOT_ZER], start, 0, 0 });
                break;
            }
            case '.':
                instructions.push_back(instruction{ jtable[insType::PUT_CHR], fold(code, i, '.'), 0, offset });
                break;
            case ',':
                instructions.push_back(instruction{ jtable[insType::RAD_CHR], 0, 0, offset });
                break;
            case 'C':
                instructions.push_back(instruction{ jtable[insType::CLR], 0, 0, offset });
                break;
            case 'P':
                instructions.push_back(instruction{ jtable[insType::MUL_CPY], copyloopMap[copyloopCounter], (int16_t)copyloopMulMap[copyloopCounter], offset });
                copyloopCounter++;
                break;        
            case 'R':
                MOVEOFFSET()
                instructions.push_back(instruction{ jtable[insType::SCN_RGT], scanloopMap[scanloopCounter++], 0, 0 });
                break;
            case 'L':
                MOVEOFFSET()
                instructions.push_back(instruction{ jtable[insType::SCN_LFT], scanloopMap[scanloopCounter++], 0, 0 });
                break;
            case 'S':
                set = true;
                break;
            case 'V':
                instructions.push_back(instruction{ jtable[insType::MUL_CPY_VEC], vectorMap[vectorCounter++], 0, offset });
                break;
            }
        }
        MOVEOFFSET()
        instructions.push_back(instruction{ jtable[insType::INT_END], 0, 0, 0 });

        if (!braceStack.empty())
            return 2;

        instructions.shrink_to_fit();
    }
    
    auto cell = cells.data() + cellptr;
    auto insp = instructions.data();
    const auto cellBase = cells.data();
    const auto inspBase = instructions.data();

    std::array<char, 1024> outBuffer = { 0 };
    int bufCount = 0;

    goto *insp->jump;

#define LOOP() insp++; goto *insp->jump
#define OFFCELL() *(cell+insp->offset)
#define OFFCELLP() *(cell+insp->offset+insp->data)
    _ADD_SUB:
    OFFCELL() += insp->data;
    LOOP();

    _SET:
    OFFCELL() = insp->data;
    LOOP();

    _PTR_MOV:
    cell += insp->data;
    LOOP();

    _JMP_ZER:
    if (!(*cell)) [[unlikely]] insp = inspBase + insp->data;
    LOOP();

    _JMP_NOT_ZER:
    if (*cell) [[likely]] insp = inspBase + insp->data;
    LOOP();

    _PUT_CHR:
    outBuffer.fill(0);
    #pragma GCC ivdep
    for (bufCount = 0; bufCount < insp->data; bufCount++)
        outBuffer[bufCount] = OFFCELL();
    std::cout << outBuffer.data();
    LOOP();

    _RAD_CHR:
    if (int in = std::cin.get(); in == EOF) {
        switch (eof) {
        case 0:
            break;
        case 1:
            OFFCELL() = 0;
            break;
        case 2:
            OFFCELL() = 255;
            break;
        default:
            __builtin_unreachable();
        }
    } else {
        OFFCELL() = in;
    }
    LOOP();

    _CLR:
    OFFCELL() = 0;
    LOOP();

    _MUL_CPY:
    OFFCELLP() += OFFCELL() * insp->auxData;
    LOOP();

    _MUL_CPY_VEC:
    const int amount = insp->data;
    const auto concOffset = insp->offset;

    insp++;
    #pragma GCC ivdep
    for (int i = 0; i < amount; i++)
        *(cell+concOffset+(insp+i)->data) += OFFCELL() * (insp+i)->auxData;

    insp += amount - 1;

    LOOP();

    _SCN_RGT:
    #pragma GCC ivdep
    for (int j = 0; true; j += insp->data)
        if (const auto check = cell + j; !*check) {
            cell = check;
            break;
        }
    LOOP();
    
    _SCN_LFT:
    #pragma GCC ivdep
    for (int k = 0; true; k += insp->data)
        if (const auto check = cell - k; !*check) {
            cell = check;
            break;
        }
    LOOP();

    _INT_END:
    cellptr = cell - cellBase;
    return 0;
}

int main(int argc, char* argv[])
{
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename; cmdl("i", "") >> filename;
    const bool optimize = !cmdl["nopt"];
    const bool sdumpMemory = cmdl["dm"];
    const bool help = cmdl["h"];
    int eof = 0; cmdl("eof", 0) >> eof;

    size_t cellptr = 0;
    std::array<uint8_t, BFVM_TAPE_SIZE> cells{ 0 };
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    } else if (!filename.empty()) {
        std::ifstream in(filename);
        if (std::string code; in.good()) {
            code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            switch (execute(cells, cellptr, code, optimize, eof)) {
            case 1:
                std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched close bracket";
                break;
            case 2:
                std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched open bracket";
                break;
            }
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
                  << "Goof - an optimizing BF VM, this time written in C++" << '\n'
                  << "Version 1.2" << '\n'
                  << "Type " << fg::cyan << "help" << fg::reset << " to see available commands."
                  << std::endl;

        while (true) {
            std::cout << ">>> ";
            std::string repl;
            std::cin >> repl;
            std::getchar();

            if (repl.starts_with("help")) {
                std::cout << style::underline << "General commands:" << style::reset << '\n'
                    << fg::cyan << "help" << fg::reset << " - Displays this list" << '\n'
                    << fg::cyan << "exit" << fg::reset << "/" << fg::cyan << "quit" << fg::reset << " - Exits Goof" << '\n'
                    << style::underline << "Memory commands:" << style::reset << '\n'
                    << fg::cyan << "clear" << fg::reset << " - Clears memory cells" << '\n'
                    << fg::cyan << "dump" << fg::reset << " - Displays values of memory cells, cell highlighted in " << fg::green << "green" << fg::reset << " is the cell currently pointed to" << '\n';
            } else if (repl.starts_with("clear")) {
                cellptr = 0;
                cells.fill(0);
            } else if (repl.starts_with("dump")) {
                dumpMemory(cells, cellptr);
            } else if (repl.starts_with("exit") || repl.starts_with("quit")) {
                return 0;
            } else {
                switch (execute(cells, cellptr, repl, optimize, eof)) {
                case 1:
                    std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched close bracket";
                    break;
                case 2:
                    std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched open bracket";
                    break;
                }
            }
        }
    }
}