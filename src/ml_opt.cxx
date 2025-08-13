/*
    Goof2 - An optimizing brainfuck VM
    Machine learning optimizer implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ml_opt.hxx"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace goof2 {
bool mlOptimizerEnabled = true;

static std::vector<std::pair<std::regex, std::string>> loadModel() {
    std::vector<std::pair<std::regex, std::string>> rules;
    std::ifstream in("assets/ml_model.txt");
    if (!in) {
        std::cerr << "warning: could not open ML model file assets/ml_model.txt" << std::endl;
        return rules;
    }
    std::string line;
    size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#' || line.rfind("//", 0) == 0) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) {
            std::cerr << "warning: skipping malformed rule at line " << lineNo
                      << ": missing tab delimiter" << std::endl;
            continue;
        }
        rules.emplace_back(std::regex(line.substr(0, tab)), line.substr(tab + 1));
    }
    if (rules.empty()) {
        std::cerr << "warning: no ML optimization rules loaded" << std::endl;
    }
    return rules;
}

void applyMlOptimizer(std::string& code) {
    if (!mlOptimizerEnabled) return;
    static const auto rules = loadModel();
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
