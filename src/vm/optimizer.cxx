#include "vm/optimizer.hxx"

namespace goof2::vmRegex {
using namespace std::regex_constants;
const std::regex nonInstructionRe(R"([^+\-<>\.,\]\[])", optimize | nosubs);
const std::regex balanceSeqRe(R"([+-]{2,}|[><]{2,})", optimize | nosubs);
const std::regex clearLoopRe(R"([+-]*\[[+-]+\](?:\[[+-]+\])*)", optimize | nosubs);
const std::regex scanLoopClrRe(R"(\[-[<>]+\]|\[[<>]\[-\]\])", optimize | nosubs);
const std::regex scanLoopRe(R"(\[[<>]+\])", optimize | nosubs);
const std::regex commaTrimRe(R"([+\-C]+,)", optimize | nosubs);
const std::regex clearThenSetRe(R"(C([+-]+))", optimize);
const std::regex copyLoopRe(R"(\[-((?:[<>]+[+-]+)+)[<>]+\]|\[((?:[<>]+[+-]+)+)[<>]+-\])", optimize);
const std::regex leadingSetRe(R"((?:^|([RL\]]))C*([\+\-]+))", optimize);
const std::regex copyLoopInnerRe(R"((?:<+|>+)[+-]+)", optimize | nosubs);
const std::regex clearSeqRe(R"(C{2,})", optimize | nosubs);
const std::regex clearPassRe(R"((C([+-]+))|C{2,})", optimize);
}  // namespace goof2::vmRegex
