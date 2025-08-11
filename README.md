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

## Usage

The VM supports selectable cell widths. Use the `--cw` option to choose 8-, 16- or 32-bit
cells at startup:

```sh
./goof --cw 16 program.bf
```

## License

This project is licensed under the terms of the GNU Affero General Public License v3.0 or later.
It includes third-party components under separate licenses:
- argh (BSD-3-Clause), see `LICENSE-argh`
- simde (MIT and CC0), see `LICENSE-simde`

