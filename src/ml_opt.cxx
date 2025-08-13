/*
    Goof2 - An optimizing brainfuck VM
    Machine learning optimizer implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ml_opt.hxx"

#include <regex>
#include <string>
#include <vector>

#include "mlModel.hxx"

namespace goof2 {
bool mlOptimizerEnabled = true;

static const std::vector<std::pair<std::regex, std::string>>& modelRules() {
    static const auto rules = [] {
        std::vector<std::pair<std::regex, std::string>> out;
        out.reserve(mlModelCount);
        for (size_t i = 0; i < mlModelCount; ++i) {
            out.emplace_back(std::regex(mlModel[i].pattern), mlModel[i].replacement);
        }
        return out;
    }();
    return rules;
}

void applyMlOptimizer(std::string& code) {
    if (!mlOptimizerEnabled) return;
    static const auto& rules = modelRules();
    bool replaced;
    do {
        replaced = false;
        for (const auto& [pattern, replacement] : rules) {
            std::string newCode = std::regex_replace(code, pattern, replacement);
            if (newCode != code) {
                replaced = true;
                code = std::move(newCode);
            }
        }
    } while (replaced);
}
}  // namespace goof2
