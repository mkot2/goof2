/*
    Goof - An optimizing brainfuck VM
    TUI REPL implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "repl.hxx"

#include <algorithm>
#include <sstream>

#include "cpp-terminal/color.hpp"
#include "cpp-terminal/cursor.hpp"
#include "cpp-terminal/event.hpp"
#include "cpp-terminal/input.hpp"
#include "cpp-terminal/key.hpp"
#include "cpp-terminal/screen.hpp"
#include "cpp-terminal/style.hpp"
#include "cpp-terminal/terminal.hpp"
#include "cpp-terminal/window.hpp"
#include "cpp-terminal/terminfo.hpp"
#include "include/vm.hxx"

namespace {
bool supportsColor() {
    using Term::Terminfo;
    return Terminfo::get(Terminfo::Bool::ControlSequences) &&
           Terminfo::getColorMode() != Terminfo::ColorMode::NoColor &&
           Terminfo::getColorMode() != Terminfo::ColorMode::Unset;
}

void highlightBf(Term::Window& scr, std::size_t x, std::size_t y, const std::string& code) {
    if (!supportsColor()) return;
    const std::size_t cols = scr.columns();
    for (std::size_t i = 0; i < code.size() && (x + i) <= cols; ++i) {
        Term::Color::Name color;
        bool token = true;
        switch (code[i]) {
            case '>':
            case '<':
                color = Term::Color::Name::Cyan;
                break;
            case '+':
            case '-':
                color = Term::Color::Name::Yellow;
                break;
            case '[':
            case ']':
                color = Term::Color::Name::Magenta;
                break;
            case '.':
            case ',':
                color = Term::Color::Name::Green;
                break;
            default:
                token = false;
        }
        if (token) {
            scr.set_fg(x + i, y, color);
        }
    }
}
void appendLines(std::vector<std::string>& log, const std::string& text) {
    std::istringstream iss(text);
    for (std::string line; std::getline(iss, line);) {
        log.push_back(line);
    }
}

std::string render(Term::Window& scr, const std::vector<std::string>& log, const std::string& input,
                   size_t cellPtr, uint8_t cellVal) {
    const std::size_t rows = scr.rows();
    const std::size_t cols = scr.columns();
    scr.clear();

    const std::size_t promptWidth = 2;
    const std::size_t wrap = cols > promptWidth ? cols - promptWidth : 1;
    const std::size_t maxInputLines = rows > 1 ? rows - 1 : 1;
    const std::size_t maxChars = wrap * maxInputLines;
    std::size_t startPos = input.size() > maxChars ? input.size() - maxChars : 0;
    std::vector<std::string> lines;
    for (std::size_t pos = startPos; pos < input.size(); pos += wrap) {
        lines.push_back(input.substr(pos, wrap));
    }
    if (lines.empty()) lines.push_back("");

    const std::size_t inputLines = lines.size();
    const std::size_t logHeight = rows > (1 + inputLines) ? rows - (1 + inputLines) : 0;
    std::size_t start = log.size() > logHeight ? log.size() - logHeight : 0;
    for (std::size_t i = 0; i < logHeight && (start + i) < log.size(); ++i) {
        const std::string& line = log[start + i];
        scr.print_str(1, 1 + i, line);
        if (line.rfind("$ ", 0) == 0) {
            highlightBf(scr, 3, 1 + i, line.substr(2));
        }
    }

    for (std::size_t i = 0; i < inputLines; ++i) {
        std::string promptLine = (i == 0 ? "$ " : "  ") + lines[i];
        std::size_t row = logHeight + 1 + i;
        scr.print_str(1, row, promptLine);
        highlightBf(scr, 3, row, lines[i]);
    }

    std::string status = "ptr: " + std::to_string(cellPtr) + " val: " + std::to_string(+cellVal);
    if (status.size() < cols)
        status += std::string(cols - status.size(), ' ');
    else
        status = status.substr(0, cols);
    scr.fill_bg(1, rows, cols, 1, Term::Color::Name::White);
    scr.fill_fg(1, rows, cols, 1, Term::Color::Name::Black);
    scr.print_str(1, rows, status);

    std::size_t cursor_col = std::min(promptWidth + lines.back().size() + 1, cols);
    std::size_t cursor_row = logHeight + lines.size();
    scr.set_cursor_pos(cursor_col, cursor_row);
    return scr.render(1, 1, true);
}
}  // namespace

void runRepl(std::vector<uint8_t>& cells, size_t& cellPtr, size_t ts, bool optimize, int eof,
             bool dynamicSize) {
    Term::terminal.setOptions(Term::Option::ClearScreen, Term::Option::NoSignalKeys,
                              Term::Option::Raw);
    Term::Screen termSize = Term::screen_size();
    Term::Window scr(termSize);
    std::vector<std::string> log;
    std::string input;
    std::vector<std::string> history;
    std::size_t historyIndex = 0;
    bool on = true;
    while (on) {
        Term::cout << render(scr, log, input, cellPtr, cells[cellPtr]) << std::flush;
        Term::Event ev = Term::read_event();
        if (ev.type() == Term::Event::Type::Screen) {
            termSize = ev;
            scr = Term::Window(termSize);
            Term::cout << render(scr, log, input, cellPtr, cells[cellPtr]) << std::flush;
            continue;
        }
        Term::Key key = ev;
        if (key == Term::Key::Ctrl_C || key == Term::Key::Esc) {
            on = false;
        } else if (key == Term::Key::Enter) {
            if (!input.empty()) {
                log.push_back("$ " + input);
            }
            if (input == "exit" || input == "quit") {
                on = false;
            } else if (input == "clear") {
                cellPtr = 0;
                cells.assign(ts, 0);
                log.clear();
            } else if (input == "dump") {
                std::ostringstream oss;
                dumpMemory(cells, cellPtr, oss);
                appendLines(log, oss.str());
            } else if (input == "help") {
                log.push_back("Commands: help, dump, clear, exit/quit");
            } else if (!input.empty()) {
                std::ostringstream oss;
                std::streambuf* oldbuf = std::cout.rdbuf(oss.rdbuf());
                executeExcept(cells, cellPtr, input, optimize, eof, dynamicSize, true);
                std::cout.rdbuf(oldbuf);
                appendLines(log, oss.str());
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
}
