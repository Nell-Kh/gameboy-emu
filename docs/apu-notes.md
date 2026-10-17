# APU notes

What M5 must do, written before the code: the four sound channels, their registers and the
frame sequencer, sound out of the window, and the APU in save states. Sources:

- Pan Docs: [Audio](https://gbdev.io/pandocs/Audio.html),
  [Audio Registers](https://gbdev.io/pandocs/Audio_Registers.html),
  [Audio Details](https://gbdev.io/pandocs/Audio_details.html).
- Blargg's [Gameboy sound hardware](https://gbdev.gg8.se/wiki/articles/Gameboy_sound_hardware)
  notes on the gbdev wiki, written alongside his `dmg_sound` tests; they describe the obscure
  length, sweep and wave-RAM behaviour the tests check.
- Blargg's `dmg_sound` ROMs from the c-sp `v7.0` collection (ADR-011) and c-sp's how-to file for
  them: the 12 single tests, the combined ROM, its run time and its reference screenshot.

A statement marked **(test)** comes from a test ROM's expectations rather than the Pan Docs. A
statement marked **(choice)** is our decision where neither source settles it; each is listed in
accuracy.md once implemented.

## 1. Where things stand

The sound registers (`FF10`-`FF26`) and wave RAM (`FF30`-`FF3F`) are plain memory today: writes
are stored, unused and write-only bits read as 1, and nothing else happens (accuracy.md, "I/O
registers"). All 12 `dmg_sound` single tests run to the end and report a failure, for example
`01-registers`: "NR52 write/read, Failed #3"; `02-len ctr`: "Length becoming 0 should clear
status, Failed #2". M5 replaces the plain memory with an `Apu` class in the core, owned by the
bus like the timer and the PPU.

## 2. How the tests report

The `dmg_sound` ROMs print nothing over the serial port, so the headless runner sees no verdict
today. Each one writes its result to cartridge RAM instead (all 12 are type `03`, MBC1 + RAM +
battery, with 8 KiB of RAM), checked by running them here:

| Address | Contents |
|---|---|
| `A000` | status: `80` while running, then `00` for a pass or the failing test's number |
| `A001`-`A003` | the signature `DE B0 61`, so a random RAM byte is not read as a verdict |
| `A004`- | the text the ROM also shows on screen, zero-terminated |

**Plan:** the headless runner and `rom_tests` accept this as a second kind of verdict next to the
serial one: once the signature is present and the status is not `80`, the run is over, the text
is printed, and the exit status is 0 for `00` and 1 otherwise. The RAM is read straight from the
cartridge (a new `GameBoy::cartridge_ram()`), not through the bus, because a ROM may disable RAM
after writing its result.

The combined `dmg_sound.gb` never stops (it repeats the tests); c-sp's how-to gives 36 emulated
seconds and a reference screenshot made with SameBoy. It is checked like dmg-acid2: run 36
seconds, compare `dmg_sound-dmg.png` in shades.

## 3. Registers

| Address | Name | Read back as (unused and write-only bits read 1) | Notes |
|---|---|---|---|
| `FF10` | NR10 sweep | `value \| 80` | channel 1 only |
| `FF11` | NR11 duty, length | `value \| 3F` | length is write-only |
| `FF12` | NR12 envelope | `value` | top 5 bits 0 turns the DAC off, and the channel with it |
| `FF13` | NR13 period low | `FF` | write-only |
| `FF14` | NR14 trigger, length enable, period high | `value \| BF` | only length enable reads back |
| `FF16`-`FF19` | NR21-NR24 | as NR11-NR14 | channel 2, no sweep |
| `FF1A` | NR30 DAC on | `value \| 7F` | bit 7 alone is the DAC |
| `FF1B` | NR31 length | `FF` | 8-bit length |
| `FF1C` | NR32 volume | `value \| 9F` | 0, 100 %, 50 %, 25 % |
| `FF1D`, `FF1E` | NR33, NR34 | `FF`, `value \| BF` | |
| `FF20`-`FF23` | NR41-NR44 | `FF`, `value`, `value`, `value \| BF` | noise |
| `FF24` | NR50 master volume | `value` | the VIN bits are stored but nothing is connected |
| `FF25` | NR51 panning | `value` | |
| `FF26` | NR52 | bit 7 power, bits 0-3 channel on (read-only), `\| 70` | |
| `FF15`, `FF1F`, `FF27`-`FF2F` | | `FF` | no register |
| `FF30`-`FF3F` | wave RAM | see section 6 | |

**Power off** (writing 0 to NR52 bit 7): every register from `FF10` to `FF25` is cleared and
ignores writes until power is back, except that on the DMG the length parts of NR11, NR21, NR31
and NR41 can still be written **(test: `08-len ctr during power`, `11-regs after power`)**. Wave
RAM is not touched. Powering on resets the frame sequencer so that its next step is step 0.

## 4. Frame sequencer, length, envelope, sweep

**Frame sequencer.** 512 Hz, driven by the falling edge of DIV bit 4 (bit 12 of the timer's
16-bit counter), so a write to DIV can clock it early. Eight steps: length on 0, 2, 4, 6; sweep
on 2 and 6; envelope on 7. The timer already owns that counter (`core/timer.h`); the APU gets
told about the falling edge rather than keeping a second counter **(choice)**, so the two can
never drift.

**Length** (64 steps for channels 1, 2, 4; 256 for the wave channel). Reaching 0 turns the
channel off. Two quirks the tests check **(test: `02-len ctr`, `03-trigger`)**:

- Enabling length in NRx4 during a frame-sequencer step that does not clock length clocks it
  once extra, if the length is not already 0; if that makes it 0 and the write is not a trigger,
  the channel turns off.
- A trigger with length 0 loads the maximum (64 or 256), and if length is enabled and the next
  step does not clock length, the maximum minus one.

**Envelope.** Period 0 means no change; otherwise the volume moves by one every period, between
0 and 15, then stops. A write to NRx2 with the top five bits 0 turns the DAC and the channel off.

**Sweep** (channel 1) **(test: `04-sweep`, `05-sweep details`, `06-overflow on trigger`)**:

- A shadow copy of the period, an enabled flag and a timer, all set on trigger. A sweep period
  of 0 is treated as 8 for the timer.
- On trigger with a non-zero shift, the new period is calculated once and, above 2047, turns the
  channel off; it is not written back.
- Each sweep clock with a non-zero period calculates, checks for overflow, writes back if the
  shift is non-zero, and calculates and checks again.
- Clearing the negate bit after a calculation has used negate since the last trigger turns the
  channel off.

## 5. The four channels and the mix

- **Square** (channels 1 and 2): duty 12.5 %, 25 %, 50 %, 75 %; the period timer counts
  `(2048 - period) * 4` clock ticks per duty step.
- **Wave** (channel 3): 32 four-bit samples from wave RAM, shifted by the volume code; the timer
  counts `(2048 - period) * 2` ticks per sample.
- **Noise** (channel 4): a 15-bit linear feedback shift register, or 7-bit in short mode; divisor
  and shift from NR43.
- Each channel's DAC turns its 0-15 digital value into an analog level; NR51 routes each channel
  to left, right or both; NR50 scales each side by 1-8.

The tests above read registers and timing, not the sound itself. What comes out is checked by
unit tests on each channel (duty patterns, the LFSR sequence, envelope steps, the sweep
arithmetic) and by a tone test: a small program plays channel 1 at a known period, and counting
zero crossings in one emulated second of output must give the expected frequency.

## 6. Wave RAM while the channel plays (the hard part)

On the DMG, while channel 3 is on, the CPU reaches wave RAM only in the same clock tick the
channel is reading it: a read then returns the byte being played, and at any other moment `FF`;
a write lands on that byte, and at any other moment is lost. Triggering channel 3 while it is
already reading corrupts the first bytes of wave RAM **(test: `09-wave read while on`,
`10-wave trigger while on`, `12-wave write while on`)**. These three need the wave channel's
timing to the 2 MHz tick, measured against the CPU's own memory accesses. They are the riskiest
part of M5 and are planned last (section 9).

## 7. Sound out of the window

**In the core.** The APU mixes to stereo at a fixed output rate, 48 000 samples per second
**(choice)**, by averaging the analog level over each sample's clock ticks (a box filter), then a
high-pass filter like the capacitor on the real output, so a DAC switched on or off does not
leave a constant offset. Samples go into a buffer the frontend drains with
`GameBoy::take_audio()`; the core stays free of I/O. Band-limited synthesis (as in `blip_buf`)
would remove more aliasing on high notes; it is not planned for M5 **(choice)**.

**In the window.** SDL's audio queue (`SDL_QueueAudio`). Two ways to keep sound and picture in
step:

1. **Audio sets the pace.** Run frames while less than about 3 frames of sound (50 ms) are
   queued, otherwise sleep a millisecond. The game runs at the sound card's clock; the picture
   follows. This replaces the wall-clock pacing of ADR-016.
2. **The wall clock keeps the pace (ADR-016 unchanged).** The emulator makes exactly 48 000
   samples per emulated second, but the sound card's clock and the performance counter drift
   apart, so the queue slowly grows or empties; samples would have to be dropped or repeated, or
   the output rate nudged, to hold it.

**Proposed: 1**, with the current wall-clock pacing kept for when no audio device opens, with
`--mute`, and in the CI app-smoke job (SDL's dummy audio driver), so the save-on-signal tests
are not changed by audio timing. If chosen, it becomes a new ADR that supersedes ADR-016.

## 8. Save states

A new `APU ` section after `JOYP`: every register, the frame-sequencer step, and each channel's
timers, length, envelope, sweep and LFSR state, written field by field like the rest (ADR-020).
The output buffer and the high-pass filter's memory are left out, as the serial text is. Adding a
section is a layout change, so the format version goes to 2 and `v0.4.0` states are refused with
the reason, as ADR-020 says. The determinism tests also compare the samples produced after a
load with those of the original run.

## 9. Order of work and exit

1. **Registers and power**: the `Apu` class with the register table, NR52 power, and the
   `A000` verdict in the headless runner and `rom_tests`. Target: `01-registers`,
   `11-regs after power`.
2. **Frame sequencer and length**: `02-len ctr`, `03-trigger`, `07-len sweep period sync`,
   `08-len ctr during power`.
3. **Sweep**: `04-sweep`, `05-sweep details`, `06-overflow on trigger`.
4. **The channels and the mix**, with their unit tests and the tone test.
5. **Sound in the window**: SDL audio, the pacing chosen in review, `--mute`.
6. **Wave RAM while playing**: `09`, `10`, `12`.
7. **Save states**: the `APU ` section, format version 2, samples in the determinism tests.
8. The combined `dmg_sound` screenshot.

Each step is its own commit with its tests; accuracy.md is updated as it lands.

**M5 exit (proposed):** all 12 `dmg_sound` single tests pass by their `A000` verdict and the
combined ROM matches its screenshot, in CI; the tone test passes; the APU is in save states with
the determinism tests covering sound; and by hand, Libbet's music plays in the window without
crackling or drift over a few minutes.

## 10. Questions for review

1. **Pacing:** audio sets the pace (section 7, option 1, superseding ADR-016), or keep the wall
   clock and correct the queue?
2. **Output quality:** is a box filter plus high-pass enough for M5, with band-limited synthesis
   left for later?
3. **Wave RAM quirks** (`09`, `10`, `12`): part of the M5 exit as proposed, or allowed as a
   documented gap if they need more than a weekend?
4. **Save-state version 2:** confirm that `v0.4.0` states are simply refused, with no converter.
5. **The `A000` verdict** in the headless runner: acceptable as a second verdict source next to
   serial output, or keep it in `rom_tests` only?
