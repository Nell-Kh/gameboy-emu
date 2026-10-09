# Decisions

Short architecture decision records. Newest at the bottom. A decision is changed by adding a new
record that supersedes the old one, not by editing history.

## ADR-001: C++20

- **Context:** The project exists to show modern C++, and it needs fixed-width integers, bit
  operations and `constexpr` everywhere.
- **Decision:** C++20 with compiler extensions off (`-std=c++20`). No exceptions in the hot path,
  no RTTI.
- **Consequences:** `<bit>`, `std::span`, concepts and designated initialisers are available.
  Needs gcc 11+ or clang 14+, which every current CI image has.

## ADR-002: CMake + Ninja, three targets

- **Context:** The core must build and be tested without SDL, so that CI stays fast and the
  emulator logic never depends on a window.
- **Decision:** One CMake project with three targets: `core` (static library, no dependencies),
  `gameboy-emu` (the frontend executable, links `core`), `unit_tests` (GoogleTest, links `core`).
  Ninja is the generator. Named configurations live in `CMakePresets.json` (`debug`, `asan`,
  `release`). GoogleTest is fetched by CMake at a pinned tag, not vendored.
- **Consequences:** A build is `cmake --preset X && cmake --build --preset X`. The first configure
  needs network access to fetch GoogleTest.

## ADR-003: Warnings as errors, for our code only

- **Context:** Warnings that are allowed to accumulate stop being read. Bit-level code is where
  implicit conversions and sign bugs hide.
- **Decision:** `-Wall -Wextra -Wpedantic -Werror` on every target we own, carried by the
  `gb_warnings` interface target. Third-party code does not link it.
- **Consequences:** A new compiler version can break the build with a new warning; that is
  accepted and fixed when it happens. A warning inside GoogleTest cannot break the build.

## ADR-004: Sanitizers on every test run, lint as a gate

- **Context:** An emulator is array indexing and integer arithmetic. Out-of-bounds access and
  undefined behaviour can produce a result that looks right on one compiler and not on another.
- **Decision:** CI builds and runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
  (`asan` preset) on gcc and clang on Linux and on clang on macOS. UBSan findings abort the test
  (`-fno-sanitize-recover`). A separate job fails on any clang-format difference or clang-tidy
  finding.
- **Consequences:** Test runs are slower than a plain build, which does not matter at this size.
  The `release` preset stays unsanitized for real use and for measuring speed.

## ADR-005: The CPU ticks the machine on every memory access

- **Context:** The original plan was for `step()` to run a whole instruction and for the
  scheduler to advance the timer, PPU and APU afterwards by the returned cycle count. That is
  instruction-level accuracy. Blargg `mem_timing`, which is on the must-pass list, checks at which
  machine cycle *inside* an instruction each read and write happens, so that model cannot pass it.
- **Decision:** One machine cycle is 4 clock ticks, and the CPU does at most one memory access per
  machine cycle. Every CPU read, write and internal cycle first calls `Bus::tick(4)`, then does the
  access. `step()` still returns the instruction's total, measured from the bus clock. This
  supersedes the "CPU design" row of the project brief.
- **Consequences:** Accuracy is machine-cycle level: components see each access at the cycle it
  happens. The cost is one function call per access and no separate scheduler loop. Instruction
  lengths are no longer a table to maintain; they fall out of the accesses each instruction makes,
  and tests assert them. Whether the tick belongs before or after the access is confirmed against
  `mem_timing` in M2.

## ADR-006: The instruction decoder is generated from a public opcode table

- **Context:** The SM83 has 500 instructions (245 plain, 256 behind the `0xCB` prefix, minus the
  prefix byte itself). Typing 500 `case` labels by hand is where copy-paste bugs come from, and
  there would be nothing independent to test them against.
