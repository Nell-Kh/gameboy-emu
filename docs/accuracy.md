# Accuracy

What has been checked against real-hardware test ROMs, and what is known to be simplified.
Everything in the tables under "Test ROMs that pass" runs in CI on every push (gcc and clang on Linux, clang on macOS,
with AddressSanitizer and UndefinedBehaviorSanitizer). The window itself runs in CI only with
SDL's dummy drivers, in one job that checks the save file is written when the app gets SIGTERM
or SIGINT (ADR-021); everything else about the real window is checked by hand.

## Test ROMs that pass

Blargg's `cpu_instrs`, individual ROMs, from
[retrio/gb-test-roms](https://github.com/retrio/gb-test-roms) at commit `c240dd7`:

| ROM | What it covers | Result |
|---|---|---|
| `01-special` | DAA, CPL, SCF, CCF, JR, POP AF and other one-offs | Passed |
| `02-interrupts` | EI, DI, HALT, the timer interrupt | Passed |
| `03-op sp,hl` | ADD SP, LD HL SP+e8, LD SP HL and stack-pointer loads | Passed |
| `04-op r,imm` | 8-bit arithmetic and logic with an immediate operand | Passed |
| `05-op rp` | 16-bit INC, DEC and ADD HL | Passed |
| `06-ld r,r` | every register-to-register load | Passed |
| `07-jr,jp,call,ret,rst` | all jumps, calls, returns and restarts | Passed |
| `08-misc instrs` | PUSH, POP, LDH and loads through memory | Passed |
| `09-op r,r` | 8-bit arithmetic, logic and rotates on registers | Passed |
| `10-bit ops` | BIT, RES and SET on registers | Passed |
| `11-op a,(hl)` | the same operations on memory at HL | Passed |

Blargg's combined ROMs, which run all their sub-tests in one 64 KiB image and need MBC1:

| ROM | What it covers | Result |
|---|---|---|
| `cpu_instrs` | the eleven `cpu_instrs` sub-tests above, in one run | Passed |
| `mem_timing` | the three `mem_timing` sub-tests below, in one run | Passed |

Blargg's timing ROMs, from the same repository:

| ROM | What it covers | Result |
|---|---|---|
| `instr_timing` | the duration of every instruction, measured with the timer | Passed |
| `mem_timing/01-read_timing` | the machine cycle in which each instruction reads memory | Passed |
| `mem_timing/02-write_timing` | the machine cycle in which each instruction writes memory | Passed |
| `mem_timing/03-modify_timing` | the read and the write of read-modify-write instructions | Passed |

[Mooneye Test Suite](https://github.com/Gekkio/mooneye-test-suite), prebuilt, from
[c-sp/game-boy-test-roms](https://github.com/c-sp/game-boy-test-roms) release `v7.0`:

| ROM | What it covers | Result |
|---|---|---|
| `timer/div_write` | writing DIV resets the internal counter | Passed |
| `timer/rapid_toggle` | toggling the timer on and off, and the extra counts it causes | Passed |
| `timer/tim00`, `tim01`, `tim10`, `tim11` | TIMA at each of the four rates | Passed |
| `timer/tim00_div_trigger` and the other three `_div_trigger` | the extra count when a DIV write makes the watched bit fall | Passed |
| `timer/tima_reload` | the one-cycle delay between overflow and reload | Passed |
| `timer/tima_write_reloading` | writing TIMA during and right after the reload | Passed |
| `timer/tma_write_reloading` | writing TMA during the reload | Passed |
| `ei_sequence`, `ei_timing`, `rapid_di_ei` | the one-instruction delay of EI, and DI cancelling it | Passed |
| `intr_timing` | the duration of an interrupt dispatch | Passed |
| `if_ie_registers` | IF and IE behaviour, including the unused bits | Passed |
| `interrupts/ie_push` | dispatch cancelled when pushing PC overwrites IE | Passed |
| `oam_dma/basic`, `oam_dma/reg_read` | OAM DMA copies 160 bytes; its register reads back | Passed |
| `oam_dma/sources-GS` | OAM DMA from every source area, including cartridge RAM (needs MBC5) | Passed |
| `oam_dma_start`, `oam_dma_timing` | the cycle OAM becomes blocked and the cycle it is free again | Passed |
| `oam_dma_restart` | starting a new DMA while one runs | Passed |
| `call_timing`, `call_timing2`, `call_cc_timing`, `call_cc_timing2` | in which cycle CALL reads and writes, timed with OAM DMA | Passed |
| `jp_timing`, `jp_cc_timing`, `ret_timing`, `ret_cc_timing`, `reti_timing`, `rst_timing` | the same for JP, RET, RETI and RST | Passed |
| `push_timing`, `pop_timing`, `add_sp_e_timing`, `ld_hl_sp_e_timing` | the same for the stack instructions | Passed |
| `reti_intr_timing`, `halt_ime1_timing`, `div_timing` | interrupt after RETI, wake from HALT, DIV phase | Passed |
| `halt_ime0_ei` | EI right before HALT makes HALT behave as with IME on | Passed |
| `halt_ime0_nointr_timing`, `halt_ime1_timing2-GS` | waking from HALT costs exactly what a run of NOPs would | Passed |
| `di_timing-GS` | DI takes effect immediately on a DMG | Passed |
| `instr/daa` | DAA for every input | Passed |
| `boot_regs-dmgABC` | CPU registers after boot | Passed |
| `emulator-only/mbc1/bits_bank1`, `bits_bank2`, `bits_mode`, `bits_ramg` | which bits of each MBC1 register count | Passed |
| `emulator-only/mbc1/rom_512kb` to `rom_16Mb` (6 ROMs) | ROM banking at every MBC1 ROM size | Passed |
| `emulator-only/mbc1/ram_64kb`, `ram_256kb` | RAM banking and enabling | Passed |
| `emulator-only/mbc5/rom_512kb` to `rom_64Mb` (8 ROMs) | MBC5 ROM banking at every size: bank numbers 0-255, bank 0 mappable at `4000`, bits 1-7 of the bank-bit-8 register ignored | Passed |
| `ppu/intr_2_0_timing`, `intr_2_mode0_timing`, `intr_2_mode3_timing` | mode 2, 3 and 0 boundaries, through STAT interrupts and the mode bits | Passed |
| `ppu/intr_2_oam_ok_timing` | the dot OAM becomes readable after mode 2 | Passed |
| `ppu/intr_1_2_timing-GS` | from the mode 1 STAT interrupt to the next mode 2 | Passed |
| `ppu/vblank_stat_intr-GS` | the mode 2 select also fires when VBlank starts | Passed |
| `ppu/stat_irq_blocking` | STAT interrupts only on a rising edge of the shared line | Passed |
| `ppu/stat_lyc_onoff` | the LYC = LY bit while the LCD is switched off and on | Passed |
| `ppu/hblank_ly_scx_timing-GS` | the `SCX % 8` delay in mode 3, against the LY change | Passed |
| `ppu/intr_2_mode0_timing_sprites` | how much objects lengthen mode 3, in 105 object layouts | Passed |
| `bits/unused_hwio-GS` | unused and write-only I/O bits read as 1; unmapped I/O reads `0xFF` | Passed |
| `bits/mem_oam` | the sprite table (OAM) is readable and writable | Passed |
| `bits/reg_f` | the low four bits of F are always 0 | Passed |

[dmg-acid2](https://github.com/mattcurrie/dmg-acid2) by Matt Currie, from the same c-sp
collection. When the ROM reaches `LD B, B`, the frame is compared with `dmg-acid2-dmg.png` in
shades 0-3. `mbc3-tester`, from the same collection, never stops; its frame is compared with
`mbc3-tester-dmg.png` after 60 frames (c-sp's notes say the result is final after 40).
`rtc3test` has three subtests picked on a menu; each test presses the buttons from c-sp's how-to
(held 5 frames, 20 frames apart, from frame 60), runs for the subtest's time plus 2 emulated
seconds, and compares the screen with its reference:

| ROM | What it covers | Result |
|---|---|---|
| `dmg-acid2` | background, window and its line counter, objects, flips, 8x16 objects, object priority, the 10-objects-per-line limit, tile addressing modes | Pixel-identical |
| `mbc3-tester` | MBC3 ROM banking: all 256 banks of a 4 MiB ROM (the MBC30's 8-bit bank register, ADR-022) | Pixel-identical |
| `rtc3test` basic tests (A) | the clock starts, ticks every 1000 ms (±1 ms), stops while halted, reads back writes, rolls over into day 256, sets and keeps the day carry | Pixel-identical |
| `rtc3test` range tests (Down, A) | which bits of each clock register exist; invalid seconds, minutes and hours count on and wrap without a carry | Pixel-identical |
| `rtc3test` sub-second writes (Down, Down, A) | writing the seconds restarts the second in progress, writing the other registers does not, and halting pauses it (±1.5 ms) | Pixel-identical |

To reproduce one by hand:

```
./build/release/app/gameboy-emu --headless path/to/01-special.gb
```

These ROMs have no pass or fail verdict the emulator can read; they are checked by the
`DmgAcid2`, `Mbc3Tester` and `Rtc3Test` tests in `rom_tests`, which on a mismatch save the frame
as `<reference>-actual.png` next to the reference and print the first differing pixel.

## Games checked by hand

Not run in CI: these were run by hand and the screen was checked by eye.

| Game | What was checked | Result |
|---|---|---|
| [Libbet and the Magic Floor](https://github.com/pinobatch/libbet) v0.08 (zlib licence), built from source at commit `1232e71` with RGBDS 1.0.4 | title and story screens; the built-in demo; on macOS with the keyboard: rolling, jumping (A + d-pad), finishing floors and moving on to the next, the progress counter. Start does nothing during a floor, as in the game's source (it has no pause). | Plays correctly, without sound |

Tetris was not checked: it needs a ROM of the commercial game, which this project does not
provide.

## Not yet tested in CI

- `emulator-only/mbc1/multicart_rom_8Mb`: MBC1 multicarts (MBC1M) wire the bank register
  differently and are not supported.
- The `emulator-only/mbc2` tests: MBC2 is not supported.
- MBC5 bank bit 8: the Mooneye `mbc5` ROMs never set it (they only check bank numbers 0-255), so
  banks 256-511 are checked by unit tests only.
- The other Mooneye acceptance tests in the same folders. All were run by hand; these do not pass:
  - `boot_div-dmgABCmgb` and `boot_hwio-dmgABCmgb`: the exact DIV phase and the I/O register values
    the boot ROM leaves behind are not modelled.
  - `boot_*` tests for other models (`dmg0`, `mgb`, `sgb`, `sgb2`, `-S`) are expected to fail on a
    DMG.
  - `serial/boot_sclk_align-dmgABCmgb`: the phase of the serial clock the boot ROM leaves behind is
    not modelled.
  - `ppu/lcdon_timing-GS`: LY matches throughout. The first mismatch is the LYC = LY bit in STAT at
    machine cycle `$6F` after the LCD is switched on: expected clear (`$80`), ours set (`$84`).
  - `ppu/lcdon_write_timing-GS`: at machine cycle `$83` after the LCD is switched on an OAM write
    should go through (`$81`); ours is blocked (`$00`).
  - Both `lcdon` tests state their own cause: on the first line after the LCD is switched on the
    PPU runs 2 dots late. That line starts in mode 0 here as on hardware, but the 2-dot lateness is
    not modelled; that is the likely cause of both mismatches, not yet confirmed.
- Sound (M5).

## Known simplifications

Everything that is approximated, missing or unverified. If you find one that is not listed here,
that is a bug in this document.

**CPU**

- **STOP:** resets DIV and the CPU sleeps until a joypad interrupt is requested (a button
  pressed in a selected group), but the clock keeps running. On hardware the oscillator stops, so
  the timer would freeze.
- **HALT** is modelled as a run of opcode fetches: waking costs no extra cycle, and EI directly
  before HALT with an interrupt already pending services it and returns to the HALT (ADR-013).
  The exact timing of that EI-then-HALT case is not covered by any test ROM.
- **Interrupt dispatch** takes 20 ticks and is checked after the opcode fetch (ADR-009). The one
  mid-dispatch effect that is modelled is the high-byte push overwriting IE (the jump goes to
  `0x0000`); other cancellation cases are not.
- **Undefined opcodes** (the 11 unused encodings) lock the CPU, which only burns time afterwards.
  This matches hardware, where they hang the CPU until power-off. It is listed because it is not
  verified by a test ROM, only by unit tests.

**Timer**

- Modelled: TIMA driven by a falling edge of a DIV-counter bit, the one-machine-cycle delay
  between overflow and reload, a TIMA write during that delay cancelling the reload, a TIMA write
  in the reload cycle being ignored, a TMA write in the reload cycle reaching TIMA, and the extra
  count when a DIV or TAC write makes the watched bit fall.
- Verified by unit tests and by all 13 Mooneye timer tests listed above.
- Writes land on machine-cycle boundaries; nothing finer than 4 ticks is modelled.

**Serial**

- Internal clock only, at the fixed DMG rate. Nothing is ever connected, so every transfer
  shifts in `0xFF`, and a transfer waiting on an external clock never completes.

**PPU**

- Modes, LY, LYC and the STAT interrupt line advance per dot (`docs/ppu-notes.md`). Mode 3 lasts
  172 dots plus `SCX % 8`, 6 if the window starts on the line, and the object penalty: the Pan
  Docs formula with two changes taken from `intr_2_mode0_timing_sprites`, which it matches in all
  105 layouts (`docs/ppu-notes.md`, section 2). The test sees mode 3's end only to the machine
  cycle, so the penalty is confirmed to that precision, not to the dot. Whether the 3-dot credit
  per line belongs to the object fetch or to where mode 3 ends within its machine cycle is not
  settled.
- LY shows the next line's number during the last 4 dots of each line; this is what
  `hblank_ly_scx_timing-GS` needs. LYC is compared with the line itself, not with that early value.
- **Line 153:** other emulators report that LY reads 0 from dot 4 of line 153 and that LYC is
  compared against 0 for the rest of that line. Not modelled; no M3 target needs it
  (`docs/ppu-notes.md`, section 2).
- The first line after the LCD is switched on starts in mode 0 and goes straight to mode 3, but
  without the 2-dot lateness the `lcdon_*` tests measure.
- At power-on the LCD is on and the PPU is at the start of line 0 in mode 2. The exact position
  the real boot ROM leaves it at is not modelled.
- The STAT write quirk of the DMG (a write can raise a STAT interrupt as if `FF` had been written
  for one cycle) is not modelled.
- The background, window and objects are drawn by a scanline renderer (ADR-014): each line is
  drawn in one go when its mode 3 starts, so register writes during mode 3 (mid-line scroll,
  palette or window changes) take effect from the next line.
- **Window quirks:** on hardware WX = 0 shifts the window left by `SCX % 8`, and WX = 166 shows
  a column at the right edge shifted down a line. Neither is modelled: WX = 0 here just hides the
  window's first 7 columns, and WX = 166 shows window column 0 at screen x 159.
- LY = WY is checked at the start of each line's mode 2. The first line after the LCD is switched
  on has no mode 2, so for that line it is checked at the moment the LCD is switched on.
- That first line also has no OAM scan, so it draws no objects. Not checked against hardware.
- Objects are drawn with the object size (LCDC bit 2) at the start of mode 3; the OAM scan uses
  the size at the start of mode 2. A change between the two is not modelled further.

**Memory and I/O**

- **Memory map:** echo RAM (`0xE000-0xFDFF`) mirrors work RAM and the unusable region
  (`0xFEA0-0xFEFF`) reads `0x00` and ignores writes; on hardware the unusable region also reads
  differently while the PPU is scanning OAM, which is not modelled. Video RAM is blocked during
  mode 3 and OAM during modes 2 and 3 (blocked reads return `FF`, writes are ignored).
- **OAM DMA** copies one byte per machine cycle and blocks OAM (reads `0xFF`, writes ignored) from
  the second cycle after the write to `0xFF46` until 160 cycles later, as the Mooneye tests check.
  Not modelled: on hardware the CPU also cannot use the bus the DMA is reading from (ROM, work RAM)
  and reads the DMA's byte instead. Here only OAM is blocked; everything else stays usable.
- **Cartridges:** no mapper (types `0x00`, `0x08`, `0x09`), MBC1 (`0x01`-`0x03`), MBC3
  (`0x0F`-`0x13`) and MBC5 (`0x19`-`0x1E`) are supported; every other type is refused with a
  message. On MBC3 ROMs over 2 MiB the ROM bank register is 8 bits wide, as on the MBC30
  (ADR-022). On MBC5 cartridges with a rumble motor (`0x1C`-`0x1E`) the motor bit is honoured (it
  no longer selects RAM) but no rumble is produced. The file size must match the header. MBC1
  multicarts are not detected. The header checksum is not checked; the real boot ROM would refuse
  to start a cartridge with a bad one.
- **Battery saves:** battery-backed RAM is kept in `game.sav` next to the ROM by the window (never
  in headless mode): written on exit, on SIGINT / SIGTERM, and at most once a second while the
  program changes it; a save file of the wrong size is neither loaded nor overwritten. For MBC3
  with a timer, BGB's 48-byte clock footer follows the RAM (the 44-byte form is also read), so
  the file is RAM + 48 bytes, or 48 bytes with no RAM.
- **MBC3 clock** (types `0x0F`, `0x10`): counts emulated time, not the wall clock (ADR-018), so
  it falls behind real time whenever emulation runs slower than real time. With no save file it
  starts at day 0, 00:00:00 and running; on hardware the registers start with whatever the chip
  held. When a save is loaded, the seconds since it was written are added (none if the clock was
  halted or the saved time is in the future; passing day 511 sets the carry), matching
  second-by-second ticking. The footer has no room for the fraction of a second, so the second
  in progress restarts at load. The clock is always written on exit, but during play only when
  the program changes RAM or a clock register; after a crash, the catch-up from the last write
  still brings it forward to real time.
- **I/O registers:** joypad, serial, timer, interrupt and all LCD registers (`FF40`-`FF4B`) are
  emulated. Addresses with no register read `0xFF`. The sound registers are plain memory whose
  unused and write-only bits read as 1. They start at 0, not at their post-boot values, and have
  no side effects: switching sound off in NR52 does not block or clear the other sound registers.
- **LCD off:** the frame goes blank (shade 0) while the LCD is off. On hardware the screen is
  then a little lighter than shade 0.
- **Frontend:** the window runs one whole frame at a time and paces itself to 59.73 frames per
  second (ADR-016). Input is read once per frame, so a key press reaches the game up to one frame
  later than on hardware.
- **Joypad:** a button press shows in `FF00` and requests the joypad interrupt the moment the
  frontend reports it. Not modelled: switch bounce, which on hardware can request the interrupt
  several times per press, and the few cycles a newly selected group takes to show. Opposite
  d-pad directions held at once, which a real d-pad cannot do, are left to the frontend to prevent.
- **Boot ROM:** not run. The CPU registers, DIV, IF, LCDC and BGP start at their post-boot values.
