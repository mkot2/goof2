#pragma once

#include <xxhash.h>

#include <cstdint>
#include <string_view>

inline std::uint64_t hashOutput(std::string_view s) { return XXH64(s.data(), s.size(), 0); }
