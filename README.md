# peasoup

A Game Boy (DMG) emulator in C++20, checked against 121 hardware test ROMs on every push.

[![CI](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml/badge.svg)](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Nell-Kh/gameboy-emu)](https://github.com/Nell-Kh/gameboy-emu/releases)

![Libbet and the Magic Floor playing its demo in peasoup's window](docs/media/libbet-demo.gif)

*[Libbet and the Magic Floor](https://github.com/pinobatch/libbet) by Damian Yerrick (zlib
licence), playing its built-in demo in peasoup's window. The GIF is silent; the emulator plays
the game's music and sound effects.*

peasoup runs original Game Boy games: the CPU, the picture, the sound, the timer, the cartridge
chips with their battery saves and real-time clock, and save states. It is built to be measured
rather than eyeballed: every claim about accuracy is a test ROM that passes in CI, and what does
not pass is listed with its cause. The emulator is a library with no dependencies; a small SDL2
program around it provides the window, keyboard and sound.

## Features

- The SM83 CPU with every instruction and the timing of each memory access (ADR-005)
- The PPU: background, window, objects, all four modes with their timing, STAT interrupts
- Sound: the two square channels with sweep and envelope, the wave channel and the noise
  channel, mixed to 48 kHz stereo; the audio queue sets the game's pace, so sound never crackles
  or drifts (ADR-025)
- Cartridges: no mapper, MBC1, MBC3 with its real-time clock, MBC5
- Battery saves in `game.sav`, with the cartridge clock in BGB's footer format (ADR-019)
- Save states on F5 / F8: versioned, checksummed, refused with a reason if they do not fit
  (ADR-020)
- Fast-forward, pause, reset, screenshots, window size 1x-4x
- A headless mode that runs test ROMs and reads their verdict

## Accuracy

121 test ROMs run in CI and all pass:

| Source | Passing in CI |
|---|---|
| Blargg (`cpu_instrs`, `instr_timing`, `mem_timing`, `mem_timing-2`, `halt_bug`) | 22 |
| Blargg `dmg_sound` (12 singles and the combined screen) | 13 |
| Mooneye Test Suite (`acceptance`, `emulator-only`) | 81 |
| dmg-acid2, mbc3-tester, rtc3test (pixel-identical screens) | 5 |

The Mooneye and Blargg ROMs that do not pass are the ones for other models, MBC2, the DMG's OAM
corruption bug and the exact state the boot ROM leaves; [docs/accuracy.md](docs/accuracy.md) lists
every one with its cause, and every simplification.

## Engineering

- **704 tests** in CTest, run under AddressSanitizer and UndefinedBehaviorSanitizer: 565 unit
  tests; 129 on real ROMs (the 121 test ROMs, 5 save-state determinism runs, 3 PNG checks); 9
  command-line checks; and the check that the generated decoder is up to date. The app job adds 2
  window tests.
- **CI on every push:** gcc and clang on Linux, clang on macOS, a lint job (clang-format and
  clang-tidy with warnings as errors), and an app job that starts the real window with SDL's
  dummy video and audio drivers and checks the save file is written on SIGTERM and SIGINT
  (ADR-021).
- **27 design decisions** recorded as short ADRs in [docs/DECISIONS.md](docs/DECISIONS.md), with
  design notes for the [PPU](docs/ppu-notes.md), [cartridges](docs/cartridge-notes.md) and
  [sound](docs/apu-notes.md), and an [architecture overview](docs/architecture.md).
- **About 5 500 hand-written lines** in the core; the instruction decoder (about 2 100 lines) is
  generated from the public gbdev opcode table, and a test fails if it is stale (ADR-006).
- **Determinism:** a save state loaded into a fresh machine continues identically, frame,
  registers, sound samples and every byte of state, checked on real ROMs.
- **Measured, not guessed:** the window reports every audio underrun with the length of the stall
  behind it; that report found the two causes of crackle in v0.5.0 (ADR-025).

## Build and run

Needs CMake 3.24+, Ninja, a C++20 compiler and SDL2.

macOS:

```
brew install cmake ninja sdl2
```

Ubuntu 24.04 (22.04's CMake is too old):

```
sudo apt install cmake ninja-build g++ libsdl2-dev
```

Then:

```
cmake --preset release
cmake --build --preset release
./build/release/app/peasoup path/to/game.gb
```

To build with the sanitizers and run the tests:

```
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The first configure downloads GoogleTest and the public test ROMs. Python 3 is used by one test,
which checks the generated decoder is up to date; without Python that test is left out.
Configuring with `-DGB_APP_SMOKE_TESTS=ON` adds the two window tests, which need Python 3.

**Bring your own ROM.** No ROM, commercial or otherwise, is stored in this repository.
[Libbet](https://github.com/pinobatch/libbet) is a free game that runs well.

## Controls

| Game Boy | Keyboard |
|---|---|
| D-pad | arrow keys |
| A / B | X / Z |
| Start | Enter |
| Select | Backspace |

| Emulator | Keyboard |
|---|---|
| Save state / load state | F5 / F8 |
| Window size 1x-4x | 1-4 |
| Fast-forward (up to 4x) | hold Tab |
| Pause | P |
| Reset | R |
| Screenshot (`game-1.png`, ...) | F12 |
| Quit | Escape |

Battery saves go to `game.sav` and states to `game.state`, next to the ROM. `--scale N` sets the
starting window size, `--mute` turns sound off, and `peasoup --help` lists every option and key.

To run a test ROM without a window:

```
./build/release/app/peasoup --headless path/to/rom.gb
```

It prints what the ROM sends over the serial port (or the verdict Blargg's sound tests write to
cartridge RAM) and exits with 0 on "Passed". `--trace` prints the CPU state before every
instruction.

## Repository layout

| Folder | What is in it |
|---|---|
| `core/` | the emulator as a library: CPU, bus, PPU, APU, timer, cartridges, save states; no dependencies |
| `app/` | the program: the SDL2 window, the headless runner, battery saves, screenshots |
| `tests/` | unit tests, test-ROM runs, screenshot comparisons, the window smoke test |
| `tools/` | the opcode table and the generator that turns it into the instruction decoder |
| `docs/` | design decisions, design notes, the accuracy report, the architecture overview |

## Not supported

- The Game Boy Color, the Game Boy Advance and the Super Game Boy
- The link cable: serial transfers complete as if nothing were plugged in
- MBC2, MBC1 multicarts, and the other rare cartridge types
- The DMG's OAM corruption bug, and the exact state the boot ROM leaves in DIV and the I/O
  registers; execution starts at `0x0100` with the registers the boot ROM leaves

## Licence and credits

peasoup is under the MIT licence (see [LICENSE](LICENSE)). The vendored stb files keep their
own licence (`tests/third_party/stb/LICENSE`).

- The instruction table is [gbdev/gb-opcodes](https://github.com/gbdev/gb-opcodes) (CC0-1.0).
- Test ROMs, downloaded at build time and not stored here: Blargg's test ROMs (Shay Green),
  the [Mooneye Test Suite](https://github.com/Gekkio/mooneye-test-suite) (Joonas Javanainen),
  [dmg-acid2](https://github.com/mattcurrie/dmg-acid2) (Matt Currie), mbc3-tester, and rtc3test
  (aaaaaa123456789),
  through [retrio/gb-test-roms](https://github.com/retrio/gb-test-roms) and
  [c-sp/game-boy-test-roms](https://github.com/c-sp/game-boy-test-roms).
- [Libbet and the Magic Floor](https://github.com/pinobatch/libbet) by Damian Yerrick (zlib
  licence), shown in the GIF.
- [stb_image](https://github.com/nothings/stb) by Sean Barrett reads the reference screenshots in
  the tests (public domain / MIT).
- [Pan Docs](https://gbdev.io/pandocs/) and the gbdev community's hardware research.

"Game Boy" is a trademark of Nintendo. peasoup is not affiliated with Nintendo.
