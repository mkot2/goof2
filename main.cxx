/*
    Goof - An optimizing brainfuck VM
    Version 1.4.0

    Made by M.K.
    Co-vibed by ChatGPT 5 Thinking :3
    2023-2025
    Published under the CC0-1.0 license
*/
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "argh.hxx"
#include "rang.hxx"
#include "cpp-terminal/exception.hpp"
#include "cpp-terminal/input.hpp"
#include "cpp-terminal/iostream.hpp"
#include "cpp-terminal/key.hpp"
#include "cpp-terminal/screen.hpp"
#include "cpp-terminal/terminal.hpp"
#include "cpp-terminal/tty.hpp"
#include "cpp-terminal/window.hpp"
#include "include/vm.hpp"

using namespace rang;

void dumpMemory(const std::vector<uint8_t>& cells, size_t cellptr)
{
    size_t lastNonEmpty = 0;
    for (lastNonEmpty = cells.size() - 1; lastNonEmpty > cellptr; lastNonEmpty--)
        if (cells[lastNonEmpty])
            break;
    std::cout << "Memory dump:" << '\n'
              << style::underline << "row+col |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |" << std::endl;
    for (size_t i = 0, row = 0; i <= std::max(lastNonEmpty, cellptr); i++) {
        if (i % 10 == 0) {
            if (row) std::cout << std::endl;
            std::cout << row << std::string(8 - std::to_string(row).length(), ' ') << "|";
            row += 10;
        }
        std::cout << (i == cellptr ? fg::green : fg::reset)
                  << +cells[i] << fg::reset << std::string(3 - std::to_string(cells[i]).length(), ' ') << "|";
    }
    std::cout << style::reset << std::endl;
}

void executeExcept(std::vector<uint8_t>& cells, size_t& cellptr, std::string& code, bool optimize, int eof, bool dynamicSize, bool term = false)
{
    int ret = bfvmcpp::execute(cells, cellptr, code, optimize, eof, dynamicSize, term);
    switch (ret) {
    case 1:
        std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched close bracket";
        break;
    case 2:
        std::cout << fg::red << "ERROR:" << fg::reset << " Unmatched open bracket";
        break;
    }
}

static std::string render(Term::Window& scr, const std::size_t& rows, const std::size_t& cols, const std::size_t& menuheight, const std::size_t& menuwidth, const std::size_t& menupos)
{
  scr.clear();
  const std::size_t menux0 = ((cols - menuwidth) / 2) + 1;
  const std::size_t menuy0 = ((rows - menuheight) / 2) + 1;
  scr.print_rect(menux0, menuy0, menux0 + menuwidth + 1, menuy0 + menuheight + 1);

  for(std::size_t i = 1; i <= menuheight; ++i)
  {
    const std::string item = std::to_string(i) + ": item";
    scr.print_str(menux0 + 1, menuy0 + i, item);
    if(i == menupos)
    {
      scr.fill_fg(menux0 + 1, menuy0 + i, menux0 + item.size(), menuy0 + i, Term::Color::Name::Red);  // FG
      scr.fill_bg(menux0 + 1, menuy0 + i, menux0 + menuwidth, menuy0 + i, Term::Color::Name::Gray);   // BG
      scr.fill_style(menux0 + 1, menuy0 + i, menux0 + item.size(), menuy0 + i, Term::Style::Bold);
    }
    else
    {
      scr.fill_fg(menux0 + 1, menuy0 + i, menux0 + item.size(), menuy0 + i, Term::Color::Name::Blue);
      scr.fill_bg(menux0 + 1, menuy0 + i, menux0 + menuwidth, menuy0 + i, Term::Color::Name::Green);
    }
  }

  const std::size_t y = menuy0 + menuheight + 5;
  scr.print_str(1, y, "Selected item: " + std::to_string(menupos));
  scr.print_str(1, y + 1, "Menu width: " + std::to_string(menuwidth));
  scr.print_str(1, y + 2, "Menu height: " + std::to_string(menuheight));
  scr.print_str(1, y + 3, "Unicode test: Ondřej Čertík, ἐξήκοι");

  return scr.render(1, 1, false);
}

