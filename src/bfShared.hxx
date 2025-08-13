#pragma once

/*
    Goof2 - An optimizing brainfuck VM
    Shared helpers for JIT and VM
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>

namespace goof2 {

inline int32_t fold(std::string_view code, size_t& i, char match) {
    int32_t count = 1;
    while (i < code.length() - 1 && code[i + 1] == match) {
        ++i;
        ++count;
    }
    return count;
}

inline std::string processBalanced(std::string_view s, char no1, char no2) {
    const auto total = std::count(s.begin(), s.end(), no1) - std::count(s.begin(), s.end(), no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

template <typename Callback>
inline void regexReplaceInplace(std::string& str, const std::regex& re, Callback cb) {
    std::string result;
    auto begin = str.cbegin();
    auto end = str.cend();
    std::smatch match;
    while (std::regex_search(begin, end, match, re)) {
        result.append(begin, match[0].first);
        result += cb(match);
        begin = match[0].second;
    }
    result.append(begin, end);
    str = std::move(result);
}

}  // namespace goof2
