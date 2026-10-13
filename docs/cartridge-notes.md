# Cartridge notes

What M4 must do, written before the code: MBC5, battery saves, MBC3 with its real-time clock,
and save states. Sources:

- Pan Docs: [MBC3](https://gbdev.io/pandocs/MBC3.html), [MBC5](https://gbdev.io/pandocs/MBC5.html),
  [The Cartridge Header](https://gbdev.io/pandocs/The_Cartridge_Header.html).
- The Mooneye `emulator-only/mbc5/` sources and their harness `mbc5_rom.s`
  ([mooneye-test-suite](https://github.com/Gekkio/mooneye-test-suite), commit `31510e1`; the ROMs
  we run come from c-sp `v7.0`, ADR-011).
- [rtc3test](https://github.com/aaaaaa123456789/rtc3test) at commit `80ae792`: its `tests.md`
  (what each subtest checks) and `src/` (how it reads and writes the clock).
- c-sp's how-to files for `rtc3test` and `mbc3-tester` in the same `v7.0` collection: the button
  sequences, run times and reference screenshots.
- BGB's [MBC3 RTC save format](https://bgb.bircd.org/rtcsave.html), which VBA-M and other
  emulators also read.

A statement marked **(test)** comes from a test ROM's expectations rather than the Pan Docs. A
statement marked **(choice)** is our decision where neither source settles it; each is listed in
accuracy.md once implemented.

## 1. Cartridge types after M4

From the header table at `0x0147`. Rows in bold are new in M4.

| Code | Type | Notes |
|---|---|---|
| `00`, `08`, `09` | no mapper (+ RAM, + battery) | as in M2 |
| `01`-`03` | MBC1 (+ RAM, + battery) | as in M2 |
| **`0F`** | **MBC3 + timer + battery** | `rtc3test` is this type, with no RAM |
| **`10`** | **MBC3 + timer + RAM + battery** | |
| **`11`-`13`** | **MBC3 (+ RAM, + battery)** | `mbc3-tester` is `11` with a 4 MiB ROM (section 3) |
| **`19`-`1B`** | **MBC5 (+ RAM, + battery)** | the Mooneye `mbc5` ROMs are `19`; `sources-GS` is `1B` with 8 KiB RAM |
| **`1C`-`1E`** | **MBC5 + rumble (+ RAM, + battery)** | rumble is not emulated (section 2) |

Everything else stays refused with a message, as now. "Battery" in the type name is what decides
whether RAM (and the clock) is saved to a file (section 6).

## 2. MBC5

Pan Docs, MBC5.

| Writes to | Register | Effect |
|---|---|---|
| `0000-1FFF` | RAM enable | `0A` enables cartridge RAM, anything else disables it. Pan Docs: real MBCs only look at the low 4 bits; we do the same, as for MBC1. |
| `2000-2FFF` | ROM bank, low 8 bits | |
| `3000-3FFF` | ROM bank, bit 8 | only bit 0 of the value counts |
| `4000-5FFF` | RAM bank | bits 0-3 select one of up to 16 banks of 8 KiB |
| `6000-7FFF` | nothing | |

- `0000-3FFF` is always bank 0. `4000-7FFF` shows the selected bank, and **bank 0 can be
  selected there** ("bank 0 is actually bank 0"), unlike MBC1 and MBC3.
- The 9-bit bank number is masked to the ROM's bank count, so a 1 MiB ROM (64 banks) wraps every
  64 banks. **(test)** The Mooneye `mbc5` ROMs check all 512 bank numbers against exactly this
  wrap, and write `0x3000` with bits 1-7 set "to expose bugs", so those bits must be ignored.
- RAM banks are masked to the RAM size. With no RAM, `A000-BFFF` reads `FF`.
- **Rumble (`1C`-`1E`):** bit 3 of the RAM bank register drives the motor instead of selecting
  RAM, so only bits 0-2 select a bank. The motor itself is not emulated **(choice)**.

## 3. MBC3

Pan Docs, MBC3.

| Writes to | Register | Effect |
|---|---|---|
| `0000-1FFF` | RAM and timer enable | `0A` enables both RAM and the clock registers; only the low 4 bits are compared, as for MBC1 and MBC5 **(choice)** |
| `2000-3FFF` | ROM bank | 7 bits; `00` selects bank `01`, as on MBC1, but banks `20`, `40`, `60` are reachable |
| `4000-5FFF` | RAM bank or clock register | `00`-`03` select a RAM bank, `08`-`0C` a clock register |
| `6000-7FFF` | latch clock | writing `00` then `01` copies the clock into the readable registers |

- With RAM and timer disabled, `A000-BFFF` reads `FF` and writes are ignored.
- **ROMs over 2 MiB (MBC30).** Plain MBC3 has 7 bank bits (128 banks, 2 MiB). MBC30, used only in
  the Japanese Pokémon Crystal, has 8 bits and up to 64 KiB RAM (8 banks), under the same type
  codes. **(test)** `mbc3-tester` is type `11` with a 4 MiB ROM, and its DMG reference
  screenshot ticks all 256 banks. So: the ROM bank register is 8 bits wide and masked to the
  ROM's bank count, and the RAM bank register selects up to 8 banks masked to the RAM size. For
  ROMs of 2 MiB or less this is the same as plain MBC3.
- A RAM bank / register value outside `00`-`07` and `08`-`0C` maps nothing: reads `FF`, writes
  are ignored **(choice)**.

## 4. The real-time clock

**Registers**, selected with `08`-`0C` and read or written at `A000-BFFF`:

| Select | Register | Bits that exist |
|---|---|---|
| `08` | S, seconds | 0-5 |
| `09` | M, minutes | 0-5 |
| `0A` | H, hours | 0-4 |
| `0B` | DL, day counter bits 0-7 | 0-7 |
| `0C` | DH: bit 0 = day bit 8, bit 6 = halt, bit 7 = day carry | 0, 6, 7 |

Bits that do not exist read 0 **(test: range tests, "Valid bits")**.

**Latch.** Reads return a latched copy of the clock. Writing `00` then `01` to `6000-7FFF` copies
the live clock into it; the copy then stays fixed while the clock runs (Pan Docs). rtc3test
latches before every read (`latch_RTC` in `read_RTC_register` and `ReadRTC`), so it does not say
what a read returns right after a write without a new latch. **(choice)** A write updates both
the live register and the latched copy, so it reads back without a latch.

**Ticking**, once per second while the halt bit is 0. Each step is from rtc3test's expectations
**(test)**:

- S: 59 goes to 0 and carries into M. 63 (the largest value that fits) goes to 0 **without**
  carrying. Anything else goes up by one, so 60-62 count on to 63.
- M: the same, 59 → 0 with a carry into H, 63 → 0 without.
- H: 23 → 0 with a carry into the day counter, 31 → 0 without; 24-30 count on.
- Day counter (9 bits, DL plus DH bit 0): 511 → 0 and sets the carry bit, which stays set until
  the program writes it to 0 ("Overflow stickiness").

**The sub-second counter (test: sub-second writes).** The clock counts seconds from an internal
counter that cannot be read but can be measured by waiting for the next tick:

- Writing **S** resets it: the next tick comes a full second later (RTCS/500 and RTCS/900 both
  expect 1000 ms).
- Writing **M, H, DL or DH** does not touch it: the tick comes when it would have (RTCM/50
  expects 50 ms, RTCH/200 200 ms, and so on).
- Setting the **halt** bit stops it where it is, and clearing the bit resumes from there
  ("RTC off/400": stopped 400 ms before a tick, the tick comes 400 ms after restarting).

**Timing tolerance (test).** rtc3test times the clock with the Game Boy's own timer: a tick must
come 1000 ms after the last one within 1 ms, and the sub-second tests allow 1.5 ms. Measured in
emulated time, that is about 4,200 clock ticks out of 4,194,304 per second.

## 5. Where the clock's time comes from

A decision for review (ADR-018 once agreed).

**Proposal: the clock advances with emulated time; the wall clock is only read by the frontend,
at load.** The cartridge gets `tick(t_cycles)` from the bus, like the timer, and the clock counts
4,194,304 ticks per second while not halted. The wall clock enters only when a battery file is
loaded: the frontend compares the timestamp saved in the file (section 6) with the current time
and tells the cartridge to advance by the seconds that passed while the emulator was closed.

Why this rather than giving the cartridge a wall-clock time source with a fake for tests:

- rtc3test checks the clock against the emulated timer to 1 ms. A headless test runs many times
  faster than real time, so any clock that reads real time fails it; the fake would have to
  advance with emulated ticks anyway.
- Save states need no stored wall time to be deterministic: the same state and the same input
  give the same clock readings.
- The core keeps doing no I/O and reading no clock. The one place that knows real time is the
  frontend's battery-file code, and the elapsed seconds are a plain argument, so tests pass fixed
  numbers.

The cost: while the emulator runs slower than real time the in-game clock falls behind, and a
future fast-forward would speed it up. The window runs at real speed (ADR-016), so in normal play
the two stay together. Listed in accuracy.md as a simplification.

## 6. Battery saves

**Who does what.** The core never touches files. It exposes, for a cartridge with a battery:
the RAM bytes, a way to load them, a "changed since last save" flag, and for MBC3 + timer the
clock state to write and read back. The frontend owns the file (ADR-019 once agreed).

**The file.** Next to the ROM, with the extension replaced: `game.gb` → `game.sav`. Contents:

1. The cartridge RAM, exactly the size the header declares (raw bytes, as other emulators write
   them, so a `.sav` can move between emulators).
2. For a cartridge with a timer, the BGB format appended: 48 bytes, all little-endian 32-bit
   fields with the value in the low byte. Offsets 0-19: the live S, M, H, DL, DH. Offsets 20-39:
   the latched S, M, H, DL, DH. Offsets 40-47: the Unix time of the save, 64-bit. On load the
   44-byte variant (32-bit time, written by old VBA) is also accepted.

**Loading,** when the ROM is opened in the window:

- No file: start with RAM as the cartridge powers on.
- File of the right size (RAM, or RAM + 44 / + 48 with a timer): load it, then advance the clock
  by `now - saved time` seconds. A saved time in the future is ignored, as BGB and VBA-M do.
- Any other size: do not load it, print why, and **never overwrite it**, so a file from another
  emulator or a different ROM is not destroyed.

**Writing** **(choice)**:

- When the window closes.
- During play, at most once a second, and only if the RAM changed since the last write. Games
  often write a save in several steps, so writing on every change would be slow and pointless.
- Always to `game.sav.tmp` first, then renamed over `game.sav`, so a crash mid-write leaves the
  old save intact.

**Headless mode never reads or writes save files**, so CI runs stay clean and repeatable.

**"A save survives quit + relaunch"**, the M4 exit, is tested at two levels:

- Core: a test program writes a counter to battery RAM; the RAM is taken out, put into a fresh
  `GameBoy` with the same ROM, and the program finds the counter it left. No files, runs in CI.
- Frontend: the `.sav` reading and writing code (sizes, the RTC footer, the temp-then-rename
  write, refusing a wrong size) has its own unit tests on a temporary directory, without SDL.
- By hand: a real game with battery RAM saved, closed and reopened in the window.

## 7. Save states

A snapshot of the whole machine, saved and loaded from the window: **F5** saves, **F8** loads,
one file per ROM, `game.state` **(choice)**. Headless mode does not use them; tests call the core
directly.

**Format** (ADR-020 once agreed). Everything little-endian, written field by field. Never a
`memcpy` of a struct, so padding and compiler layout can never leak into the file.

Header, 32 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the ASCII bytes `GBEMUSTA` |
| 8 | 4 | format version, starting at 1 |
| 12 | 1 | the ROM's header checksum (byte `0x014D`) |
| 13 | 1 | zero |
| 14 | 2 | the ROM's global checksum, as stored at `0x014E`-`0x014F` |
| 16 | 16 | the ROM title from the header, padded with zeros |

The checksums and title identify the game, so a state is never loaded into a different ROM.

Then sections, each a 4-byte ASCII tag, a 32-bit length and the payload, in this order. A loader
that finds a missing section, a wrong length or a different version refuses the file with a
message and leaves the running machine untouched.

| Tag | Contents, in order | Bytes |
|---|---|---|
| `CPU ` | A, F, B, C, D, E, H, L (1 each); SP, PC (2 each); IME (1: 0 off, 1 pending after EI, 2 on); halted, stopped, locked, HALT-bug pending (1 each) | 17 |
| `BUS ` | work RAM `C000-DFFF` (8192); the plain I/O bytes `FF00-FF7F` (128); high RAM `FF80-FFFE` (127); IF, IE (1 each); total clock ticks (8); DMA ticks carried into the next machine cycle (4) | 8461 |
| `TIMR` | internal counter (2); TIMA, TMA, TAC (1 each); overflow state (1: none, delay after the wrap, just reloaded); ticks left in it (1) | 7 |
| `SERL` | SB, SC (1 each); ticks left in the transfer (4) | 6 |
| `DMA ` | the FF46 value (1); requested, starting, active: each a presence byte and its value (1+1, 1+1, 1+2) | 8 |
| `JOYP` | select bits (1); pressed buttons (1) | 2 |
| `PPU ` | VRAM (8192); OAM (160); LCDC, STAT select bits, SCY, SCX, LYC, BGP, OBP0, OBP1, WY, WX (1 each); line, dot, mode-3 length (4 each); mode (1); first line, window Y reached (1 each); window line (4); LYC = LY, STAT line high (1 each); this line's objects (10) and their count (1); frames completed (8); the frame buffer (23040) | 31442 |
| `CART` | mapper registers, then RAM, then for MBC3 + timer the clock; the exact layout per mapper is listed when each mapper is written | varies |

Left out on purpose: the serial output text (a test aid, not hardware), the per-line background
index buffer (rebuilt at the start of every line), and the ROM itself.

**Versioning.** Any change to any section's layout bumps the format version. M4 loads only its
own version; converting old states can come later if it is ever needed.

**Determinism test.** Run a ROM for N frames and save a state; run M more frames and keep the
frame and the CPU registers; load the state, run the same M frames with the same input, and
require an identical frame, registers and tick count. Run with Libbet's demo and with a Blargg
ROM. A second test saves, loads into a fresh `GameBoy`, saves again, and requires the two files
to be byte-identical.

## 8. Targets

**Mooneye `emulator-only/mbc5/`**, all 8: `rom_512kb`, `rom_1Mb`, `rom_2Mb`, `rom_4Mb`, `rom_8Mb`,
`rom_16Mb`, `rom_32Mb`, `rom_64Mb`. Pass condition as for every Mooneye ROM (`LD B, B` and the
Fibonacci registers).

**Mooneye `acceptance/oam_dma/sources-GS`**: already passes its DMA checks by hand with a
permissive loader; it only needs type `1B` to load.

**`mbc3-tester`**: screenshot test. c-sp's how-to: the ROM loops forever and the result can be
checked after the first 40 frames; compare with `mbc3-tester-dmg.png` in shades, like dmg-acid2.

**`rtc3test`**: three screenshot tests, one per subtest. The subtest is chosen on the ROM's menu,
so each test plays a fixed input script and records it in the test code. From c-sp's how-to:

| Subtest | Buttons | Run time (emulated seconds) | Reference |
|---|---|---|---|
| basic tests | A | 13 | `rtc3test-basic-tests-dmg.png` |
| range tests | Down, A | 8 | `rtc3test-range-tests-dmg.png` |
| sub-second writes | Down, Down, A | 26 | `rtc3test-sub-second-writes-dmg.png` |

The input runs through a test-only helper (press a button at frame N for a few frames, then
release), not a new command-line option: the tests drive `GameBoy::set_button` and `run_frame`
directly. The exact frames are chosen when the test is written and kept in it.

**The screenshot comparison** is the dmg-acid2 one, moved into a shared test helper: load the
reference with stb_image, map the four greys to shades, compare, and on a mismatch save the frame
next to the reference and print the first differing pixel.

## 9. Order of work and exit

1. **MBC5**, the simplest: the 8 Mooneye `mbc5` ROMs and `sources-GS` green.
2. **Battery saves**: the core interface, then the frontend `.sav` code and its tests, then the
   core round-trip test.
3. **MBC3 and the clock**: `mbc3-tester`, then the three `rtc3test` subtests. Then the RTC footer
   in `.sav` files.
4. **Save states**: the format, then F5 / F8 in the window, then the determinism tests.

Each step is its own commit with its tests, and accuracy.md is updated as it lands.

**M4 exit:** Mooneye `emulator-only/mbc5` and `oam_dma/sources-GS` green in CI, `mbc3-tester`
and the three `rtc3test` subtests matching their screenshots in CI, and a save that survives quit
and relaunch (section 6).

**Also when convenient** (from the M3 review): the object-penalty model that matches all 105
layouts of `intr_2_mode0_timing_sprites`, behind a unit test with the 105 cases.

## 10. Open questions for review

1. Section 5: emulated-time clock with a wall-clock catch-up at load, instead of a wall-clock
   time source injected into the cartridge.
2. Section 6: when battery RAM is written (on close, and at most once a second while changed).
3. Section 7: the save-state header, the section list, and refusing other versions instead of
   converting them.
4. Section 3: treating type `11`-`13` with a ROM over 2 MiB as MBC30 (8-bit bank register), which
   is what `mbc3-tester`'s reference requires.
