/*
    Goof2 - An optimizing brainfuck VM
    JIT backend using sljit
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "jit.hxx"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <regex>
#include <string_view>
#include <vector>

#include "bfShared.hxx"
#include "ml_opt.hxx"
#include "parallel.hxx"
#include "vm.hxx"

#if GOOF2_USE_SLJIT
#include <sljitLir.h>
#endif

namespace goof2 {
#if GOOF2_USE_SLJIT

template <typename CellT>
static int buildInstructions(std::string& code, bool optimize,
                             std::vector<instruction>& instructions, std::vector<uint8_t>& ops,
                             ProfileInfo* profile) {
    int copyloopCounter = 0;
    std::vector<int> copyloopMap;
    int scanloopCounter = 0;
    std::vector<int> scanloopMap;

    if (optimize) {
        code = std::regex_replace(code, std::regex(R"([^+\-<>\.,\]\[])"), "");

        regexReplaceInplace(code, std::regex(R"([+-]{2,})"), [&](const std::smatch& what) {
            return processBalanced(what.str(), '+', '-');
        });
        regexReplaceInplace(code, std::regex(R"([><]{2,})"), [&](const std::smatch& what) {
            return processBalanced(what.str(), '>', '<');
        });

        code = std::regex_replace(code, std::regex(R"([+-]*(?:\[[+-]+\])+)"), "C");

        regexReplaceInplace(code, std::regex(R"(\[>+\]|\[<+\])"), [&](const std::smatch& what) {
            const auto current = what.str();
            const auto count = std::count(current.begin(), current.end(), '>') -
                               std::count(current.begin(), current.end(), '<');
            scanloopMap.push_back(std::abs(count));
            if (count > 0)
                return std::string("R");
            else
                return std::string("L");
        });

        code = std::regex_replace(code, std::regex(R"([+\-C]+,)"), ",");

        regexReplaceInplace(
            code, std::regex(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])"),
            [&](const std::smatch& what) {
                int numOfCopies = 0;
                int offset = 0;
                const std::string whole = what.str();
                const std::string current = what[1].str() + what[2].str();

                if (std::count(whole.begin(), whole.end(), '>') -
                        std::count(whole.begin(), whole.end(), '<') ==
                    0) {
                    std::smatch whatL;
                    auto start = current.cbegin();
                    auto end = current.cend();
                    std::regex inner(R"([<>]+[+-]+)");
                    while (std::regex_search(start, end, whatL, inner)) {
                        offset += -std::count(whatL[0].first, whatL[0].second, '<') +
                                  std::count(whatL[0].first, whatL[0].second, '>');
                        copyloopMap.push_back(offset);
                        copyloopMap.push_back(std::count(whatL[0].first, whatL[0].second, '+') -
                                              std::count(whatL[0].first, whatL[0].second, '-'));
                        numOfCopies++;
                        start = whatL[0].second;
                    }
                    return std::string(numOfCopies, 'P') + "C";
                } else {
                    return whole;
                }
            });

        code = std::regex_replace(code, std::regex(R"((?:^|([RL\]]))C*([\+\-]+))"), "$1S$2");
    }

    applyMlOptimizer(code);

    std::vector<size_t> braceStack;
    std::vector<int> loopIdStack;
    int loopCounter = 0;
    int16_t offset = 0;
    bool set = false;
    instructions.reserve(code.length());
    ops.reserve(code.length());

    auto emit = [&](insType op, instruction inst) {
        if (!instructions.empty() && instructions.back().offset == inst.offset) {
            auto& last = instructions.back();
            insType lastOp = static_cast<insType>(ops.back());
            bool lastIsWrite =
                lastOp == insType::ADD_SUB || lastOp == insType::SET || lastOp == insType::CLR;
            bool newIsWrite = op == insType::ADD_SUB || op == insType::SET || op == insType::CLR;
            if (lastIsWrite && newIsWrite) {
                if (op == insType::ADD_SUB) {
                    if (lastOp == insType::ADD_SUB) {
                        last.data += inst.data;
                        return;
                    } else if (lastOp == insType::SET) {
                        last.data = static_cast<int32_t>(static_cast<CellT>(last.data + inst.data));
                        return;
                    } else if (lastOp == insType::CLR) {
                        instructions.pop_back();
                        ops.pop_back();
                        instructions.push_back(instruction{
                            nullptr, static_cast<int32_t>(static_cast<CellT>(inst.data)), 0,
                            inst.offset});
                        ops.push_back(static_cast<uint8_t>(insType::SET));
                        return;
                    }
                } else {
                    instructions.pop_back();
                    ops.pop_back();
                }
            }
        }
        instructions.push_back(inst);
        ops.push_back(static_cast<uint8_t>(op));
    };

    auto moveOffset = [&]() {
        if (offset) {
            emit(insType::PTR_MOV, instruction{nullptr, offset, 0, 0});
            offset = 0;
        }
    };

    for (size_t i = 0; i < code.length(); ++i) {
        switch (code[i]) {
            case '+': {
                const int32_t folded = fold(code, i, '+');
                const insType op = set ? insType::SET : insType::ADD_SUB;
                emit(op,
                     instruction{nullptr,
                                 set ? static_cast<int32_t>(static_cast<CellT>(folded)) : folded, 0,
                                 offset});
                set = false;
                break;
            }
            case '-': {
                const int32_t folded = -fold(code, i, '-');
                const insType op = set ? insType::SET : insType::ADD_SUB;
                emit(op,
                     instruction{nullptr,
                                 set ? static_cast<int32_t>(static_cast<CellT>(folded)) : folded, 0,
                                 offset});
                set = false;
                break;
            }
            case '>':
                offset += static_cast<int16_t>(fold(code, i, '>'));
                break;
            case '<':
                offset -= static_cast<int16_t>(fold(code, i, '<'));
                break;
            case '[':
                moveOffset();
                braceStack.push_back(instructions.size());
                loopIdStack.push_back(loopCounter);
                emit(insType::JMP_ZER,
                     instruction{nullptr, 0, static_cast<int16_t>(loopCounter), 0});
                ++loopCounter;
                break;
            case ']': {
                if (!braceStack.size()) return 1;

                moveOffset();
                const int start = braceStack.back();
                const int loopId = loopIdStack.back();
                const int sizeminstart = instructions.size() - start;
                braceStack.pop_back();
                loopIdStack.pop_back();
                instructions[start].data = sizeminstart;
                instructions[start].auxData = static_cast<int16_t>(loopId);
                emit(insType::JMP_NOT_ZER,
                     instruction{nullptr, sizeminstart, static_cast<int16_t>(loopId), 0});
                break;
            }
            case '.':
                emit(insType::PUT_CHR, instruction{nullptr, fold(code, i, '.'), 0, offset});
                break;
            case ',':
                emit(insType::RAD_CHR, instruction{nullptr, 0, 0, offset});
                break;
            case 'C':
                emit(insType::CLR, instruction{nullptr, 0, 0, offset});
                break;
            case 'P':
                emit(insType::MUL_CPY,
                     instruction{nullptr, copyloopMap[copyloopCounter++],
                                 static_cast<int16_t>(copyloopMap[copyloopCounter++]), offset});
                break;
            case 'R':
                moveOffset();
                emit(insType::SCN_RGT, instruction{nullptr, scanloopMap[scanloopCounter++], 0, 0});
                break;
            case 'L':
                moveOffset();
                emit(insType::SCN_LFT, instruction{nullptr, scanloopMap[scanloopCounter++], 0, 0});
                break;
            case 'S':
                set = true;
                break;
        }
    }
    moveOffset();
    emit(insType::END, instruction{nullptr, 0, 0, 0});

    if (!braceStack.empty()) return 2;

    instructions.shrink_to_fit();
    if (profile) profile->loopCounts.resize(loopCounter);
    return 0;
}

extern "C" int jit_getchar() {
    std::lock_guard<std::mutex> lock(goof2::ioMutex);
    return std::cin.get();
}
template <typename CellT>
static void monitorHotLoops(ProfileInfo* profile, std::string& code) {
    constexpr std::uint64_t HOT_LOOP_THRESHOLD = 1000;
    if (!profile) return;
    for (auto count : profile->loopCounts) {
        if (count > HOT_LOOP_THRESHOLD) {
            std::vector<instruction> tmpInst;
            std::vector<uint8_t> tmpOps;
            buildInstructions<CellT>(code, true, tmpInst, tmpOps, profile);
            break;
        }
    }
}

extern "C" void jit_putchar(int ch) {
    std::lock_guard<std::mutex> lock(goof2::ioMutex);
    std::cout.put(static_cast<char>(ch));
    std::cout.flush();
}

template <typename CellT>
int execute_jit(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, bool dynamicSize, bool term, MemoryModel model, ProfileInfo* profile) {
    std::vector<instruction> instructions;
    std::vector<uint8_t> ops;
    buildInstructions<CellT>(code, optimize, instructions, ops, profile);

    sljit_compiler* compiler = sljit_create_compiler(nullptr);
    int ret = 0;
    if (compiler) {
        sljit_emit_enter(compiler, 0, SLJIT_ARGS1(W, W), 2, 2, 0, 0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0, SLJIT_R0, 0);
        if (profile && !profile->loopCounts.empty()) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM,
                           (sljit_sw)profile->loopCounts.data());
        }
        std::vector<sljit_label*> labels(instructions.size());
        std::vector<sljit_jump*> jumps(instructions.size(), nullptr);
        for (size_t i = 0; i < instructions.size(); ++i) {
            labels[i] = sljit_emit_label(compiler);
            const instruction& inst = instructions[i];
            const insType op = static_cast<insType>(ops[i]);
            switch (op) {
                case insType::ADD_SUB:
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT), SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT), SLJIT_IMM, inst.data);
                    break;
                case insType::SET:
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT), SLJIT_IMM, inst.data);
                    break;
                case insType::PTR_MOV:
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0, SLJIT_S0, 0, SLJIT_IMM,
                                   inst.data * sizeof(CellT));
                    break;
                case insType::JMP_ZER:
                    jumps[i] = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                              inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                    break;
                case insType::JMP_NOT_ZER:
                    if (profile && !profile->loopCounts.empty()) {
                        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                                       static_cast<sljit_sw>(inst.auxData * sizeof(std::uint64_t)));
                        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 1);
                        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S1),
                                       static_cast<sljit_sw>(inst.auxData * sizeof(std::uint64_t)),
                                       SLJIT_R1, 0);
                    }
                    jumps[i] = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                              inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                    break;
                case insType::PUT_CHR: {
                    sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT));
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(W, W), SLJIT_IMM,
                                     SLJIT_FUNC_ADDR(jit_putchar));
                    break;
                }
                case insType::RAD_CHR: {
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS0(W), SLJIT_IMM,
                                     SLJIT_FUNC_ADDR(jit_getchar));
                    sljit_jump* eof_jmp =
                        sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, -1);
                    sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT), SLJIT_R0, 0);
                    sljit_label* after = sljit_emit_label(compiler);
                    sljit_set_label(eof_jmp, after);
                    break;
                }
                case insType::CLR:
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                    break;
                case insType::MUL_CPY:
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0),
                                   inst.offset * sizeof(CellT));
                    sljit_emit_op2(compiler, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM,
                                   inst.auxData);
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0),
                                   (inst.offset + inst.data) * sizeof(CellT), SLJIT_MEM1(SLJIT_S0),
                                   (inst.offset + inst.data) * sizeof(CellT), SLJIT_R0, 0);
                    break;
                case insType::SCN_RGT: {
                    sljit_label* loop = sljit_emit_label(compiler);
                    sljit_jump* exit = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                                      0, SLJIT_IMM, 0);
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0, SLJIT_S0, 0, SLJIT_IMM,
                                   inst.data * sizeof(CellT));
                    sljit_jump* back = sljit_emit_jump(compiler, SLJIT_JUMP);
                    sljit_set_label(back, loop);
                    sljit_label* end = sljit_emit_label(compiler);
                    sljit_set_label(exit, end);
                    break;
                }
                case insType::SCN_LFT: {
                    sljit_label* loop = sljit_emit_label(compiler);
                    sljit_jump* exit = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                                      0, SLJIT_IMM, 0);
                    sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_S0, 0, SLJIT_S0, 0, SLJIT_IMM,
                                   inst.data * sizeof(CellT));
                    sljit_jump* back = sljit_emit_jump(compiler, SLJIT_JUMP);
                    sljit_set_label(back, loop);
                    sljit_label* end = sljit_emit_label(compiler);
                    sljit_set_label(exit, end);
                    break;
                }
                case insType::END:
                    sljit_emit_return(compiler, SLJIT_MOV, SLJIT_S0, 0);
                    break;
            }
        }
        goof2::parallelFor(0, instructions.size(), [&](size_t i) {
            if (ops[i] == static_cast<uint8_t>(insType::JMP_ZER)) {
                size_t target = i + instructions[i].data;
                if (jumps[i]) sljit_set_label(jumps[i], labels[target]);
            } else if (ops[i] == static_cast<uint8_t>(insType::JMP_NOT_ZER)) {
                size_t target = i - instructions[i].data;
                if (jumps[i]) sljit_set_label(jumps[i], labels[target]);
            }
        });
        void* codeptr = sljit_generate_code(compiler, 0, nullptr);
        using JitFunc = CellT* (*)(CellT*);
        auto func = reinterpret_cast<JitFunc>(codeptr);
        CellT* ptr = cells.data() + cellPtr;
        CellT* endPtr = func(ptr);
        cellPtr = static_cast<size_t>(endPtr - cells.data());
        sljit_free_code(codeptr, nullptr);
        sljit_free_compiler(compiler);
    } else {
        ret =
            execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, term, model, profile);
    }

    monitorHotLoops<CellT>(profile, code);
    return ret;
}

#else  // !GOOF2_USE_SLJIT

template <typename CellT>
int execute_jit(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, bool dynamicSize, bool term, MemoryModel model, ProfileInfo* profile) {
    return execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, term, model, profile);
}

#endif  // GOOF2_USE_SLJIT

template int execute_jit<uint8_t>(std::vector<uint8_t>&, size_t&, std::string&, bool, int, bool,
                                  bool, MemoryModel, ProfileInfo*);
template int execute_jit<uint16_t>(std::vector<uint16_t>&, size_t&, std::string&, bool, int, bool,
                                   bool, MemoryModel, ProfileInfo*);
template int execute_jit<uint32_t>(std::vector<uint32_t>&, size_t&, std::string&, bool, int, bool,
                                   bool, MemoryModel, ProfileInfo*);
template int execute_jit<uint64_t>(std::vector<uint64_t>&, size_t&, std::string&, bool, int, bool,
                                   bool, MemoryModel, ProfileInfo*);

}  // namespace goof2
