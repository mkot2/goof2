# AGENTS Instructions

## Build & Test
- Build the project with CMake:
  ```sh
  cmake -S . -B build
  cmake --build build
  ```
  The resulting executable will be located in the `build` directory.
- Run tests:
  ```sh
  ctest --test-dir build
  ```

## Coding Style
- Follow the existing C++ code style in this repository.
- Format C++ sources and headers with `clang-format` before committing.

## Licensing
- The project is licensed under the GNU Affero General Public License v3.0 or later.
- Third-party components:
  - argh (BSD-3-Clause), see `LICENSE-argh`
  - simde (MIT and CC0), see `LICENSE-simde`

## Directory Notes
- `src/` contains core source files.
- `include/` holds public headers.
- `tests/` contains unit and fuzz tests.
- `main.cxx` is the entry point for the command-line interface.
