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

- **Status:** Accepted, amended. The tick-before-access order is confirmed by `mem_timing` and the
  17 Mooneye instruction-timing tests (M2). Two refinements since: interrupts are checked after
  the opcode fetch, not before it (ADR-009), and a halted CPU's cycles count as opcode fetches
  (ADR-013). OAM DMA and the line clock are driven from the same `Bus::tick` calls, one step per
  machine cycle, so the model itself is unchanged.
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

- **Status:** Completed in M2. MBC1 exists (ADR-012) and the combined `cpu_instrs.gb` and
  `mem_timing.gb` run in CI alongside the individual ROMs.
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

- **Status:** Accepted, amended by ADR-013: the line clock also requests the VBlank interrupt.
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

## ADR-012: Cartridges are a polymorphic interface owned by the bus

- **Context:** Cartridges differ in their mapper chip (none, MBC1, MBC3, MBC5, ...), which decides
  how writes to the ROM area switch banks and how the RAM area behaves. The bus should not know
  which one is inserted.
- **Decision:** `core::Cartridge` is an abstract class with four calls: read and write for the ROM
  area (`0x0000-0x7FFF`) and for the RAM area (`0xA000-0xBFFF`). `NoMbc` and `Mbc1` implement it.
  The bus holds the inserted cartridge as `std::unique_ptr<Cartridge>`: it owns it, a new one
  replaces the old, and with none inserted both areas read `0xFF`. `make_cartridge()` reads the
  header, checks it against the file and picks the class; an unsupported or inconsistent ROM gives
  an error message, not a half-working cartridge.
- **Consequences:** One virtual call per cartridge access, which is negligible next to the rest of
  a memory access. Adding MBC3 and MBC5 in M4 means one new class each and one line in the
  factory. Battery-backed RAM is kept in memory only until M4 adds `.sav` files.

## ADR-013: HALT is a run of opcode fetches; the line clock requests VBlank

- **Context:** Four Mooneye tests were listed as failing for an unconfirmed reason. Reading their
  sources showed two causes. All four wait for the VBlank interrupt, which nothing requested.
  And two of them (`di_timing-GS`, `halt_ime1_timing2-GS`) measure the time from waking out of
  HALT to the next interrupt, to the machine cycle; it was one cycle too long. While halted the
  CPU keeps fetching the opcode after HALT, so the cycle in which the interrupt shows up is
  already that fetch.
- **Decision:** The line clock from ADR-010 requests the VBlank interrupt when line 144 starts.
  When a halted CPU sees an enabled interrupt, it continues straight from the fetch that halted
  cycle made: with IME on it goes into the dispatch's remaining four machine cycles, with IME off
  it executes the next instruction without fetching it again. EI directly followed by HALT with an
  interrupt already pending now services the interrupt with the HALT's own address as the return
  address, so the HALT runs again afterwards.
- **Consequences:** All four tests pass. `Cpu::step()` can return 0 ticks for the instruction right
  after a wake-up with IME off, because its fetch was counted in the last halted step. Unit tests
  pin both wake-up timings and the EI-then-HALT return address.

## ADR-014: The PPU draws a whole scanline at once, with mode timing kept per dot

- **Context:** A PPU can be modelled as a pixel FIFO, one pixel per dot, or as a scanline renderer
  that draws each line in one go. The brief chose the scanline renderer and said to switch only if
  a chosen test ROM needs the FIFO. dmg-acid2 does not write registers during mode 3, and the
  Mooneye `ppu/` targets measure mode and interrupt timing, not pixels.
- **Decision:** Each line is drawn in one step at the start of mode 3, from the registers as they
  are then. Mode changes, the STAT interrupt line, LY, LYC and the VRAM and OAM access rules still
  advance per dot, with mode 3's length computed from the Pan Docs formula
  (`docs/ppu-notes.md`, section 2). Supersedes nothing.
