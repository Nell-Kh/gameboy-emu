# Accuracy

What has been checked against real-hardware test ROMs, and what is known to be simplified.
Everything in the first table runs in CI on every push (gcc and clang on Linux, clang on macOS,
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

To reproduce one by hand:

```
./build/release/app/gameboy-emu --headless path/to/01-special.gb
```

## Not yet tested in CI

- The combined `cpu_instrs.gb` (needs MBC1 bank switching, M2).
- `instr_timing` and `mem_timing`. They print "Passed" when run by hand with `--headless`, but
  they are not CI tests yet, so they are not claimed (M2).
- Mooneye's timer and interrupt tests. None has been run.
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
- **Interrupt dispatch** takes a fixed 20 ticks. The one mid-dispatch effect that is modelled is
  the push overwriting IE (the jump goes to `0x0000`); other cancellation cases are not.
- **Undefined opcodes** (the 11 unused encodings) lock the CPU, which only burns time afterwards.
  This matches hardware, where they hang the CPU until power-off. It is listed because it is not
  verified by a test ROM, only by unit tests.

**Timer**

- Modelled: TIMA driven by a falling edge of a DIV-counter bit, the one-machine-cycle delay
  between overflow and reload, a TIMA write during that delay cancelling the reload, a TIMA write
  in the reload cycle being ignored, a TMA write in the reload cycle reaching TIMA, and the extra
  count when a DIV or TAC write makes the watched bit fall.
- Verified only by unit tests written from the documented behaviour, plus `02-interrupts`. No
  Mooneye timer test has been run, so the edge cases above are unconfirmed against hardware.
- Writes land on machine-cycle boundaries; nothing finer than 4 ticks is modelled.

**Serial**

- Internal clock only, at the fixed DMG rate. Nothing is ever connected, so every transfer
  shifts in `0xFF`, and a transfer waiting on an external clock never completes.

**Memory and I/O**

- **Memory map:** `0x8000-0xFEFF` is plain RAM. Echo RAM does not mirror, the unusable region
  `0xFEA0-0xFEFF` is writable, and there are no video-RAM or OAM access restrictions.
- **OAM DMA** (`0xFF46`) is not implemented. Writing to it does nothing but store the byte.
- **Cartridges:** only the first 32 KiB of a ROM are visible. There is no mapper, no cartridge
  RAM, and writes to the ROM region are ignored.
- **I/O registers:** only SB, SC, DIV, TIMA, TMA, TAC, IF and IE behave like hardware, including
  their unused bits reading as 1. Every other register, the joypad at `0xFF00` included, is plain
  memory that starts at 0, not at its post-boot value, and has no write-only or unused bits.
- **Boot ROM:** not run. The CPU registers, DIV and IF start at their post-boot values.
