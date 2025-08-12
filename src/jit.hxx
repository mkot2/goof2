/*
    Goof2 - An optimizing brainfuck VM
    JIT internal definitions
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

struct instruction {
    const void* jump;
    int32_t data;
    int16_t auxData;
    int16_t offset;
};

enum class insType : uint8_t {
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
