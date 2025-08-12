/*
    Goof2 - JIT selection utilities
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstddef>

enum class JitMode { Auto, Force, Disable };

struct JitModel {
    double coefLength;
    double coefWidth;
    double intercept;
};

extern JitMode jitMode;
extern JitModel jitModel;
extern bool jitModelLoaded;

bool loadJitModel(const char* path, JitModel& model);
bool shouldUseJit(std::size_t programLength, int cellWidth);
