# goof2
![Lines of Code](https://tokei.rs/b1/github/mkot2/goof2?category=code)

An all-in-one Brainfuck development tool/VM

## Building

This project uses CMake for builds and requires a compiler that provides
GCC/Clang-style builtins. It is tested with GCC, Clang, and clang-cl. To
compile:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The resulting executable will be located in the `build` directory.

### Windows

The project can also be built on Windows using CMake with either the
MinGW or Clang toolchains (for example, clang-cl). Pure MSVC builds are
not supported:

```powershell
cmake -S . -B build -G Ninja
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
cmake -S . -B build -G Ninja -DBUILD_COVERAGE=ON
cmake --build build --target coverage
```

An HTML report will be generated in `build/coverage`. Open
`build/coverage/index.html` in a browser to explore the coverage data.

## Memory checking (Valgrind)

On platforms where Valgrind is available (e.g., Linux), you can run the
test suite under Valgrind to detect memory leaks and invalid memory use:

```sh
sudo apt-get install -y valgrind   # Debian/Ubuntu
cmake -S . -B build -G Ninja
cmake --build build
cmake --build build --target memcheck
```

This invokes `ctest -T memcheck` with strict options
(`--leak-check=full`, `--show-leak-kinds=all`, `--error-exitcode=2`, etc.).
The memcheck target is only available when Valgrind is found.

## Usage

The VM can execute a Brainfuck program from a file using `-i <file>` or directly from
an inline string with `-e <code>`. When `-e` is provided any `-i` option is ignored.
Inline execution works with all other flags, e.g. optimization or tape settings.

```sh
printf 'A' | ./goof2 -e ',.'
./goof2 -e '+++' -nopt
```

The VM supports selectable cell widths. Use the `--cw` option to choose 8-, 16-, 32- or 64-bit
cells at startup:

```sh
./goof2 --cw 64 program.bf
```

Add `--profile` to measure execution time and instruction count:

```sh
./goof2 --profile program.bf
```

Select a memory allocation strategy with `-mm <contiguous|fibonacci|paged|os>`. If omitted,
the VM chooses a model heuristically.

## Instruction cache

Compiled programs are cached in memory to speed up repeated executions. The cache reserves
space for roughly 64 entries up front and evicts the least recently used entry when the
limit is exceeded.

## Memory models

The virtual machine grows its cell tape using several strategies:

- **Contiguous** doubles the allocation each time more space is needed.
- **Fibonacci** expands according to the Fibonacci sequence to balance
  memory usage against allocation frequency.
- **Paged** adds memory in fixed 64 KB pages.
- **OS-backed** reserves memory from the operating system using virtual
  memory facilities. This model is used only when such APIs are available
  and otherwise falls back to the contiguous model.

Use the `-mm` flag to select a model explicitly.

## License

This project is licensed under the terms of the GNU Affero General Public
License v3.0 or later. It includes third-party components under separate licenses:
- linenoise-ng (BSD-2-Clause), included via git submodule; see `linenoise-ng/LICENSE`
- simde (MIT and CC0), included via git submodule; see `simde/COPYING`
 - xxhash (BSD-2-Clause), included via git submodule; see `xxhash/LICENSE`
