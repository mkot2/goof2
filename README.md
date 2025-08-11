# bfvmcpp
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
./goof --cw 16 program.bf
```

### Memory growth strategies

When dynamic tape resizing is enabled, the VM heuristically picks one of several
memory growth strategies:

- **Contiguous doubling** – the tape doubles in size whenever more space is
  required, minimizing unused cells for small programs.
- **Fibonacci growth** – for medium tapes, sizes follow the Fibonacci sequence
  to reduce the number of reallocations at the cost of some extra memory.
- **Paged allocation** – very large tapes are extended in fixed 64KB pages to
  avoid expensive large reallocations.
- **OS-backed virtual memory** – when supported by the host OS, extremely large
  tapes reserve address space via `mmap`/`VirtualAlloc` so physical memory is
  committed only as cells are touched.

The heuristics choose contiguous doubling up to 2^16 cells, Fibonacci growth
up to 2^24 cells, paged allocation up to 2^28 cells, and OS-backed virtual
memory beyond that when available. See `src/vm.cxx` for implementation
details.

## License

This project is licensed under the terms of the GNU Affero General Public License v3.0 or later.
It includes third-party components under separate licenses:
- argh (BSD-3-Clause), see `LICENSE-argh`
- simde (MIT and CC0), see `LICENSE-simde`

