#include <array>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <iostream>
#include <string>
#include <string_view>

#define BOOST_REGEX_MAX_STATE_COUNT 1000000000 // Should be enough to parse anything
#include <boost/regex.hpp>
#include "argh.hxx"
#include "rang.hxx"
#include "mmx.h"

#define BFVM_DEFAULT_TAPE_SIZE 32768 // Initial size of the cell vector

using namespace rang;

size_t fold(std::string_view code, size_t& i, char match)
{
    size_t count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) { i++; count++; }
    return count;
}

std::string processBalanced(std::string_view s, char no1, char no2)
{
    const auto total = std::count(s.begin(), s.end(), no1) - std::count(s.begin(), s.end(), no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

void dumpMemory(const std::vector<uint8_t>& cells, size_t cellptr)
{
    size_t lastNonEmpty = 0;
    for (size_t i = cells.size() - 1; i > 0; i--)
        if (cells[i]) {
            lastNonEmpty = i;
            break;
        }
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

__attribute((optimize(3),hot,aligned(64)))
int execute(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize, int eof, bool term = false)
{
    struct instruction
    {
        const void* jump;
        const int32_t data;
        const int16_t auxData;
        const int16_t offset;
    };

    std::vector<instruction> instructions;
    {
        enum class insType // I wish for implicity to int :(
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
            SCN_RGT,
            SCN_LFT,

            END,
        };

        std::map<insType, void*> jtable = {
            {insType::ADD_SUB, &&_ADD_SUB},
            {insType::SET, &&_SET},
            {insType::PTR_MOV, &&_PTR_MOV},
            {insType::JMP_ZER, &&_JMP_ZER},
            {insType::JMP_NOT_ZER, &&_JMP_NOT_ZER},
            {insType::PUT_CHR, &&_PUT_CHR},
            {insType::RAD_CHR, &&_RAD_CHR},

            {insType::CLR, &&_CLR},
            {insType::MUL_CPY, &&_MUL_CPY},
            {insType::SCN_RGT, &&_SCN_RGT},
            {insType::SCN_LFT, &&_SCN_LFT},

            {insType::END, &&_END}
        };

        int copyloopCounter = 0;
        std::vector<int> copyloopMap;

        int scanloopCounter = 0;
        std::vector<int> scanloopMap;

        if (optimize) {
            code = boost::regex_replace(code, boost::basic_regex(R"([^\+\-\>\<\.\,\]\[])"), "");

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '+', '-');
                });
            code = boost::regex_replace(code, boost::basic_regex(R"([><]{2,})"), [&](auto& what) {
                return processBalanced(what.str(), '>', '<');
                });

            code = boost::regex_replace(code, boost::basic_regex(R"([+-]*(?:\[[+-]+\])+\.*)"), "C");

            code = boost::regex_replace(code, boost::basic_regex(R"(\[>+\]|\[<+\])"), [&](auto& what) {
                const auto current = what.str();
                const auto count = std::count(current.begin(), current.end(), '>') - std::count(current.begin(), current.end(), '<');
                scanloopMap.push_back(std::abs(count));
                if (count > 0)
                    return "R";
                else
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
                        copyloopMap.push_back(std::count(whatL[0].begin(), whatL[0].end(), '+') - std::count(whatL[0].begin(), whatL[0].end(), '-'));
                        numOfCopies++;
                        start = whatL[0].second;
                    }
                    return std::string(numOfCopies, 'P') + "C";
                } else {
                    return whole;
                }
                });

            //if (!term) code = boost::regex_replace(code, boost::basic_regex(R"((?:^|(?<=[RL\]])|C+)([\+\-]+))"), "S${1}"); // We can't really assume in term

            //code = boost::regex_replace(code, boost::basic_regex(R"((?<=[^\.]\.)[^\.]+?$)"), "");

        }
        
        std::vector<size_t> braceStack;
        int16_t offset = 0;
        bool set = false;
        instructions.reserve(code.length());
        #define MOVEOFFSET() if (offset) [[likely]] { instructions.push_back(instruction{ jtable[insType::PTR_MOV], offset, 0, 0}); offset = 0;}
        for (size_t i = 0; i < code.length(); i++) {
            switch (code[i]) {
            case '+':
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], fold(code, i, '+'), 0, offset });
                set = false;
                break;
            case '-':
            {               
                const auto folded = -fold(code, i, '-');
                instructions.push_back(instruction{ jtable[set ? insType::SET : insType::ADD_SUB], set ? 255 * (-folded / 255 + 1) + folded : folded, 0, offset });
                set = false;
                break;
            }
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
                const int sizeminstart = instructions.size() - start;
                braceStack.pop_back();
                const_cast<int32_t&>(instructions[start].data) = sizeminstart;
                instructions.push_back(instruction{ jtable[insType::JMP_NOT_ZER], sizeminstart, 0, 0 });
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
                instructions.push_back(instruction{ jtable[insType::MUL_CPY], copyloopMap[copyloopCounter++], (int16_t)copyloopMap[copyloopCounter++], offset });
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
            }
        }
        MOVEOFFSET()
        instructions.push_back(instruction{ jtable[insType::END], 0, 0, 0 });

        if (!braceStack.empty())
            return 2;

        instructions.shrink_to_fit();
    }
    
    auto cell = cells.data() + cellptr;
    auto insp = instructions.data();
    auto cellBase = cells.data();

    std::array<char, 1024> outBuffer = { 0 };
    const auto bufferBegin = outBuffer.begin();
    const auto bufferEnd = outBuffer.end();

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
    if (const ptrdiff_t dist = (cell + insp->data) - cellBase; dist >= cells.size()) // Should be enough for an infinite array
    {
        // Resolve pointers and stuff
        const ptrdiff_t currentCell = cell - cellBase;
        cells.resize(cells.size() / BFVM_DEFAULT_TAPE_SIZE * BFVM_DEFAULT_TAPE_SIZE); // Double it every time
        cellBase = cells.data();
        cell = cellBase + currentCell;
    }
        
    cell += insp->data;
    LOOP();

    _JMP_ZER:
    if (!*cell) [[unlikely]] insp += insp->data;
    LOOP();

    _JMP_NOT_ZER:
    if (*cell) [[likely]] insp -= insp->data;
    LOOP();

    _PUT_CHR:
    std::fill(bufferBegin, bufferBegin + insp->data, OFFCELL());
    std::fill(bufferBegin + insp->data, bufferEnd, 0);
    std::cout << outBuffer.data();
    LOOP();

    _RAD_CHR:
    if (const int in = std::cin.get(); in == EOF) {
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

    _SCN_RGT:
    while(true) {
        const simde__m64 emp = simde_mm_setzero_si64();
        const simde__m64 cellstc = simde_mm_set_pi8(*(cell + 7 * insp->data), *(cell + 6 * insp->data), *(cell + 5 * insp->data),
                                            *(cell + 4 * insp->data), *(cell + 3 * insp->data), *(cell + 2 * insp->data),
                                            *(cell + 1 * insp->data), *(cell));
        if (const auto idx = __builtin_ffsll(simde_m_to_int64(simde_mm_cmpeq_pi8(emp, cellstc))); idx) {
            cell += idx / 8 * insp->data;
            LOOP();
        }
        cell += 8 * insp->data;
    }
    
    _SCN_LFT:
    while(true) {
        const simde__m64 emp = simde_mm_setzero_si64();
        const simde__m64 cellstc = simde_mm_set_pi8(*(cell - 7 * insp->data), *(cell - 6 * insp->data), *(cell - 5 * insp->data),
                                            *(cell - 4 * insp->data), *(cell - 3 * insp->data), *(cell - 2 * insp->data),
                                            *(cell - 1 * insp->data), *(cell));
        if (const auto idx = __builtin_ffsll(simde_m_to_int64(simde_mm_cmpeq_pi8(emp, cellstc))); idx) {
            cell -= idx / 8 * insp->data;
            LOOP();
        }
        cell -= 8 * insp->data;
    }

    _END:
    cellptr = cell - cellBase;
    return 0;
}

void executeExcept(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize, int eof, bool term = false)
{
    switch (execute(cells, cellptr, code, optimize, eof, term)) {
    case 1:
        std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched close bracket";
        break;
    case 2:
        std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched open bracket";
        break;
    }
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
    std::vector<uint8_t> cells;
    cells.assign(BFVM_DEFAULT_TAPE_SIZE, 0);
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    } else if (!filename.empty()) {
        std::ifstream in(filename);
        if (std::string code; in.good()) {
            code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            executeExcept(cells, cellptr, code, optimize, eof);
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
                  << "Goof v1.2.1 - an optimizing brainfuck VM" << '\n'
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
                cells.assign(BFVM_DEFAULT_TAPE_SIZE, 0);
            } else if (repl.starts_with("dump")) {
                dumpMemory(cells, cellptr);
            } else if (repl.starts_with("exit") || repl.starts_with("quit")) {
                return 0;
            } else {
                executeExcept(cells, cellptr, repl, optimize, eof, true);
            }
        }
    }
}