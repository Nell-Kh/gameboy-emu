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

## Not yet tested

- The combined `cpu_instrs.gb` (needs MBC1 bank switching, M2).
- `instr_timing` and `mem_timing` (M2).
- Anything involving the screen or sound (M3, M5).

## Known simplifications

- **STOP:** the CPU sleeps until a button press and DIV is reset, but the clock keeps running.
  On hardware the oscillator stops, so the timer would freeze.
- **Memory map:** `0x8000-0xFEFF` is plain RAM. Echo RAM, the unusable region and video-RAM
  access rules arrive with the PPU and the M2 memory work.
- **Cartridges:** only the first 32 KiB of a ROM are visible. There is no mapper yet.
- **I/O registers:** only serial, timer and the interrupt registers behave like hardware. The
  rest are plain memory and start at 0, not at their post-boot values.
