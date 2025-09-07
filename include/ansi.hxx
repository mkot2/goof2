#pragma once
#include <string_view>

namespace ansi {
inline constexpr std::string_view red{"\x1b[31m"};
inline constexpr std::string_view green{"\x1b[32m"};
inline constexpr std::string_view yellow{"\x1b[33m"};
inline constexpr std::string_view reset{"\x1b[0m"};
inline constexpr std::string_view underline{"\x1b[4m"};
}  // namespace ansi
