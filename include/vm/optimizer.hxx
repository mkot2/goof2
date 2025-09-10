#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory_resource>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

using SvMatch = std::match_results<std::string_view::const_iterator>;

namespace goof2 {

struct CountingResource : std::pmr::memory_resource {
    std::pmr::memory_resource* upstream;
    std::size_t bytes = 0;

    explicit CountingResource(std::pmr::memory_resource* up = std::pmr::new_delete_resource())
        : upstream(up) {}

   private:
    void* do_allocate(std::size_t s, std::size_t align) override {
        bytes += s;
        return upstream->allocate(s, align);
    }
    void do_deallocate(void* p, std::size_t s, std::size_t align) override {
        upstream->deallocate(p, s, align);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

inline std::string processBalanced(std::string_view s, char no1, char no2) {
    const auto total = std::ranges::count(s, no1) - std::ranges::count(s, no2);
    return std::string(std::abs(total), total > 0 ? no1 : no2);
}

template <typename Callback>
inline void regexReplaceInplace(std::string& str, const std::regex& re, Callback cb) {
    std::string_view sv{str};
    using Iterator = std::string_view::const_iterator;

    std::regex_iterator<Iterator> it(sv.begin(), sv.end(), re), end;
    if (it == end) return;

    std::string result;
    result.reserve(str.size());

    Iterator last = sv.begin();
    do {
        const SvMatch& m = *it;
        result.append(last, m[0].first);
        result += cb(m);
        last = m[0].second;
    } while (++it != end);
    result.append(last, sv.end());
    str = std::move(result);
}

struct RegexReplacement {
    size_t start;
    size_t end;
    std::string text;
    std::function<void()> sideEffect;
};

template <typename Callback>
inline std::pmr::vector<RegexReplacement> regexCollect(const std::string& str, const std::regex& re,
                                                       Callback cb, std::pmr::memory_resource* mr) {
    std::pmr::vector<RegexReplacement> reps{mr};
    reps.reserve(std::max<size_t>(8, str.size() / 16));
    std::string_view sv{str};
    using Iterator = std::string_view::const_iterator;
    for (std::regex_iterator<Iterator> it(sv.begin(), sv.end(), re), end; it != end; ++it) {
        const SvMatch& m = *it;
        auto [rep, eff] = cb(m);
        reps.push_back({static_cast<size_t>(m[0].first - sv.begin()),
                        static_cast<size_t>(m[0].second - sv.begin()), std::move(rep),
                        std::move(eff)});
    }
    return reps;
}

namespace vmRegex {
using namespace std::regex_constants;
extern const std::regex nonInstructionRe;
extern const std::regex balanceSeqRe;
extern const std::regex clearLoopRe;
extern const std::regex scanLoopClrRe;
extern const std::regex scanLoopRe;
extern const std::regex commaTrimRe;
extern const std::regex clearThenSetRe;
extern const std::regex copyLoopRe;
extern const std::regex leadingSetRe;
extern const std::regex copyLoopInnerRe;
extern const std::regex clearSeqRe;
extern const std::regex clearPassRe;
}  // namespace vmRegex

}  // namespace goof2
