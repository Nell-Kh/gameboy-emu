# Accuracy

What has been checked against real-hardware test ROMs, and what is known to be simplified.
Everything in the tables under "Test ROMs that pass" runs in CI on every push (gcc and clang on Linux, clang on macOS,
with AddressSanitizer and UndefinedBehaviorSanitizer).

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
| `bits/unused_hwio-GS` | unused and write-only I/O bits read as 1; unmapped I/O reads `0xFF` | Passed |
| `bits/mem_oam` | the sprite table (OAM) is readable and writable | Passed |
| `bits/reg_f` | the low four bits of F are always 0 | Passed |

To reproduce one by hand:

```
./build/release/app/gameboy-emu --headless path/to/01-special.gb
```

## Not yet tested in CI

- The combined `cpu_instrs.gb` (needs MBC1 bank switching, M2).
- The combined `mem_timing.gb` (the three individual ROMs above cover the same checks).
- Other Mooneye acceptance tests. These were run by hand and do not pass yet:
  - `call_timing`, `jp_timing`, `ret_timing`, `reti_timing`, `push_timing`, `add_sp_e_timing`,
    `ld_hl_sp_e_timing`: they measure timing with OAM DMA, which is not implemented (M2).
  - `halt_ime0_ei`, `halt_ime0_nointr_timing`, `halt_ime1_timing2-GS`: they do not use OAM DMA but
    do read PPU registers; the cause has not been investigated yet.
- No other Mooneye test has been run.
- Anything involving the screen or sound (M3, M5).

## Known simplifications

Everything that is approximated, missing or unverified. If you find one that is not listed here,
that is a bug in this document.

**CPU**

- **STOP:** the CPU sleeps until a joypad interrupt is requested and DIV is reset, but the clock
  keeps running. On hardware the oscillator stops, so the timer would freeze. There is also no
  joypad yet, so nothing in a real program can end a STOP.
- **EI followed directly by HALT:** on hardware an interrupt that is already pending is serviced
  and then returns *to* the HALT, halting again. Here it returns to the instruction after it.
- **Waking from HALT** costs no extra time. Hardware timing around the wake-up cycle is not
  modelled.
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

**PPU (only the line clock exists, ADR-010)**

- LY advances one line every 456 ticks through lines 0-153 while the LCD is on and is 0 while it
  is off. Not modelled: the PPU modes and their timing, line 153 reading as 0 early, LY=LYC and
  the STAT register.
- LCDC is a plain read-write register; none of its bits except "LCD on" has any effect yet.
- At power-on the LCD is on and LY starts at line 0. The exact line position the real boot ROM
  leaves behind is not modelled.
- No rendering, no PPU interrupts (VBlank, STAT), no video-memory access rules.

**Memory and I/O**

- **Memory map:** echo RAM (`0xE000-0xFDFF`) mirrors work RAM and the unusable region
  (`0xFEA0-0xFEFF`) reads `0x00` and ignores writes. Video RAM, cartridge RAM and OAM are plain
  memory: there are no PPU access restrictions, and on hardware the unusable region also reads
  differently while the PPU is scanning OAM.
- **OAM DMA** (`0xFF46`) is not implemented. Writing to it does nothing but store the byte.
- **Cartridges:** only the first 32 KiB of a ROM are visible. There is no mapper, writes to the
  ROM region are ignored, and `0xA000-0xBFFF` is plain RAM whatever the cartridge header says.
- **I/O registers:** SB, SC, DIV, TIMA, TMA, TAC, LCDC, LY, IF and IE are emulated. Addresses
  with no register read `0xFF`. Every other register (joypad, sound, the rest of the PPU) is plain
  memory whose unused and write-only bits read as 1. Those registers start at 0, not at their
  post-boot values, and have no side effects: the joypad always reads "no buttons pressed", STAT
  does not report the PPU mode, and switching sound off in NR52 does not block or clear the other
  sound registers.
- **Boot ROM:** not run. The CPU registers, DIV, IF and LCDC start at their post-boot values.
