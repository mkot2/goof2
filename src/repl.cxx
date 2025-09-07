/*
    Goof2 - An optimizing brainfuck VM
    TUI REPL implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifdef GOOF2_ENABLE_REPL
#include "repl.hxx"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "cpp-terminal/color.hpp"
#include "cpp-terminal/cursor.hpp"
#include "cpp-terminal/event.hpp"
#include "cpp-terminal/input.hpp"
#include "cpp-terminal/key.hpp"
#include "cpp-terminal/screen.hpp"
#include "cpp-terminal/style.hpp"
#include "cpp-terminal/terminal.hpp"
#include "cpp-terminal/terminfo.hpp"
#include "cpp-terminal/window.hpp"
#include "vm.hxx"

namespace {
std::vector<std::string> backBuffer;
bool supportsColor() {
    using Term::Terminfo;
    return Terminfo::get(Terminfo::Bool::ControlSequences) &&
           Terminfo::getColorMode() != Terminfo::ColorMode::NoColor &&
           Terminfo::getColorMode() != Terminfo::ColorMode::Unset;
}

constexpr auto bfColors = [] {
    std::array<Term::Color::Name, 256> table{};
    table.fill(Term::Color::Name::Default);
    table['>'] = table['<'] = Term::Color::Name::Cyan;
    table['+'] = table['-'] = Term::Color::Name::Yellow;
    table['['] = table[']'] = Term::Color::Name::Magenta;
    table['.'] = table[','] = Term::Color::Name::Green;
    return table;
}();

void highlightBf(Term::Window& scr, std::size_t x, std::size_t y, std::string_view code,
                 bool hasColor) {
    if (!hasColor) return;
    const std::size_t cols = scr.columns();
    for (std::size_t i = 0; i < code.size() && (x + i) <= cols; ++i) {
        auto color = bfColors[static_cast<unsigned char>(code[i])];
        if (color != Term::Color::Name::Default) {
            scr.set_fg(x + i, y, color);
        }
    }
}
// Keep only the most recent entries to bound memory usage. Older lines are
// discarded once `maxLogLines` is exceeded.
constexpr std::size_t maxLogLines = 1024;
void appendLines(std::vector<std::string>& log, const std::string& text) {
    std::string_view view{text};
    const std::size_t estimatedLines = std::ranges::count(view, '\n') + 1;
    if (log.size() + estimatedLines > maxLogLines) {
        std::size_t excess = log.size() + estimatedLines - maxLogLines;
        log.erase(log.begin(), log.begin() + excess);
    }
    // Reserve to minimize reallocations; trades peak memory for speed.
    log.reserve(log.size() + estimatedLines);
    std::size_t start = 0;
    while (start < view.size()) {
        std::size_t end = view.find('\n', start);
        if (end == std::string_view::npos) {
            log.emplace_back(view.substr(start));
            break;
        }
        log.emplace_back(view.substr(start, end - start));
        start = end + 1;
    }
}

void appendInputLines(std::vector<std::string>& log, const std::string& input) {
    std::string_view view{input};
    const std::size_t estimatedLines = std::ranges::count(view, '\n') + 1;
    if (log.size() + estimatedLines > maxLogLines) {
        std::size_t excess = log.size() + estimatedLines - maxLogLines;
        log.erase(log.begin(), log.begin() + excess);
    }
    // Reserve to reduce reallocations; consumes extra memory up front.
    log.reserve(log.size() + estimatedLines);
    std::size_t start = 0;
    bool first = true;
    while (start < view.size()) {
        std::size_t end = view.find('\n', start);
        if (end == std::string_view::npos) end = view.size();
        log.emplace_back(first ? "$ " : "  ");
        log.back().append(view.substr(start, end - start));
        first = false;
        start = end + 1;
    }
    if (first) {
        log.emplace_back("$ ");
    }
}

constexpr std::array<const char*, 5> table{"auto", "contig", "fib", "paged", "os"};

const char* modelName(goof2::MemoryModel m) { return table[static_cast<size_t>(m)]; }

enum class MenuState { None, TapeSize, EOFVal, CellWidth, Search };

enum class Tab { Log, Memory };

template <typename CellT>
std::string render(Term::Window& scr, const std::vector<std::string>& log,
                   const std::vector<std::string>& dump, const std::vector<CellT>& cells,
                   const std::vector<size_t>& changed, const std::vector<size_t>& matches,
                   const std::string& input, size_t cellPtr, CellT cellVal, const ReplConfig& cfg,
                   MenuState menuState, Tab tab, const std::string& menuInput, bool hasColor) {
    const std::size_t rows = scr.rows();
    const std::size_t cols = scr.columns();

    if (backBuffer.size() != rows || (rows && backBuffer[0].size() != cols)) {
        backBuffer.assign(rows, std::string(cols, ' '));
    }

    auto pad = [cols](const std::string& txt) {
        if (txt.size() < cols) return txt + std::string(cols - txt.size(), ' ');
        return txt.substr(0, cols);
    };

    const std::size_t promptWidth = 2;
    const std::size_t wrap = cols > promptWidth ? cols - promptWidth : 1;
    const std::size_t maxInputLines = rows > 4 ? rows - 4 : 1;
    const std::size_t maxChars = wrap * maxInputLines;
    std::size_t startPos = input.size() > maxChars ? input.size() - maxChars : 0;
    std::vector<std::string_view> lines;
    std::string_view inputView(input);
    for (std::size_t pos = startPos; pos < input.size(); pos += wrap) {
        lines.push_back(inputView.substr(pos, wrap));
    }
    if (lines.empty()) lines.push_back(std::string_view{});

    const std::size_t inputLines = lines.size();
    const std::size_t logHeight = rows > (4 + inputLines) ? rows - (4 + inputLines) : 0;
    const std::vector<std::string>& view = (tab == Tab::Log ? log : dump);
    std::size_t start = view.size() > logHeight ? view.size() - logHeight : 0;
    for (std::size_t i = 0; i < logHeight && (start + i) < view.size(); ++i) {
        const std::string& line = view[start + i];
        std::string padded = pad(line);
        if (backBuffer[i] != padded) {
            scr.print_str(1, 1 + i, padded);
            if (tab == Tab::Log && (line.rfind("$ ", 0) == 0 || line.rfind("  ", 0) == 0)) {
                highlightBf(scr, 3, 1 + i, std::string_view(line).substr(2), hasColor);
            }
            backBuffer[i] = std::move(padded);
        }
    }

    if (tab == Tab::Memory && hasColor) {
        const std::size_t yOffset = 3;
        const std::size_t xOffset = 10;
        const std::size_t cellStep = 4;
        if (cfg.highlightChanges) {
            for (size_t idx : changed) {
                std::size_t row = idx / 10;
                std::size_t col = idx % 10;
                std::string val = std::to_string(cells[idx]);
                scr.fill_bg(xOffset + col * cellStep, yOffset + row, val.size(), 1,
                            Term::Color::Name::Yellow);
            }
        }
        if (cfg.searchActive) {
            for (size_t idx : matches) {
                std::size_t row = idx / 10;
                std::size_t col = idx % 10;
                std::string val = std::to_string(cells[idx]);
                scr.fill_bg(xOffset + col * cellStep, yOffset + row, val.size(), 1,
                            Term::Color::Name::Magenta);
            }
        }
    }

    {
        std::string sep = pad(std::string(cols, '-'));
        if (backBuffer[logHeight] != sep) {
            scr.print_str(1, logHeight + 1, sep);
            backBuffer[logHeight] = std::move(sep);
        }
    }

    for (std::size_t i = 0; i < inputLines; ++i) {
        std::size_t row = logHeight + 2 + i;
        std::string line = (i == 0 ? "$ " : "  ") + std::string(lines[i]);
        std::string padded = pad(line);
        if (backBuffer[row - 1] != padded) {
            scr.print_str(1, row, padded);
            highlightBf(scr, 3, row, lines[i], hasColor);
            backBuffer[row - 1] = std::move(padded);
        }
    }

    {
        std::string sep = pad(std::string(cols, '-'));
        std::size_t row = logHeight + 2 + inputLines;
        if (backBuffer[row - 1] != sep) {
            scr.print_str(1, row, sep);
            backBuffer[row - 1] = std::move(sep);
        }
    }

    std::string menu =
        "[F1]opt:" + std::string(cfg.optimize ? "on" : "off") +
        " [F2]dyn:" + std::string(cfg.dynamicSize ? "on" : "off") + " [F3]ts:" +
        (menuState == MenuState::TapeSize ? ">" + menuInput : std::to_string(cfg.tapeSize)) +
        " [F4]eof:" + (menuState == MenuState::EOFVal ? ">" + menuInput : std::to_string(cfg.eof)) +
        " [F5]cw:" +
        (menuState == MenuState::CellWidth ? ">" + menuInput : std::to_string(cfg.cellWidth)) +
        " [F6]mm:" + modelName(cfg.model) + " [F7]" + (tab == Tab::Log ? "log*" : "log") + " [F8]" +
        (tab == Tab::Memory ? "mem*" : "mem") + " [F9]chg" + " [F10]find:" +
        (menuState == MenuState::Search
             ? ">" + menuInput
             : (cfg.searchActive ? std::to_string(cfg.searchValue) : ""));
    if (menu.size() < cols)
        menu += std::string(cols - menu.size(), ' ');
    else
        menu = menu.substr(0, cols);
    {
        std::string paddedMenu = pad(menu);
        if (backBuffer[rows - 2] != paddedMenu) {
            scr.print_str(1, rows - 1, paddedMenu);
            backBuffer[rows - 2] = std::move(paddedMenu);
        }
    }

    std::string status = "ptr: " + std::to_string(cellPtr) + " val: " + std::to_string(+cellVal);
    if (status.size() < cols)
        status += std::string(cols - status.size(), ' ');
    else
        status = status.substr(0, cols);
    std::string paddedStatus = pad(status);
    if (backBuffer[rows - 1] != paddedStatus) {
        scr.fill_bg(1, rows, cols, 1, Term::Color::Name::White);
        scr.fill_fg(1, rows, cols, 1, Term::Color::Name::Black);
        scr.print_str(1, rows, paddedStatus);
        backBuffer[rows - 1] = std::move(paddedStatus);
    }

    std::size_t cursor_col = std::min(promptWidth + lines.back().size() + 1, cols);
    std::size_t cursor_row = logHeight + 1 + lines.size();
    scr.set_cursor_pos(cursor_col, cursor_row);
    return scr.render(1, 1, true);
}
}  // namespace

template <typename CellT>
int runRepl(std::vector<CellT>& cells, size_t& cellPtr, ReplConfig& cfg) {
    Term::terminal.setOptions(Term::Option::ClearScreen, Term::Option::NoSignalKeys,
                              Term::Option::Raw);
    Term::Screen termSize = Term::screen_size();
    Term::Window scr(termSize);
    bool hasColor = supportsColor();
    std::vector<std::string> log;
    std::vector<std::string> dump;
    std::string input;
    std::vector<std::string> history;
    std::size_t historyIndex = 0;
    MenuState menuState = MenuState::None;
    Tab tab = Tab::Log;
    std::string menuInput;
    int newCw = 0;
    bool on = true;
    std::vector<size_t> changed;
    std::vector<size_t> searchMatches;
    std::vector<CellT> prevCells = cells;
    std::size_t changeNav = 0;
    std::size_t searchNav = 0;
    auto resetContext = [&]() {
        cellPtr = 0;
        std::vector<CellT> newCells(cfg.tapeSize, 0);
        cells.swap(newCells);
        log.clear();
        dump.clear();
        input.clear();
        history.clear();
        historyIndex = 0;
        prevCells = cells;
        changed.clear();
        searchMatches.clear();
        changeNav = searchNav = 0;
    };
    auto refreshDump = [&]() {
        std::ostringstream oss;
        dumpMemory<CellT>(cells, cellPtr, oss);
        std::string dumpStr = oss.str();
        std::string_view view{dumpStr};
        dump.clear();
        std::size_t start = 0;
        while (start < view.size()) {
            std::size_t end = view.find('\n', start);
            if (end == std::string_view::npos) {
                dump.emplace_back(view.substr(start));
                break;
            }
            dump.emplace_back(view.substr(start, end - start));
            start = end + 1;
        }
    };
    auto markChanges = [&]() {
        changed.clear();
        for (std::size_t i = 0; i < cells.size() && i < prevCells.size(); ++i) {
            if (cells[i] != prevCells[i]) changed.push_back(i);
        }
        if (cells.size() > prevCells.size()) {
            for (std::size_t i = prevCells.size(); i < cells.size(); ++i)
                if (cells[i]) changed.push_back(i);
        }
        prevCells = cells;
        changeNav = 0;
    };
    auto updateSearch = [&]() {
        searchMatches.clear();
        if (!cfg.searchActive) return;
        for (std::size_t i = 0; i < cells.size(); ++i)
            if (static_cast<uint64_t>(cells[i]) == cfg.searchValue) searchMatches.push_back(i);
        searchNav = 0;
    };
    updateSearch();
    while (on) {
        if (tab == Tab::Memory) refreshDump();
        Term::cout << render<CellT>(scr, log, dump, cells, changed, searchMatches, input, cellPtr,
                                    cells[cellPtr], cfg, menuState, tab, menuInput, hasColor)
                   << std::flush;
        Term::Event ev = Term::read_event();
        if (ev.type() == Term::Event::Type::Screen) {
            termSize = ev;
            scr = Term::Window(termSize);
            if (tab == Tab::Memory) refreshDump();
            Term::cout << render<CellT>(scr, log, dump, cells, changed, searchMatches, input,
                                        cellPtr, cells[cellPtr], cfg, menuState, tab, menuInput,
                                        hasColor)
                       << std::flush;
            continue;
        }
        Term::Key key = ev;
        if (menuState != MenuState::None) {
            if (key == Term::Key::Esc) {
                menuState = MenuState::None;
                menuInput.clear();
            } else if (key == Term::Key::Enter) {
                try {
                    if (menuState == MenuState::TapeSize) {
                        std::size_t val = std::stoull(menuInput);
                        if (val > 0) {
                            std::size_t bytes = val * sizeof(CellT);
                            if (bytes > GOOF2_TAPE_WARN_BYTES) {
                                log.push_back("WARNING: tape alloc ~" +
                                              std::to_string(bytes >> 20) +
                                              " MiB may exhaust memory");
                            }
                            cfg.tapeSize = val;
                            resetContext();
                        }
                    } else if (menuState == MenuState::EOFVal) {
                        int val = std::stoi(menuInput);
                        cfg.eof = val;
                        resetContext();
                    } else if (menuState == MenuState::CellWidth) {
                        int val = std::stoi(menuInput);
                        if (val == 8 || val == 16 || val == 32 || val == 64) {
                            cfg.cellWidth = val;
                            on = false;
                            newCw = val;
                        }
                    } else if (menuState == MenuState::Search) {
                        if (menuInput.empty()) {
                            cfg.searchActive = false;
                            searchMatches.clear();
                        } else {
                            cfg.searchValue = std::stoull(menuInput);
                            cfg.searchActive = true;
                            updateSearch();
                        }
                    }
                } catch (...) {
                }
                menuState = MenuState::None;
                menuInput.clear();
            } else if (key == Term::Key::Backspace) {
                if (!menuInput.empty()) menuInput.pop_back();
            } else if (key.isprint() && std::isdigit(static_cast<unsigned char>(key.value))) {
                menuInput.push_back(static_cast<char>(key.value));
            }
            continue;
        }

        if (key == Term::Key::Ctrl_C || key == Term::Key::Esc) {
            on = false;
        } else if (key == Term::Key::F1) {
            cfg.optimize = !cfg.optimize;
            resetContext();
            updateSearch();
        } else if (key == Term::Key::F2) {
            cfg.dynamicSize = !cfg.dynamicSize;
            resetContext();
            updateSearch();
        } else if (key == Term::Key::F3) {
            menuState = MenuState::TapeSize;
            menuInput.clear();
        } else if (key == Term::Key::F4) {
            menuState = MenuState::EOFVal;
            menuInput.clear();
        } else if (key == Term::Key::F5) {
            menuState = MenuState::CellWidth;
            menuInput.clear();
        } else if (key == Term::Key::F6) {
            using goof2::MemoryModel;
            switch (cfg.model) {
                case MemoryModel::Auto:
                    cfg.model = MemoryModel::Contiguous;
                    break;
                case MemoryModel::Contiguous:
                    cfg.model = MemoryModel::Fibonacci;
                    break;
                case MemoryModel::Fibonacci:
                    cfg.model = MemoryModel::Paged;
                    break;
                case MemoryModel::Paged:
#if GOOF2_HAS_OS_VM
                    cfg.model = MemoryModel::OSBacked;
#else
                    cfg.model = MemoryModel::Auto;
#endif
                    break;
                case MemoryModel::OSBacked:
                    cfg.model = MemoryModel::Auto;
                    break;
            }
        } else if (key == Term::Key::F7) {
            tab = Tab::Log;
        } else if (key == Term::Key::F8) {
            tab = Tab::Memory;
        } else if (key == Term::Key::F9) {
            if (!changed.empty()) {
                cellPtr = changed[changeNav % changed.size()];
                changeNav = (changeNav + 1) % changed.size();
            }
        } else if (key == Term::Key::F10) {
            if (cfg.searchActive && menuState != MenuState::Search && !searchMatches.empty()) {
                cellPtr = searchMatches[searchNav % searchMatches.size()];
                searchNav = (searchNav + 1) % searchMatches.size();
            } else {
                menuState = MenuState::Search;
                menuInput.clear();
            }
        } else if (key == Term::Key::Enter) {
            if (!input.empty()) {
                appendInputLines(log, input);
                history.push_back(input);
                historyIndex = history.size();
            }
            if (input == "exit" || input == "quit") {
                on = false;
            } else if (input == "clear") {
                resetContext();
                updateSearch();
            } else if (input.rfind("load ", 0) == 0) {
                std::string path = input.substr(5);
                path.erase(0, path.find_first_not_of(" \t"));
                if (path.empty()) {
                    log.push_back("ERROR: missing file path");
                } else {
                    std::ifstream file(path);
                    if (!file) {
                        log.push_back("ERROR: failed to open file: " + path);
                    } else {
                        std::ostringstream buf;
                        buf << file.rdbuf();
                        std::string code = buf.str();
                        std::ostringstream oss;
                        std::streambuf* oldbuf = std::cout.rdbuf(oss.rdbuf());
                        executeExcept<CellT>(cells, cellPtr, code, cfg.optimize, cfg.eof,
                                             cfg.dynamicSize, cfg.model, nullptr, true);

                        std::cout.rdbuf(oldbuf);
                        appendLines(log, oss.str());
                        markChanges();
                        updateSearch();
                    }
                }
            } else if (input == "help") {
                log.push_back("Commands: help, load <file>, clear, exit/quit");
            } else if (!input.empty()) {
                std::ostringstream oss;
                std::streambuf* oldbuf = std::cout.rdbuf(oss.rdbuf());
                executeExcept<CellT>(cells, cellPtr, input, cfg.optimize, cfg.eof, cfg.dynamicSize,
                                     cfg.model, nullptr, true);
                std::cout.rdbuf(oldbuf);
                appendLines(log, oss.str());
                markChanges();
                updateSearch();
            }
            input.clear();
        } else if (key == Term::Key::ArrowUp) {
            if (!history.empty() && historyIndex > 0) {
                --historyIndex;
                input = history[historyIndex];
            }
        } else if (key == Term::Key::ArrowDown) {
            if (historyIndex + 1 < history.size()) {
                ++historyIndex;
                input = history[historyIndex];
            } else if (historyIndex + 1 == history.size()) {
                historyIndex = history.size();
                input.clear();
            }
        } else if (key == Term::Key::Backspace) {
            if (!input.empty()) input.pop_back();
        } else if (key.isprint()) {
            input.push_back(static_cast<char>(key.value));
        }
    }
    Term::terminal.setOptions(Term::Option::Cooked, Term::Option::SignalKeys, Term::Option::Cursor);
    return newCw;
}

template int runRepl<uint8_t>(std::vector<uint8_t>&, size_t&, ReplConfig&);
template int runRepl<uint16_t>(std::vector<uint16_t>&, size_t&, ReplConfig&);
template int runRepl<uint32_t>(std::vector<uint32_t>&, size_t&, ReplConfig&);
template int runRepl<uint64_t>(std::vector<uint64_t>&, size_t&, ReplConfig&);
#endif  // GOOF2_ENABLE_REPL
