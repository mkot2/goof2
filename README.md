# goof2
![Lines of Code](https://tokei.rs/b1/github/mkot2/goof2?category=code)

An all-in-one Brainfuck development tool/VM

## Building

This project uses CMake for builds. To compile:

```sh
cmake -S . -B build
cmake --build build
```

The resulting executable will be located in the `build` directory.

### Windows

The project can also be built on Windows using CMake with either the
MSVC or MinGW toolchains:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

ANSI escape sequences are enabled automatically on startup so colored
output works in both PowerShell and `cmd.exe`.

## Code coverage

Builds can optionally collect code coverage information using `gcov` and
`lcov`. First ensure `lcov` is installed on the system (for example,
`apt-get install lcov` on Debian-based systems) and then configure the
project with coverage enabled:

```sh
cmake -S . -B build -DBUILD_COVERAGE=ON
cmake --build build --target coverage
```

An HTML report will be generated in `build/coverage`. Open
`build/coverage/index.html` in a browser to explore the coverage data.

## Usage

The VM supports selectable cell widths. Use the `--cw` option to choose 8-, 16- or 32-bit
cells at startup:

```sh
./goof2 --cw 16 program.bf
```

## Memory models

The virtual machine grows its cell tape using several strategies:

- **Contiguous** doubles the allocation each time more space is needed.
- **Fibonacci** expands according to the Fibonacci sequence to balance
  memory usage against allocation frequency.
- **Paged** adds memory in fixed 64 KB pages.
- **OS-backed** reserves memory from the operating system using virtual
  memory facilities. This model is used only when such APIs are available
  and otherwise falls back to the contiguous model.

## License

This project is licensed under the terms of the GNU Affero General Public License v3.0 or later.
It includes third-party components under separate licenses:
- argh (BSD-3-Clause), included via git submodule; see `argh/LICENSE`
- cpp-terminal (MIT), included via git submodule; see `cpp-terminal/LICENSE`
- simde (MIT and CC0), included via git submodule; see `simde/COPYING`

