// This test previously relied on popen(3) with a shell command constructed from
// user-provided strings, which allowed command injection. Instead of invoking
// a shell we spawn the process directly and capture its output via a pipe.

#include <array>
#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static std::string run_inline(const std::string& code, const std::string& extra = "") {
#ifdef _WIN32
    // Windows fallback using _popen. The "code" parameter is restricted to the
    // Brainfuck instruction set to avoid command injection when constructing
    // the command line.
    auto safeChar = [](unsigned char c) {
        return c == '+' || c == '-' || c == '<' || c == '>' || c == '[' || c == ']' || c == '.' ||
               c == ',';
    };
    assert(std::all_of(code.begin(), code.end(), safeChar));
    std::string cmd = std::string("\"") + GOOF2_EXE_PATH + "\" -e \"" + code + "\"" +
                      (extra.empty() ? "" : " " + extra);
    std::array<char, 256> buf{};
    std::string out;
    FILE* pipe = _popen(cmd.c_str(), "r");
    assert(pipe);
    while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
    int rc = _pclose(pipe);
    assert(rc == 0);
    (void)rc;
    return out;
#else
    std::vector<std::string> args{GOOF2_EXE_PATH, "-e", code};
    std::istringstream iss(extra);
    for (std::string tok; iss >> tok;) args.push_back(tok);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);

    int pipefd[2];
    assert(pipe(pipefd) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::array<char, 256> buf{};
    std::string out;
    ssize_t n;
    while ((n = read(pipefd[0], buf.data(), buf.size())) > 0) {
        out.append(buf.data(), static_cast<size_t>(n));
    }
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return out;
#endif
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
