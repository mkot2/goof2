#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

static std::string run_inline(const std::string& code, const std::string& extra = "") {
    std::string cmd = std::string("'") + GOOF2_EXE_PATH + "' -e '" + code + "'" +
                      (extra.empty() ? "" : " " + extra) + " 2>&1";
    std::array<char, 256> buf{};
    std::string out;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    assert(pipe);
    while (fgets(buf.data(), buf.size(), pipe.get())) out += buf.data();
    int rc = pclose(pipe.release());
    assert(rc == 0);
    (void)rc;
    return out;
}

int main() {
    const std::string helloA = "++++++++[>++++++++<-]>+.";  // prints 'A'
    std::string out = run_inline(helloA);
    assert(out == "A");
    out = run_inline(helloA, "-nopt");
    assert(out == "A");
    out = run_inline(helloA, "-i nofile.bf");
    assert(out == "A");
    return 0;
}
