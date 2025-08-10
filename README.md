# bfvmcpp
An all-in-one Brainfuck development tool/VM

## Building

This project uses CMake for builds. To compile:

```sh
cmake -S . -B build
cmake --build build
```

The resulting executable will be located in the `build` directory.

## License

This project is licensed under the terms of the GNU Affero General Public License v3.0 or later.
It includes third-party components under separate licenses:
- argh (BSD-3-Clause), see `LICENSE-argh`
- simde (MIT and CC0), see `LICENSE-simde`

