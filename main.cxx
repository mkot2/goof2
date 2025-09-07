/*
    Goof2 - An optimizing brainfuck VM
    Main standalone file
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "vm.hxx"
#ifdef GOOF2_ENABLE_REPL
#include "cpp-terminal/color.hpp"
#include "cpp-terminal/style.hpp"
#include "repl.hxx"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// Cross-platform read-only file mapping and BF compaction
struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = nullptr;
#else
    int fd = -1;
#endif
    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept { *this = std::move(other); }
    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            this->close();
            data = other.data;
            size = other.size;
#ifdef _WIN32
            hFile = other.hFile;
            hMap = other.hMap;
            other.hFile = INVALID_HANDLE_VALUE;
            other.hMap = nullptr;
#else
            fd = other.fd;
            other.fd = -1;
#endif
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
    ~MappedFile() { close(); }
    void close() {
#ifdef _WIN32
        if (data) UnmapViewOfFile((LPCVOID)data);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (data && size) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            munmap(const_cast<char*>(data), size);
        }
        if (fd >= 0) ::close(fd);
#endif
        data = nullptr;
        size = 0;
    }
};

static bool isBfChar(char c) {
    switch (c) {
        case '+':
        case '-':
        case '>':
        case '<':
        case '[':
        case ']':
        case '.':
        case ',':
            return true;
        default:
            return false;
    }
}

static bool mapFileReadOnly(const std::string& path, MappedFile& mf) {
#ifdef _WIN32
    // Use narrow WinAPI for simplicity; CLI passes narrow paths
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file, &sz)) {
        CloseHandle(file);
        return false;
    }
    if (sz.QuadPart == 0) {
        mf.hFile = file;
        mf.hMap = nullptr;
        mf.data = nullptr;
        mf.size = 0;
        return true;
    }
    HANDLE map = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map) {
        CloseHandle(file);
        return false;
    }
    void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(map);
        CloseHandle(file);
        return false;
    }
    mf.hFile = file;
    mf.hMap = map;
    mf.data = static_cast<const char*>(view);
    mf.size = static_cast<size_t>(sz.QuadPart);
    return true;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return false;
    }
    if (st.st_size == 0) {
        mf.fd = fd;
        mf.data = nullptr;
        mf.size = 0;
        return true;
    }
    void* view = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    mf.fd = fd;
    mf.data = static_cast<const char*>(view);
    mf.size = static_cast<size_t>(st.st_size);
    return true;
#endif
}

// Reads and compacts BF source (keeps only +-<>[],.) using a memory-mapped file when possible.
// Returns true on success; on error, 'err' is set and 'out' left unchanged.
static bool readBfFileCompacted(const std::string& filename, std::string& out, std::string& err) {
    MappedFile mf;
    if (mapFileReadOnly(filename, mf)) {
        const char* p = mf.data;
        const size_t n = mf.size;
        if (n == 0 || p == nullptr) {
            out.clear();
            return true;
        }
        out.clear();
        out.reserve(n);
        // Single pass: append valid Brainfuck ops
        for (size_t i = 0; i < n; ++i) {
            if (isBfChar(p[i])) out.push_back(p[i]);
        }
        mf.close();
        return true;
    }
    // Fallback: stream and compact
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        err = "File could not be opened";
        return false;
    }
    std::string compact;
    compact.reserve(1 << 16);
    char c;
    while (in.get(c)) {
        if (isBfChar(c)) compact.push_back(c);
    }
    if (!in.eof() && in.fail()) {
        err = "Error while reading file";
        return false;
    }
    out.swap(compact);
    return true;
}

struct CmdArgs {
    std::string filename;
    std::string evalCode;
    bool dumpMemory = false;
    bool help = false;
    bool optimize = true;
    bool dynamicTape = false;
    bool profile = false;
    int eof = 0;
    std::size_t tapeSize = 30000;
    int cellWidth = 8;
    goof2::MemoryModel model = goof2::MemoryModel::Auto;
};

CmdArgs parseArgs(int argc, char* argv[]) {
    CmdArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-e" && i + 1 < argc) {
            args.evalCode = argv[++i];
            args.filename.clear();
        } else if (arg == "-i" && i + 1 < argc && args.evalCode.empty()) {
            args.filename = argv[++i];
        } else if (arg == "-dm") {
            args.dumpMemory = true;
        } else if (arg == "-h") {
            args.help = true;
        } else if (arg == "-nopt") {
            args.optimize = false;
        } else if (arg == "-dts") {
            args.dynamicTape = true;
        } else if (arg == "-eof" && i + 1 < argc) {
            const char* val = argv[++i];
            char* end = nullptr;
            long parsed = std::strtol(val, &end, 10);
            if (end == val || *end != '\0') {
                std::cerr << "Invalid EOF mode: " << val << std::endl;
                args.help = true;
            } else {
                args.eof = static_cast<int>(parsed);
            }
        } else if (arg == "-ts" && i + 1 < argc) {
            const char* val = argv[++i];
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(val, &end, 10);
            if (val[0] == '-' || end == val || *end != '\0' || parsed == 0) {
                std::cerr << "Tape size must be a positive integer: " << val << std::endl;
                args.help = true;
            } else {
                args.tapeSize = static_cast<std::size_t>(parsed);
            }
        } else if (arg == "-cw" && i + 1 < argc) {
            const char* val = argv[++i];
            char* end = nullptr;
            long parsed = std::strtol(val, &end, 10);
            if (end == val || *end != '\0' || parsed <= 0) {
                std::cerr << "Cell width must be a positive integer: " << val << std::endl;
                args.help = true;
            } else {
                args.cellWidth = static_cast<int>(parsed);
            }
        } else if (arg == "--profile") {
            args.profile = true;
        } else if (arg == "-mm" && i + 1 < argc) {
            std::string mm = argv[++i];
            std::transform(mm.begin(), mm.end(), mm.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (mm == "contiguous") {
                args.model = goof2::MemoryModel::Contiguous;
            } else if (mm == "fibonacci") {
                args.model = goof2::MemoryModel::Fibonacci;
            } else if (mm == "paged") {
                args.model = goof2::MemoryModel::Paged;
            } else if (mm == "os") {
                args.model = goof2::MemoryModel::OSBacked;
            } else {
                std::cerr << "Unknown memory model: " << mm << std::endl;
                args.help = true;
            }
        }
    }
    return args;
}
void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -e <code>        Execute Brainfuck code directly\n"
              << "  -i <file>        Execute code from file\n"
              << "  -dm              Dump memory after program\n"
              << "  -nopt            Disable optimizations\n"
              << "  -dts             Enable dynamic tape resizing\n"
              << "  -eof <value>     Set EOF return value\n"
              << "  -ts <size>       Tape size in cells (default 30000)\n"
              << "  -cw <width>      Cell width in bits (8,16,32,64)\n"
              << "  --profile        Print execution profile\n"
              << "  -mm <model>      Memory model (auto, contiguous, fibonacci, paged, os)\n"
              << "  -h               Show this help message" << std::endl;
}
}  // namespace

#ifdef GOOF2_ENABLE_REPL
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Enable ANSI escape sequences on Windows terminals
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode)) {
                mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, mode);
            }
        }
    }
#endif
    CmdArgs opts = parseArgs(argc, argv);
    std::string filename = opts.filename;
    std::string evalCode = opts.evalCode;
    const bool dumpMemoryFlag = opts.dumpMemory;
    const bool help = opts.help;
    ReplConfig cfg{opts.optimize,
                   opts.dynamicTape,
                   opts.eof,
                   opts.tapeSize,
                   opts.cellWidth,
                   opts.model,
                   true,
                   false,
                   0};
    const bool profile = opts.profile;
    if (cfg.tapeSize == 0) {
        std::cout << Term::color_fg(Term::Color::Name::Red)
                  << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                  << " Tape size must be positive; using default 30000" << std::endl;
        cfg.tapeSize = 30000;
    }
    const std::size_t widthBytes = static_cast<std::size_t>(cfg.cellWidth / 8);
    if (widthBytes == 0 || cfg.tapeSize > (GOOF2_TAPE_MAX_BYTES / widthBytes)) {
        std::cout << Term::color_fg(Term::Color::Name::Red)
                  << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                  << " Requested tape exceeds maximum allowed size ("
                  << (GOOF2_TAPE_MAX_BYTES >> 20) << " MiB)" << std::endl;
        return 1;
    }
    std::size_t requiredMem = cfg.tapeSize * widthBytes;
    if (requiredMem > GOOF2_TAPE_WARN_BYTES) {
        std::cout << Term::color_fg(Term::Color::Name::Yellow)
                  << "WARNING:" << Term::color_fg(Term::Color::Name::Default)
                  << " Tape allocation ~" << (requiredMem >> 20) << " MiB may exceed system memory"
                  << std::endl;
    }
    if (help) {
        printHelp(argv[0]);
        return 0;
    }
    if (!evalCode.empty()) {
        size_t cellPtr = 0;
        std::string code = evalCode;
        switch (cfg.cellWidth) {
            case 8: {
                std::vector<uint8_t> cells(cfg.tapeSize, 0);
                executeExcept<uint8_t>(cells, cellPtr, code, cfg.optimize, cfg.eof, cfg.dynamicSize,
                                       cfg.model);
                if (dumpMemoryFlag) dumpMemory<uint8_t>(cells, cellPtr);
                break;
            }
            case 16: {
                std::vector<uint16_t> cells(cfg.tapeSize, 0);
                executeExcept<uint16_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model);
                if (dumpMemoryFlag) dumpMemory<uint16_t>(cells, cellPtr);
                break;
            }
            case 32: {
                std::vector<uint32_t> cells(cfg.tapeSize, 0);
                executeExcept<uint32_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model);
                if (dumpMemoryFlag) dumpMemory<uint32_t>(cells, cellPtr);
                break;
            }
            case 64: {
                std::vector<uint64_t> cells(cfg.tapeSize, 0);
                executeExcept<uint64_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model);
                if (dumpMemoryFlag) dumpMemory<uint64_t>(cells, cellPtr);
                break;
            }
            default:
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                          << " Unsupported cell width; use 8,16,32,64" << std::endl;
                return 1;
        }
        return 0;
    }
    if (!filename.empty()) {
        size_t cellPtr = 0;
        std::string code;
        {
            std::string err;
            if (!readBfFileCompacted(filename, code, err)) {
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default) << ' ' << err
                          << std::endl;
                return 1;
            }
        }
        goof2::ProfileInfo profileInfo;
        goof2::ProfileInfo* profPtr = profile ? &profileInfo : nullptr;
        switch (cfg.cellWidth) {
            case 8: {
                std::vector<uint8_t> cells(cfg.tapeSize, 0);
                executeExcept<uint8_t>(cells, cellPtr, code, cfg.optimize, cfg.eof, cfg.dynamicSize,
                                       cfg.model, profPtr);
                if (dumpMemoryFlag) dumpMemory<uint8_t>(cells, cellPtr);
                break;
            }
            case 16: {
                std::vector<uint16_t> cells(cfg.tapeSize, 0);
                executeExcept<uint16_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model, profPtr);
                if (dumpMemoryFlag) dumpMemory<uint16_t>(cells, cellPtr);
                break;
            }
            case 32: {
                std::vector<uint32_t> cells(cfg.tapeSize, 0);
                executeExcept<uint32_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model, profPtr);
                if (dumpMemoryFlag) dumpMemory<uint32_t>(cells, cellPtr);
                break;
            }
            case 64: {
                std::vector<uint64_t> cells(cfg.tapeSize, 0);
                executeExcept<uint64_t>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                        cfg.dynamicSize, cfg.model, profPtr);
                if (dumpMemoryFlag) dumpMemory<uint64_t>(cells, cellPtr);
                break;
            }
            default:
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                          << " Unsupported cell width; use 8,16,32,64" << std::endl;
                return 1;
        }
        if (profile) {
            std::cout << "Instructions executed: " << profileInfo.instructions << std::endl;
            std::cout << "Elapsed time: " << profileInfo.seconds << "s" << std::endl;
        }
        return 0;
    }
    while (true) {
        size_t cellPtr = 0;
        int newCw = 0;
        switch (cfg.cellWidth) {
            case 8: {
                std::vector<uint8_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint8_t>(cells, cellPtr, cfg);
                break;
            }
            case 16: {
                std::vector<uint16_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint16_t>(cells, cellPtr, cfg);
                break;
            }
            case 32: {
                std::vector<uint32_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint32_t>(cells, cellPtr, cfg);
                break;
            }
            case 64: {
                std::vector<uint64_t> cells(cfg.tapeSize, 0);
                newCw = runRepl<uint64_t>(cells, cellPtr, cfg);
                break;
            }
            default:
                std::cout << Term::color_fg(Term::Color::Name::Red)
                          << "ERROR:" << Term::color_fg(Term::Color::Name::Default)
                          << " Unsupported cell width; use 8,16,32,64" << std::endl;
                return 1;
        }
        if (newCw == 0) break;
        cfg.cellWidth = newCw;
    }
    return 0;
}
#else   // !GOOF2_ENABLE_REPL
namespace {
template <typename CellT>
void dumpMemory(const std::vector<CellT>& cells, size_t cellPtr) {
    if (cells.empty()) {
        std::cout << "Memory dump:\n<empty>" << std::endl;
        return;
    }
    size_t lastNonEmpty = cells.size() - 1;
    while (lastNonEmpty > cellPtr && lastNonEmpty > 0 && !cells[lastNonEmpty]) {
        --lastNonEmpty;
    }
    std::cout << "Memory dump:" << std::endl;
    for (size_t i = 0; i <= lastNonEmpty; ++i) {
        if (i == cellPtr) std::cout << '[';
        std::cout << +cells[i];
        if (i == cellPtr) std::cout << ']';
        std::cout << (i % 10 == 9 ? '\n' : ' ');
    }
    if (lastNonEmpty % 10 != 9) std::cout << std::endl;
}

template <typename CellT>
void executeExcept(std::vector<CellT>& cells, size_t& cellPtr, std::string& code, bool optimize,
                   int eof, bool dynamicSize, goof2::MemoryModel model,
                   goof2::ProfileInfo* profile) {
    int ret = goof2::execute<CellT>(cells, cellPtr, code, optimize, eof, dynamicSize, false, model,
                                    profile);
    switch (ret) {
        case 1:
            std::cerr << "ERROR: Unmatched close bracket";
            break;
        case 2:
            std::cerr << "ERROR: Unmatched open bracket";
            break;
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    CmdArgs opts = parseArgs(argc, argv);

    std::string filename = opts.filename;
    std::string evalCode = opts.evalCode;
    const bool dumpMemoryFlag = opts.dumpMemory;
    const bool help = opts.help;
    bool optimize = opts.optimize;
    bool dynamicSize = opts.dynamicTape;
    int eof = opts.eof;
    std::size_t tapeSize = opts.tapeSize;
    int cellWidth = opts.cellWidth;
    goof2::MemoryModel model = opts.model;
    const bool profile = opts.profile;
    if (tapeSize == 0) {
        std::cout << "ERROR: Tape size must be positive; using default 30000" << std::endl;
        tapeSize = 30000;
    }
    const std::size_t widthBytes = static_cast<std::size_t>(cellWidth / 8);
    if (widthBytes == 0 || tapeSize > (GOOF2_TAPE_MAX_BYTES / widthBytes)) {
        std::cerr << "ERROR: Requested tape exceeds maximum allowed size ("
                  << (GOOF2_TAPE_MAX_BYTES >> 20) << " MiB)" << std::endl;
        return 1;
    }
    std::size_t requiredMem = tapeSize * widthBytes;
    if (requiredMem > GOOF2_TAPE_WARN_BYTES) {
        std::cout << "WARNING: Tape allocation ~" << (requiredMem >> 20)
                  << " MiB may exceed system memory" << std::endl;
    }
    if (help) {
        printHelp(argv[0]);
        return 0;
    }
    if (filename.empty() && evalCode.empty()) {
        std::cout << "REPL disabled; use -i <file> or -e <code> to run a program" << std::endl;
        return 0;
    }
    size_t cellPtr = 0;
    std::string code;
    if (!evalCode.empty()) {
        code = evalCode;
    } else {
        std::string err;
        if (!readBfFileCompacted(filename, code, err)) {
            std::cerr << "ERROR: " << err;
            return 1;
        }
    }
    goof2::ProfileInfo prof;
    goof2::ProfileInfo* profPtr = profile ? &prof : nullptr;
    switch (cellWidth) {
        case 8: {
            std::vector<uint8_t> cells(tapeSize, 0);
            executeExcept<uint8_t>(cells, cellPtr, code, optimize, eof, dynamicSize, model,
                                   profPtr);
            if (dumpMemoryFlag) dumpMemory<uint8_t>(cells, cellPtr);
            break;
        }
        case 16: {
            std::vector<uint16_t> cells(tapeSize, 0);
            executeExcept<uint16_t>(cells, cellPtr, code, optimize, eof, dynamicSize, model,
                                    profPtr);
            if (dumpMemoryFlag) dumpMemory<uint16_t>(cells, cellPtr);
            break;
        }
        case 32: {
            std::vector<uint32_t> cells(tapeSize, 0);
            executeExcept<uint32_t>(cells, cellPtr, code, optimize, eof, dynamicSize, model,
                                    profPtr);
            if (dumpMemoryFlag) dumpMemory<uint32_t>(cells, cellPtr);
            break;
        }
        case 64: {
            std::vector<uint64_t> cells(tapeSize, 0);
            executeExcept<uint64_t>(cells, cellPtr, code, optimize, eof, dynamicSize, model,
                                    profPtr);
            if (dumpMemoryFlag) dumpMemory<uint64_t>(cells, cellPtr);
            break;
        }
        default:
            std::cerr << "ERROR: Unsupported cell width; use 8,16,32,64" << std::endl;
            return 1;
    }
    if (profile) {
        std::cout << "Instructions executed: " << prof.instructions << std::endl;
        std::cout << "Elapsed time: " << prof.seconds << "s" << std::endl;
    }
    return 0;
}
#endif  // GOOF2_ENABLE_REPL