- **Decision:** `tools/opcodes.json` is the table from
  [gbdev/gb-opcodes](https://github.com/gbdev/gb-opcodes) (commit `376f61c`, CC0-1.0), copied
  unmodified. `tools/gen_opcodes.py` turns it into `core/opcodes.gen.cpp`: two `switch` statements
  whose cases call small hand-written helpers in `core/cpu.cpp`, plus a table of each
  instruction's name, length, duration and flag behaviour. The generator and its output are both
  committed, and a CTest test fails if the committed output is stale.
- **Consequences:** The interesting code (flags, timing, stack) is about 40 helpers a person can
  read; the repetitive part is mechanical. The generator does not copy durations from the table:
  they come from the memory accesses each instruction makes (ADR-005). That leaves the table as an
  independent reference, and `cpu_opcode_table_test.cpp` checks every instruction's duration,
  length and untouched flags against it, for all 16 flag states. Building needs Python 3 only to
  re-generate or to run that one check.

## ADR-007: Test ROMs are downloaded at a pinned commit, not committed

- **Context:** Correctness is measured by Blargg's test ROMs. The original plan was to keep them
  under `tests/roms/` with their licences. The ROMs are freely distributed and mirrored at
  [retrio/gb-test-roms](https://github.com/retrio/gb-test-roms), but that repository carries no
  licence file, so there is no written permission to redistribute them from this one.
- **Decision:** CMake fetches `retrio/gb-test-roms` at commit `c240dd7` when tests are configured,
  the same way it fetches GoogleTest. `tests/test_roms.cpp` runs each ROM on the whole machine and
  passes when the ROM prints "Passed" over the serial port. `-DGB_FETCH_TEST_ROMS=OFF` builds and
  runs everything else without them.
- **Consequences:** No third-party binary lives in this repository, and the pinned commit keeps
  the tests reproducible. The first configure needs network access (it already did, for
  GoogleTest). If the mirror ever disappears the ROM tests cannot be fetched until the URL is
  changed; the unit tests do not depend on it.

## ADR-008: M1 runs the eleven individual cpu_instrs ROMs; the combined ROM waits for MBC1

- **Context:** `cpu_instrs.gb` is one 64 KiB ROM containing eleven sub-tests, and it needs an MBC1
  mapper to switch banks. The same eleven sub-tests also ship as separate 32 KiB ROMs that need no
  mapper. Cartridge mappers are M2 work.
- **Decision:** M1's exit test is the eleven individual ROMs, each a separate CTest test. The
  combined ROM is added in M2, when MBC1 exists.
- **Consequences:** The instructions tested are identical, and a failure names the instruction
  group directly. What is not yet exercised is bank switching, which M1 does not claim.
  The timer and the tick-before-access order chosen in ADR-005 are exercised here by
  `02-interrupts`, which fails without a working timer interrupt.

## ADR-009: Interrupts are checked after the opcode fetch cycle

- **Context:** Up to M1 the CPU checked for a pending interrupt before fetching the next opcode.
  Mooneye's `rapid_toggle` showed this is one instruction late in one case: when an interrupt
  request arrives during the fetch cycle itself. On hardware the opcode fetch overlaps the end of
  the previous instruction and the check comes after it, so such a request is still serviced
  first.
- **Decision:** `step()` fetches the opcode (one machine cycle, as before) and only then checks
  IME and the pending interrupts. If one is serviced, the fetched opcode is discarded and PC is
  not advanced; that fetch counts as the first of the dispatch's five machine cycles, so dispatch
  still takes 20 ticks in total. The interrupt source is now chosen between the two pushes of PC,
  as on hardware, not after both.
- **Consequences:** `rapid_toggle` and `ie_push` pass, and nothing that passed before broke. Two
  unit tests pin the behaviour: a request landing exactly at the end of a fetch, and a low-byte
  push onto IE that must not cancel the dispatch.

## ADR-010: LY counts scanlines before the PPU exists

- **Context:** Mooneye's test ROMs switch the screen off safely before reporting a result, which
  means waiting for LY (the current scanline, `0xFF44`) to reach the vertical blank. With LY stuck
  at 0 every one of them hangs, whatever its verdict.
- **Decision:** Add `core/ppu.{h,cpp}` with only the line clock: LCDC (`0xFF40`) as a plain
  register, and LY advancing one line every 456 ticks through lines 0-153 while the LCD is on,
  reset to 0 while it is off. Writes to LY are ignored. Rendering, STAT, the PPU interrupts and
  video-memory access rules stay in M3, and they will grow from this class.
- **Consequences:** Mooneye tests can run now. LY is right to the line but has none of the finer
  hardware behaviour (mode timing, the short line 153, LY=LYC), which `docs/accuracy.md` lists.

## ADR-011: Mooneye ROMs come from c-sp's prebuilt collection

- **Context:** The Mooneye Test Suite (MIT) is published as assembly source; building it needs the
  WLA DX assembler. c-sp's [game-boy-test-roms](https://github.com/c-sp/game-boy-test-roms) (MIT)
  publishes it prebuilt, alongside other suites.
- **Decision:** CMake downloads release `v7.0` of that collection as a zip, pinned by SHA-256, and
  the tests use its `mooneye-test-suite/` folder. Blargg stays on its own pinned source
  (ADR-007).
- **Consequences:** No assembler in CI and no ROM in this repository. The Mooneye build in v7.0
  dates from 2022; newer upstream changes are not picked up until the pin is moved.
