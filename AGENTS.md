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
- Follow the existing C++ code style in this repository. Some of the rules are described here.
- The project uses C++23.
- Use camelCase for identifiers, including variables, functions, and files.
- Format C++ sources and headers with `clang-format` before committing.

## Licensing
- The project is licensed under the GNU Affero General Public License v3.0 or later.
- Third-party components (included via git submodules):
  - linenoise-ng (BSD-2-Clause), see `linenoise-ng/LICENSE`
  - simde (MIT and CC0), see `simde/COPYING`

## Directory Structure
- `src/`: core implementation files; organize subdirectories by feature.
- `include/`: public headers mirroring the layout of `src/`.
- `tests/`: unit and fuzz tests following the same structure as `src/`.
- `docs/`: project documentation.
- `linenoise-ng/`, `simde/`, `xxhash/`: third-party submodules; avoid modifying them directly.
- `main.cxx`: entry point for the command-line interface.
- Generated artifacts should reside in the `build/` directory, which is ignored by version control.
