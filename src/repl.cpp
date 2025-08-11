/*
    Goof - An optimizing brainfuck VM
    TUI REPL implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "repl.hpp"

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
#include "include/vm.hpp"

namespace {
bool supports_color() {
    using Term::Terminfo;
    return Terminfo::get(Terminfo::Bool::ControlSequences) &&
           Terminfo::getColorMode() != Terminfo::ColorMode::NoColor &&
           Terminfo::getColorMode() != Terminfo::ColorMode::Unset;
}

void highlight_bf(Term::Window& scr, std::size_t x, std::size_t y, const std::string& code) {
    if (!supports_color()) return;
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
void append_lines(std::vector<std::string>& log, const std::string& text) {
    std::istringstream iss(text);
    for (std::string line; std::getline(iss, line);) {
        log.push_back(line);
    }
}

std::string render(Term::Window& scr, const std::vector<std::string>& log, const std::string& input,
                   size_t cellptr, uint8_t cellval, std::size_t log_height) {
    const std::size_t rows = scr.rows();
    const std::size_t cols = scr.columns();
    scr.clear();

    std::size_t start = log.size() > log_height ? log.size() - log_height : 0;
    for (std::size_t i = 0; i < log_height && (start + i) < log.size(); ++i) {
        const std::string& line = log[start + i];
        scr.print_str(1, 1 + i, line);
        if (line.rfind("$ ", 0) == 0) {
            highlight_bf(scr, 3, 1 + i, line.substr(2));
        }
    }

    std::string in = input;
    std::size_t max_input = cols > 3 ? cols - 3 : 0;
    if (in.size() > max_input) {
        in = in.substr(in.size() - max_input);
    }
    std::string prompt_plain = "$ " + in;
    scr.print_str(1, rows - 1, prompt_plain);
    highlight_bf(scr, 3, rows - 1, in);

    std::string status = "ptr: " + std::to_string(cellptr) + " val: " + std::to_string(+cellval);
    if (status.size() < cols)
        status += std::string(cols - status.size(), ' ');
    else
        status = status.substr(0, cols);
    scr.fill_bg(1, rows, cols, 1, Term::Color::Name::White);
    scr.fill_fg(1, rows, cols, 1, Term::Color::Name::Black);
    scr.print_str(1, rows, status);

    scr.set_cursor_pos(std::min(prompt_plain.size() + 1, cols), rows - 1);
    return scr.render(1, 1, true);
}
}  // namespace

void run_repl(std::vector<uint8_t>& cells, size_t& cellptr, size_t ts, bool optimize, int eof,
              bool dynamicSize) {
    Term::terminal.setOptions(Term::Option::ClearScreen, Term::Option::NoSignalKeys,
                              Term::Option::Raw);
    Term::Screen term_size = Term::screen_size();
    Term::Window scr(term_size);
    std::size_t log_height = term_size.rows() > 2 ? term_size.rows() - 2 : 0;
    std::vector<std::string> log;
    std::string input;
    std::vector<std::string> history;
    std::size_t history_index = 0;
    bool on = true;
    while (on) {
        Term::cout << render(scr, log, input, cellptr, cells[cellptr], log_height) << std::flush;
        Term::Event ev = Term::read_event();
        if (ev.type() == Term::Event::Type::Screen) {
            term_size = ev;
            scr = Term::Window(term_size);
            log_height = term_size.rows() > 2 ? term_size.rows() - 2 : 0;
            std::size_t max_input = term_size.columns() > 3 ? term_size.columns() - 3 : 0;
            if (input.size() > max_input) {
                input = input.substr(input.size() - max_input);
            }
            Term::cout << render(scr, log, input, cellptr, cells[cellptr], log_height) << std::flush;
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
                cellptr = 0;
                cells.assign(ts, 0);
                log.clear();
            } else if (input == "dump") {
                std::ostringstream oss;
                dumpMemory(cells, cellptr, oss);
                append_lines(log, oss.str());
            } else if (input == "help") {
                log.push_back("Commands: help, dump, clear, exit/quit");
            } else if (!input.empty()) {
                std::ostringstream oss;
                std::streambuf* oldbuf = std::cout.rdbuf(oss.rdbuf());
                executeExcept(cells, cellptr, input, optimize, eof, dynamicSize, true);
                std::cout.rdbuf(oldbuf);
                append_lines(log, oss.str());
            }
            input.clear();
        } else if (key == Term::Key::ArrowUp) {
            if (!history.empty() && history_index > 0) {
                --history_index;
                input = history[history_index];
            }
        } else if (key == Term::Key::ArrowDown) {
            if (history_index + 1 < history.size()) {
                ++history_index;
                input = history[history_index];
            } else if (history_index + 1 == history.size()) {
                history_index = history.size();
                input.clear();
            }
        } else if (key == Term::Key::Backspace) {
            if (!input.empty()) input.pop_back();
        } else if (key.isprint()) {
            std::size_t max_input = scr.columns() > 3 ? scr.columns() - 3 : 0;
            if (input.size() < max_input) {
                input.push_back(static_cast<char>(key.value));
            }
        }
    }
    Term::terminal.setOptions(Term::Option::Cooked, Term::Option::SignalKeys, Term::Option::Cursor);
}
