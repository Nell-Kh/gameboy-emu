# gameboy-emu

[![CI](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml/badge.svg)](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml)

A Game Boy (DMG) emulator in C++20. Work in progress.

**Status:** CPU, timing, memory and MBC1 complete (`v0.2.0`). M3, the PPU, is in progress: it draws
the background, window and objects, and runs ROMs in an SDL window with keyboard input. Blargg's
`cpu_instrs`, `instr_timing` and `mem_timing`, 71 Mooneye tests and dmg-acid2 (pixel-identical) pass
in CI. There is no sound yet, and only cartridges with no mapper or MBC1 load; see
[docs/accuracy.md](docs/accuracy.md) for exactly what is and is not covered.

## Build and test

Needs CMake 3.24+, Ninja, a C++20 compiler and SDL2 (`brew install sdl2` on macOS,
`sudo apt install libsdl2-dev` on Ubuntu). SDL2 is used by the window in `app/` only; the core
does not depend on it. Python 3 is used by one test,
`Generator.OpcodesAreUpToDate`, which checks that `core/opcodes.gen.cpp` matches what
`tools/gen_opcodes.py` generates. Without Python that one test is left out of the run.

```
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The first configure downloads GoogleTest and the public test ROMs. No ROM, commercial or
otherwise, is stored in this repository. The tests read PNG screenshots with
[stb_image](https://github.com/nothings/stb), kept in `tests/third_party/` (ADR-015).

## Play a game

```
./build/release/app/gameboy-emu path/to/game.gb
```

| Game Boy | Keyboard |
|---|---|
| D-pad | arrow keys |
| A | X |
| B | Z |
| Start | Enter |
| Select | Backspace |

Escape closes the window. `--scale N` sets the window size to N times 160x144 (default 4); the
window can also be resized. Bring your own ROM: none is included.

## Run a test ROM

```
./build/asan/app/gameboy-emu --headless path/to/rom.gb
```

It prints what the ROM sends over the serial port and exits with 0 if that includes "Passed".
Add `--trace` for one line of CPU state per instruction.

## Layout

- `core/` the emulator as a library, with no dependencies
- `app/` the frontend: the SDL2 window and the headless test-ROM runner
- `tests/` unit tests and test-ROM runs
- `tools/` the opcode table and the generator that turns it into the instruction decoder
- `docs/` [design decisions](docs/DECISIONS.md) and the [accuracy report](docs/accuracy.md)