int main(int argc, char* argv[])
{
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    std::string filename; cmdl("i", "") >> filename;
    const bool optimize = !cmdl["nopt"];
    const bool sdumpMemory = cmdl["dm"];
    const bool help = cmdl["h"];
    const bool dynamicSize = cmdl["dts"];
    int eof = 0; cmdl("eof", 0) >> eof;
    int ts = 0; cmdl("ts", 30000) >> ts;

    size_t cellptr = 0;
    std::vector<uint8_t> cells;
    cells.assign(ts, 0);
    Term::terminal.setOptions(Term::Option::ClearScreen, Term::Option::NoSignalKeys, Term::Option::NoCursor, Term::Option::Raw);
    Term::Screen term_size = Term::screen_size();
    std::size_t  pos{5};
    std::size_t  h{10};
    std::size_t  w{10};
    bool         on = true;
    Term::Window scr(term_size);
    bool         need_to_render{true};
    while(on)
    {
      if(need_to_render)
      {
        Term::cout << ::render(scr, term_size.rows(), term_size.columns(), h, w, pos) << std::flush;
        need_to_render = false;
      }
      Term::Key key = Term::read_event();
      switch(key)
      {
        case Term::Key::ArrowLeft:
          if(w > 10) { --w; }
          need_to_render = true;
          break;
        case Term::Key::ArrowRight:
          if(w < (term_size.columns() - 5)) { ++w; }
          need_to_render = true;
          break;
        case Term::Key::ArrowUp:
          if(pos > 1) { --pos; }
          need_to_render = true;
          break;
        case Term::Key::ArrowDown:
          if(pos < h) { ++pos; }
          need_to_render = true;
          break;
        case Term::Key::Home:
          pos            = 1;
          need_to_render = true;
          break;
        case Term::Key::End:
          pos            = h;
          need_to_render = true;
          break;
        case Term::Key::q:
        case Term::Key::Esc:
        case Term::Key::Ctrl_C: on = false; break;
        default: break;
      }
    }
    if (help) {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    } else if (!filename.empty()) {
        std::ifstream in(filename);
        if (std::string code; in.good()) {
            code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            executeExcept(cells, cellptr, code, optimize, eof, dynamicSize);
            if (sdumpMemory) dumpMemory(cells, cellptr);
        } else {
            std::cout << fg::red << "ERROR:" << fg::reset << " File could not be read";
        }
    } else {
        std::cout << R"(   _____  ____   ____  ______ )" << '\n'
                  << R"(  / ____|/ __ \ / __ \|  ____|)" << '\n'
                  << R"( | |  __| |  | | |  | | |__   )" << '\n'
                  << R"( | | |_ | |  | | |  | |  __|  )" << '\n'
                  << R"( | |__| | |__| | |__| | |     )" << '\n'
                  << R"(  \_____|\____/ \____/|_|     )" << '\n'
                  << "Goof v1.4.0 - an optimizing brainfuck VM" << '\n'
                  << "Type " << fg::cyan << "help" << fg::reset << " to see available commands."
                  << std::endl;

        while (true) {
            std::cout << "$ ";
            std::string repl;
            std::cin >> repl;
            std::getchar();

            //TODO: Add a visualiser?
            if (repl.starts_with("help")) {
                std::cout << style::underline << "General commands:" << style::reset << '\n'
                    << fg::cyan << "help" << fg::reset << " - Displays this list" << '\n'
                    << fg::cyan << "exit" << fg::reset << "/" << fg::cyan << "quit" << fg::reset << " - Exits Goof" << '\n'
                    << style::underline << "Memory commands:" << style::reset << '\n'
                    << fg::cyan << "clear" << fg::reset << " - Clears memory cells" << '\n'
                    << fg::cyan << "dump" << fg::reset << " - Displays values of memory cells, cell highlighted in " << fg::green << "green" << fg::reset << " is the cell currently pointed to" << '\n';
            } else if (repl.starts_with("clear")) {
                cellptr = 0;
                cells.assign(ts, 0);
            } else if (repl.starts_with("dump")) {
                dumpMemory(cells, cellptr);
            } else if (repl.starts_with("exit") || repl.starts_with("quit")) {
                return 0;
            } else {
                executeExcept(cells, cellptr, repl, optimize, eof, dynamicSize, true);
            }
        }
    }
}
