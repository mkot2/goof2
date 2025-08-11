/*
    Goof - An optimizing brainfuck VM
    TUI REPL implementation
    Published under the GNU AGPL-3.0-or-later license
*/
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "repl.hpp"

#include <sstream>

#include "cpp-terminal/color.hpp"
#include "cpp-terminal/cursor.hpp"
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

std::string highlight_bf(const std::string& code) {
    if (!supports_color()) return code;
    std::string result;
    result.reserve(code.size() * 8);
    for (char c : code) {
        Term::Color::Name color;
        bool token = true;
        switch (c) {
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
            result += Term::color_fg(color);
            result.push_back(c);
            result += Term::color_fg(Term::Color::Name::Default);
        } else {
            result.push_back(c);
        }
    }
    return result;
}
void append_lines(std::vector<std::string>& log, const std::string& text) {
    std::istringstream iss(text);
    for (std::string line; std::getline(iss, line);) {
        log.push_back(line);
    }
}

std::string render(Term::Window& scr, const std::vector<std::string>& log, const std::string& input,
                   size_t cellptr, uint8_t cellval) {
    const std::size_t rows = scr.rows();
    const std::size_t cols = scr.columns();
    scr.clear();

    std::size_t log_height = rows > 2 ? rows - 2 : 0;
    std::size_t start = log.size() > log_height ? log.size() - log_height : 0;
    for (std::size_t i = 0; i < log_height && (start + i) < log.size(); ++i) {
        scr.print_str(1, 1 + i, log[start + i]);
    }

    std::string in = input;
    std::size_t max_input = cols > 2 ? cols - 2 : 0;
    if (in.size() > max_input) {
        in = in.substr(in.size() - max_input);
    }
    std::string prompt_plain = "$ " + in;
    std::string prompt = "$ " + highlight_bf(in);
    scr.print_str(1, rows - 1, prompt);

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
    std::vector<std::string> log;
    std::string input;
    bool on = true;
    while (on) {
        Term::cout << render(scr, log, input, cellptr, cells[cellptr]) << std::flush;
        Term::Key key = Term::read_event();
        if (key == Term::Key::Ctrl_C || key == Term::Key::Esc) {
            on = false;
        } else if (key == Term::Key::Enter) {
            if (!input.empty()) {
                log.push_back("$ " + highlight_bf(input));
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
        } else if (key == Term::Key::Backspace) {
            if (!input.empty()) input.pop_back();
        } else if (key.isprint()) {
            input.push_back(static_cast<char>(key.value));
        }
    }
    Term::terminal.setOptions(Term::Option::Cooked, Term::Option::SignalKeys, Term::Option::Cursor);
}
