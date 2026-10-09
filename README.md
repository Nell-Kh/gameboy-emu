# gameboy-emu

[![CI](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml/badge.svg)](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml)

A Game Boy (DMG) emulator in C++20. Work in progress.

**Status:** the CPU, timer, memory map, OAM DMA and MBC1 cartridges are done. All 500
instructions are implemented, and Blargg's `cpu_instrs`, `instr_timing` and `mem_timing` and 58
Mooneye tests pass in CI. There is no screen or sound
yet, so it cannot show a game; see [docs/accuracy.md](docs/accuracy.md) for exactly what is and
is not covered.

## Build and test

Needs CMake 3.24+, Ninja, a C++20 compiler and, for one test, Python 3.

```
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The first configure downloads GoogleTest and the public test ROMs. No ROM, commercial or
otherwise, is stored in this repository.

## Run a test ROM

```
./build/asan/app/gameboy-emu --headless path/to/rom.gb
```

It prints what the ROM sends over the serial port and exits with 0 if that includes "Passed".
Add `--trace` for one line of CPU state per instruction.

## Layout

- `core/` the emulator as a library, with no dependencies
- `app/` the command-line frontend
- `tests/` unit tests and test-ROM runs
- `tools/` the opcode table and the generator that turns it into the instruction decoder
- `docs/` [design decisions](docs/DECISIONS.md) and the [accuracy report](docs/accuracy.md)