- **Consequences:** Much simpler than a FIFO and fast. Register writes during mode 3 (mid-line
  scroll, palette or window changes) only take effect from the next line, which a few games and
  the Mealybug tests rely on; `docs/accuracy.md` lists this.

## ADR-015: stb_image reads the reference screenshots, in test code only

- **Decision:** `stb_image.h` v2.30 and `stb_image_write.h` v1.16 from
  [nothings/stb](https://github.com/nothings/stb) at commit `2c980bb` (public domain or MIT),
  vendored unchanged in `tests/third_party/stb/` with the licence and linked only into `rom_tests`.

## ADR-016: The window runs whole frames, paced by the wall clock

- **Status:** Superseded by ADR-025 whenever an audio device is open. Still the pacing with
  `--mute`, with no audio device, and in the app-smoke CI job.
- **Context:** The Game Boy draws 59.73 frames per second; most monitors refresh at 60 Hz or more.
  Showing the frame buffer at an arbitrary moment would mix lines of two frames. M5 adds sound,
  which will need its own clock.
- **Decision:** Each loop iteration runs the machine until line 144 starts (`GameBoy::run_frame`),
  shows that complete frame, then sleeps until the next frame is due by the performance counter.
  No vsync. After a stall of more than five frames the schedule restarts rather than catching up.
- **Consequences:** No torn frames and the right game speed on any monitor. A 60 Hz monitor shows
  about one frame in 200 twice. When sound arrives (M5), pacing may move to the audio queue; this
  record would then be superseded.

## ADR-017: The object penalty follows intr_2_mode0_timing_sprites, not the Pan Docs

- **Context:** The Pan Docs formula for how much objects lengthen mode 3 charges an object at
  OAM X = 0 a flat 11 dots. Implemented as written, it matched Mooneye's
  `intr_2_mode0_timing_sprites` in only 44 of its 105 object layouts.
- **Decision:** An object at X = 0 follows the same rule as any other X: 6 dots, plus the
  background-tile wait for the first object on each tile. A line with objects costs 3 dots less in
  total than that formula gives. Both changes are derived from the test's 105 layouts, not from
  documentation; `docs/ppu-notes.md`, section 2, has the formula.
- **Consequences:** The test passes, and a unit test checks all 105 layouts. The test only sees
  the end of mode 3 to the machine cycle, so the rule is confirmed to 4 dots, not to the dot, and
  where the 3 dots come from in the hardware is not known. Do not change it back to the Pan Docs
  version: the unit test and the ROM test will fail.

## ADR-018: The cartridge clock runs on emulated time

- **Status:** Accepted in review (`docs/cartridge-notes.md`, section 5). Done: the clock, its
  footer in the save file, and the catch-up at load, which a unit test checks against
  second-by-second ticking.
- **Decision:** The MBC3 clock advances with emulated clock ticks. The wall clock is read only by
  the frontend when a save file is loaded, to add the seconds the emulator was closed; a halted
  clock does not advance, and passing day 511 sets the carry bit as ticking would.
- **Consequences:** rtc3test can pass headless; save states stay deterministic; the core reads no
  clock. The in-game clock falls behind real time while emulation runs slower than real time.

## ADR-019: Battery saves: the core exposes the RAM, the frontend owns the file

- **Context:** Cartridges with a battery keep their RAM when the Game Boy is off. The core has
  never touched files (the frontend reads the ROM and passes the bytes in), and headless test
  runs must stay repeatable.
- **Decision:** The cartridge holds its RAM and knows from the header type whether it has a
  battery. `GameBoy` exposes the battery RAM, a load that refuses a wrong size, and a flag set
  only when a write changes a RAM byte. The frontend owns the file: `game.sav` next to the ROM,
  raw RAM bytes (plus the BGB clock footer for MBC3 with a timer), written on close, at most once
  a second while the RAM has changed, and on SIGINT / SIGTERM, always through a temporary file
  and a rename, never over a file of the wrong size. Headless mode never reads or writes it.
- **Consequences:** Quit-and-relaunch is tested in the core with no files (a program counts its
  boots across two `GameBoy`s); the file code is tested separately. A `.sav` moves between
  emulators that use the same raw format.

## ADR-020: Save states are a versioned, checksummed, field-by-field file

- **Status:** Accepted in review (`docs/cartridge-notes.md`, section 7). Done: the format and its
  tests in the core (`core/state_io.h`, `core/save_state.cpp`), F5 / F8 and `game.state` in the
  window (`app/state_file.h`).
- **Decision:** A 48-byte header (8-byte magic, format version, ROM checksums and title, CRC-32 of
  the payload, reserved bytes that must be zero), then tagged sections written field by field,
  little-endian, never a `memcpy` of a struct. A file with another version, another ROM, a bad
  CRC, non-zero reserved bytes or a broken section is refused with the reason.
- **Consequences:** States are portable between compilers and machines and never half-load. Any
  layout change bumps the version, and old states stop loading. A crafted file with a matching
  CRC can still hold a value a field does not allow (a PPU mode of 4, say); every such field is
  range-checked on load, and if one fails the machine is put back from a copy taken just
  before, so a refused file changes nothing. Omitting a field from both save and load would
  still round-trip byte for byte, so the tests check behaviour too: determinism runs on real
  ROMs, per-mapper register tests, and section lengths against the documented tables.

## ADR-021: CI starts the window only through SDL's dummy drivers

- **Status:** Accepted; amends ADR-004.
- **Context:** Writing the save on SIGINT and SIGTERM (ADR-019) is window code, and CI had never
  started the window.
- **Decision:** One more CI job, "app smoke", on Linux only: it builds the app with
  `GB_APP_SMOKE_TESTS=ON` and runs `ctest -L app_smoke` with `SDL_VIDEODRIVER=dummy` and
  `SDL_AUDIODRIVER=dummy`, so no real window or audio device is opened. The three existing jobs
  are unchanged and keep testing the core headless. The tests (`tests/app_smoke.py`) start the
  window on a generated ROM, send SIGTERM or SIGINT, and check the exit status and every byte of
  the save file.
- **Consequences:** Start-up, the frame loop, the battery saver and quitting on a signal run in
  CI. Real-window behaviour (vsync, scaling, keyboard input, audio) stays a by-hand check. The
  test relies on timing (the signal comes 0.5 s after the first save, before the next periodic
  write could), so it needs the emulator to run at about a third of real speed or better; it was
  stable in repeated runs with every CPU core busy.

## ADR-022: MBC3 has an 8-bit ROM bank register on ROMs over 2 MiB

- **Context:** Plain MBC3 has 7 ROM bank bits (2 MiB). The MBC30 in the Japanese Pokémon Crystal
  has 8 bits and uses the same type codes, so the header cannot tell them apart.
  `mbc3-tester` is type `11` with a 4 MiB ROM, and its reference screenshot ticks all 256 banks.
- **Decision:** The ROM bank register keeps 7 bits when the ROM is 2 MiB or smaller and 8 bits
  when it is larger; the result is masked to the ROM's bank count, and `00` selects `01`. The RAM
  bank register selects banks `00`-`07`, masked to the RAM size.
- **Consequences:** ROMs of 2 MiB or less behave exactly as plain MBC3 (writing `80` selects bank
  `01`, as on hardware). On a ROM over 2 MiB the 8-bit register is intended, not a bug: changing
  it back to 7 bits fails `mbc3-tester`. A plain MBC3 ROM over 2 MiB would be a bad dump, since
  the chip cannot address it.

## ADR-023: The wave channel's DMG timings come from dmg_sound 09, 10 and 12

- **Context:** On the DMG, while channel 3 plays, the CPU reaches wave RAM only at the moment the
  channel fetches a sample, and a retrigger at the wrong moment corrupts wave RAM
  (docs/apu-notes.md, section 6). Blargg's `09-wave read while on`, `10-wave trigger while on`
  and `12-wave write while on` measure this in steps of 2 clock ticks, 69 runs each, checked by a
  CRC of everything they read. The sources at hand describe the effects but not the offsets
  relative to this emulator's CPU access point, which is the end of each machine cycle
  (ADR-005).
- **Decision:** Three timings, in clock ticks: the first fetch after a trigger comes one sample
  period plus 4; the CPU reaches wave RAM when its access comes 2 ticks after a fetch; a
  retrigger corrupts wave RAM when the next fetch is 4 ticks away. They were found by running
  the three tests for every combination of trigger delay 0-20, access window start 0-4 and
  length 1-3 (09 and 12), then corruption offset 0-10 (with 09 and 12 passing). This is the only
  combination that passes all three.
- **Consequences:** All twelve `dmg_sound` tests and the combined ROM's screenshot pass. Moving
  any of the three by 2 ticks fails at least one of the tests (checked by mutation), so they are
  pinned. They are relative to this emulator's access point and need not match numbers other
  emulators quote for the same effects; if the CPU's access timing model ever changes, the
  search has to be run again. Like ADR-017, they are derived from tests, not documentation.

## ADR-024: Sound output is 48 kHz stereo from a box filter and a high-pass filter

- **Status:** Accepted in review (`docs/apu-notes.md`, sections 7 and 10).
- **Context:** The four channels change level at up to 1 MHz; a sound card wants a fixed sample
  rate. The simplest correct way down is to average the level over each sample (a box filter);
  the proper way is band-limited synthesis (as in `blip_buf`), which keeps the square channels'
  high harmonics from folding back as aliasing.
- **Decision:** The core mixes to 48 000 stereo samples per second: each DAC gives -1 to 1, NR51
  routes it, NR50 scales each side, the sum is scaled so the loudest mix is 1, each sample is the
  average level over its exact stretch of clock ticks (time is counted in units that make the
  arithmetic exact, so there is no drift), and a high-pass filter like the output capacitor
  removes the constant offset a DAC leaves. The output is not part of the machine's state. A tone
  test (the frequency of the real output) and amplitude tests (exact silence with every DAC off;
  a DAC left on settles to silence) check it.
- **Consequences:** Aliasing on the square channels at high notes is accepted for M5; band-limited
  synthesis is the planned v1.1 upgrade and changes only `Apu::produce_samples`. The frontend
  gets samples it can hand to any audio API.

## ADR-025: The window is paced by the audio queue

- **Status:** Accepted in review (`docs/apu-notes.md`, section 7); supersedes ADR-016 while an
  audio device is open.
- **Context:** The sound card has its own clock. Paced by the wall clock (ADR-016), the emulator
  makes exactly 48 000 samples per emulated second while the card plays at its own 48 000, and
  the two drift apart: the queue slowly fills or runs dry, and correcting it means dropping or
  repeating samples (crackle) or nudging the rate (pitch wobble). The user can hear drift against
  the sound card; nothing else in the program can be heard.
- **Decision:** With an audio device open, the loop runs a frame only while less than 50 ms of
  sound is queued and sleeps a millisecond otherwise; the picture follows the sound. Playback
  starts once 50 ms is queued, never on an empty queue. The loop counts underruns (the queue found
  empty after that) and prints the count, with the frame count and run time, on exit, so "no
  crackle" is a number. With `--mute`, with no audio device, and in the app-smoke CI job (which
  passes `--mute`, so the save-on-signal timing does not depend on the dummy audio driver), the
  wall-clock pacing of ADR-016 stays.
- **Consequences:** Sound and picture cannot drift apart, and a stall needs no catch-up logic:
  the queue just refills. The game runs at the sound card's rate, which is within a fraction of
  a percent of nominal on any real card. The M5 exit requires the counter at zero over five
  minutes of Libbet; the dummy driver in CI drains its queue at real time too, so the pacing
  also works there.

