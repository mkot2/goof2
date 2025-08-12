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

## JIT compilation

Optional JIT support using [sljit](https://zherczeg.github.io/sljit/) can be
enabled at build time:

```sh
cmake -S . -B build -DGooF2_USE_SLJIT=ON
cmake --build build
```

sljit supports x86 (32/64), ARM (32/64), RISC-V (32/64), s390x (64), PowerPC
(32/64), LoongArch (64), and MIPS (32/64). JIT requires executable memory and
may not be available on every platform. The regex optimization step remains
unchanged to guarantee identical behavior in JIT mode.

## Machine learning optimizer

Goof2 can load additional rewrite rules learned from example programs. Enable
them with `--ml-opt` and place the model file as documented in
[docs/ml_optimizer.md](docs/ml_optimizer.md).

## Usage

The VM can execute a Brainfuck program from a file using `-i <file>` or directly from
an inline string with `-e <code>`. When `-e` is provided any `-i` option is ignored.
Inline execution works with all other flags, e.g. optimization or tape settings.

```sh
printf 'A' | ./goof2 -e ',.'
./goof2 -e '+++' -nopt
```

The VM supports selectable cell widths. Use the `--cw` option to choose 8-, 16- or 32-bit
cells at startup:

```sh
./goof2 --cw 16 program.bf
```

Add `--profile` to measure execution time and instruction count:

```sh
./goof2 --profile program.bf
```

Select a memory allocation strategy with `-mm <contiguous|fibonacci|paged|os>`. If omitted,
the VM chooses a model heuristically. In the interactive REPL, press `F8` to cycle through
the available models.

## Memory models

The virtual machine grows its cell tape using several strategies:

- **Contiguous** doubles the allocation each time more space is needed.
- **Fibonacci** expands according to the Fibonacci sequence to balance
  memory usage against allocation frequency.
- **Paged** adds memory in fixed 64 KB pages.
- **OS-backed** reserves memory from the operating system using virtual
  memory facilities. This model is used only when such APIs are available
  and otherwise falls back to the contiguous model.

Use the `-mm` flag or the `F8` REPL shortcut to select a model explicitly.

## License

This project is licensed under the terms of the GNU Affero General Public
License v3.0 or later. When JIT support is enabled, the executable links against
[sljit](https://zherczeg.github.io/sljit/), which is released under the
Simplified BSD license. Review the compatibility of these licenses for your
intended use. It includes third-party components under separate licenses:
- cpp-terminal (MIT), included via git submodule; see `cpp-terminal/LICENSE`
- simde (MIT and CC0), included via git submodule; see `simde/COPYING`
- sljit (BSD-2-Clause), included via git submodule; see `LICENSE.sljit`

