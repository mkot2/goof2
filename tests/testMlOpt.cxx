#include <cassert>
#include <string>

#include "ml_opt.hxx"

int main() {
    std::string s1 = "+-";
    goof2::applyMlOptimizer(s1);
    assert(s1 == "+");

    std::string s2 = "-+";
    goof2::applyMlOptimizer(s2);
    assert(s2 == "-");

    std::string s3 = "><";
    goof2::applyMlOptimizer(s3);
    assert(s3 == ">");

    return 0;
}
