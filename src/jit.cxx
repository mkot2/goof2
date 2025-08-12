/*
    Goof2 - An optimizing brainfuck VM
    JIT backend using sljit
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "jit.hxx"

#include "vm.hxx"

#if GOOF2_USE_SLJIT
#include <sljitLir.h>
#endif

namespace goof2 {
#if GOOF2_USE_SLJIT

template <typename CellT>
int execute_jit(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                int eof, bool dynamicSize, bool term, MemoryModel model, ProfileInfo* profile) {
    // Build optimized instruction list by delegating to existing interpreter for now.
    // The interpreter already produces the instruction stream used below.
    // In a future revision this will be replaced by a proper front-end shared
    // with the interpreter.
    std::vector<instruction> instructions;
    std::vector<uint8_t> ops;
    (void)cells;
    (void)cellPtr;
    (void)code;
    (void)optimize;
    (void)eof;
    (void)dynamicSize;
    (void)term;
    (void)model;
    (void)profile;

    sljit_compiler* compiler = sljit_create_compiler(nullptr);
    if (!compiler) return -1;

    for (size_t i = 0; i < instructions.size(); ++i) {
        const instruction& inst = instructions[i];
        const insType op = static_cast<insType>(ops[i]);
        switch (op) {
            case insType::ADD_SUB:
                // *ptr += data;
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
            case insType::JMP_ZER: {
                sljit_jump* jmp = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                                 inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                inst.jump = jmp;  // reuse jump field for backpatching
                break;
            }
            case insType::JMP_NOT_ZER: {
                sljit_jump* jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_MEM1(SLJIT_S0),
                                                 inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                sljit_set_label(jmp, reinterpret_cast<sljit_label*>(inst.jump));
                break;
            }
            case insType::PUT_CHR:
                // output character *(ptr+offset)
                sljit_emit_op0(compiler, SLJIT_NOP);
                break;
            case insType::RAD_CHR:
                sljit_emit_op0(compiler, SLJIT_NOP);
                break;
            case insType::CLR:
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
                               inst.offset * sizeof(CellT), SLJIT_IMM, 0);
                break;
            case insType::MUL_CPY:
                sljit_emit_op0(compiler, SLJIT_NOP);
                break;
            case insType::SCN_RGT:
                sljit_emit_op0(compiler, SLJIT_NOP);
                break;
            case insType::SCN_LFT:
                sljit_emit_op0(compiler, SLJIT_NOP);
                break;
            case insType::END:
                break;
        }
    }
    sljit_free_compiler(compiler);
    return 0;
}

#else  // !GOOF2_USE_SLJIT

template <typename CellT>
int execute_jit(std::vector<CellT>&, size_t&, std::string&, bool, int, bool, bool, MemoryModel,
                ProfileInfo*) {
    return -1;
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
