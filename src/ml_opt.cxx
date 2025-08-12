/*
    Goof2 - An optimizing brainfuck VM
    Machine learning optimizer implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ml_opt.hxx"

#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace goof2 {
bool mlOptimizerEnabled = false;

static std::vector<std::pair<std::regex, std::string>> loadModel() {
    std::vector<std::pair<std::regex, std::string>> rules;
    std::ifstream in("assets/ml_model.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("//", 0) == 0) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        rules.emplace_back(std::regex(line.substr(0, tab)), line.substr(tab + 1));
    }
    return rules;
}

void applyMlOptimizer(std::string& code) {
    if (!mlOptimizerEnabled) return;
    static const auto rules = loadModel();
    for (const auto& [pattern, replacement] : rules) {
        code = std::regex_replace(code, pattern, replacement);
    }
}
}  // namespace goof2
