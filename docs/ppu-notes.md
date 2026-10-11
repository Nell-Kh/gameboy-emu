# PPU notes

What the M3 PPU must do, written before the code. Sources:

- Pan Docs: [Rendering](https://gbdev.io/pandocs/Rendering.html),
  [STAT](https://gbdev.io/pandocs/STAT.html), [LCDC](https://gbdev.io/pandocs/LCDC.html),
  [Accessing VRAM and OAM](https://gbdev.io/pandocs/Accessing_VRAM_and_OAM.html),
  [Interrupt Sources](https://gbdev.io/pandocs/Interrupt_Sources.html),
  [OAM](https://gbdev.io/pandocs/OAM.html), [Tile Data](https://gbdev.io/pandocs/Tile_Data.html),
  [Scrolling](https://gbdev.io/pandocs/Scrolling.html), [Window](https://gbdev.io/pandocs/Window.html),
  [Palettes](https://gbdev.io/pandocs/Palettes.html).
- The Mooneye `acceptance/ppu/` test sources ([mooneye-test-suite](https://github.com/Gekkio/mooneye-test-suite), commit `31510e1`).
  The ROMs we run are built from commit `8d742b9` (c-sp `v7.0`, ADR-011); between the two, the
  `ppu/` sources differ only in one comment.
- The [dmg-acid2](https://github.com/mattcurrie/dmg-acid2) README.

A statement marked **(test)** comes from a Mooneye test's expectations rather than from the Pan
Docs. A statement marked **(unconfirmed)** comes from neither and must be settled by a test before
it is relied on.

Units: a **dot** is one clock tick (T-cycle, 4.194304 MHz). The CPU's machine cycle is 4 dots.

## 1. Registers

| Address | Name | Access | Meaning |
|---|---|---|---|
| `FF40` | LCDC | R/W | Control, bit by bit below |
| `FF41` | STAT | mixed | Status and STAT interrupt selects, below |
| `FF42` | SCY | R/W | Background scroll Y |
| `FF43` | SCX | R/W | Background scroll X |
| `FF44` | LY | R | Current line, 0-153 |
| `FF45` | LYC | R/W | Line to compare with LY |
| `FF46` | DMA | R/W | OAM DMA (done in M2) |
| `FF47` | BGP | R/W | Background and window palette |
| `FF48` | OBP0 | R/W | Object palette 0 |
| `FF49` | OBP1 | R/W | Object palette 1 |
| `FF4A` | WY | R/W | Window Y position |
| `FF4B` | WX | R/W | Window X position plus 7 |

**LCDC**

| Bit | Meaning |
|---|---|
| 7 | LCD and PPU on. Turning it off gives the CPU full access to VRAM and OAM; the screen shows white (lighter than shade 0). Only to be turned off during VBlank. |
| 6 | Window tile map: 0 = `9800-9BFF`, 1 = `9C00-9FFF` |
| 5 | Window on. Ignored while bit 0 is 0. |
| 4 | Background and window tile data: 0 = `8800-97FF` (signed index, base `9000`), 1 = `8000-8FFF` (unsigned). Objects always use `8000`. |
| 3 | Background tile map: 0 = `9800-9BFF`, 1 = `9C00-9FFF` |
| 2 | Object size: 0 = 8x8, 1 = 8x16 |
| 1 | Objects on |
| 0 | Background and window on. When 0 they are blank (white) and the window is off; objects still draw. |

When the LCD is switched on, the PPU starts at once but the first frame is not shown.

**STAT**

| Bit | Access | Meaning |
|---|---|---|
| 7 | - | Does not exist, reads 1 (M2, Mooneye `unused_hwio`) |
| 6 | R/W | Select LYC = LY for the STAT interrupt |
| 5 | R/W | Select mode 2 for the STAT interrupt |
| 4 | R/W | Select mode 1 for the STAT interrupt |
| 3 | R/W | Select mode 0 for the STAT interrupt |
| 2 | R | LYC = LY, updated continuously |
| 1-0 | R | Current mode; 0 while the LCD is off |

DMG quirk: writing STAT (even `00`) during mode 0, 1, 2 or while LYC = LY can raise a STAT
interrupt, because the hardware acts as if `FF` were written for one machine cycle first. Two
games depend on it (*Road Rash*, *Xerd no Densetsu*). Not targeted in M3; listed in accuracy.md.

## 2. Frame and scanline timing

- One scanline is **456 dots**. One frame is **154 lines = 70224 dots** (about 59.7 frames per
  second).
- Lines 0-143 are drawn; lines 144-153 are vertical blank.
- Each drawn line goes through three modes, in order:

| Mode | What the PPU does | Length (dots) | CPU can access |
|---|---|---|---|
| 2 | OAM scan: picks the objects on this line | 80 | VRAM |
| 3 | Drawing: sends 160 pixels to the LCD | 172 to 289 | neither |
| 0 | HBlank: waits for the end of the line | 376 minus mode 3 | VRAM and OAM |
| 1 | VBlank: lines 144-153 | 4560 (10 lines) | VRAM and OAM |

**Length of mode 3.** Source: Pan Docs, [Rendering, section "Mode 3
length"](https://gbdev.io/pandocs/Rendering.html#mode-3-length); the object part is what
`intr_2_mode0_timing_sprites` measures. 172 dots minimum (160 pixels plus 12 dots of start-up
fetches), plus:

- `SCX % 8` dots at the start of the line.
- 6 dots if the window starts on this line.
- For each object on the line, 6 to 11 dots: a flat 6, plus `max(0, n - 2)`, where `n` is the
  number of pixels of the background tile under the object that lie strictly to the right of the
  object's leftmost pixel. Only the first object on a given background tile pays the second part.
  An object at OAM X = 0 always costs 11.

Whatever mode 3 gains, mode 0 loses: the line stays 456 dots.

**First line after the LCD is switched on (test).** From `lcdon_timing-GS`: line 0 starts in mode 0
(not mode 2) and goes straight to mode 3, and it runs 2 dots late; lines 1 and 2 have normal
timing. The expected LY, STAT, OAM and VRAM values per cycle are tabulated in that test.

**Line 153.** Other emulators report that on line 153 LY reads 0 from dot 4 on, and LYC is
compared against 0 for the rest of that line. This is not on the Pan Docs pages above. Checked
against the M3 targets: **not needed by any of them.** `intr_1_2_timing-GS` measures from the
mode 1 STAT interrupt at line 144 to the mode 2 STAT interrupt at line 0 and never reads LY or
uses LYC near the end of the frame; `stat_lyc_onoff` compares LYC with LY = 144 (`90`) and with
LY = 0 after switching the LCD on, never with line 153. No other `ppu/` test mentions line 153.
It is left out and listed in accuracy.md.

**While the LCD is off:** LY reads 0 and the STAT mode reads 0. The LYC = LY bit keeps its last
value and is not updated, even if LYC changes; switching the LCD back on resumes the comparison
at LY = 0 **(test,** `stat_lyc_onoff`**)**.

## 3. Interrupts

- **VBlank** (`0x40`): requested when line 144 starts. Already done in M2 (ADR-013).
- **STAT** (`0x48`): the four conditions (mode 0, mode 1, mode 2, LYC = LY), each ANDed with its
  select bit in STAT, are ORed into one internal line. The interrupt is requested on a **rising
  edge** of that line only. So:
  - two enabled conditions that follow each other without a gap (mode 0 then mode 1) give one
    interrupt, not two;
  - with LYC = LY held true across mode 3, the line never falls and no further STAT interrupt
    comes (`stat_irq_blocking`);
  - enabling a select bit while its condition is already true does raise the interrupt
    (`stat_irq_blocking` round 1: selecting mode 1 during VBlank).
- **Mode 2 select at line 144 (test):** with STAT bit 5 set, a STAT interrupt is also requested
  when VBlank starts, at the same time as the VBlank interrupt (`vblank_stat_intr-GS`).

## 4. CPU access to VRAM and OAM

| PPU state | VRAM `8000-9FFF` | OAM `FE00-FE9F` |
|---|---|---|
| LCD off | yes | yes |
| Mode 2 | yes | **no** |
| Mode 3 | **no** | **no** |
| Mode 0 | yes | yes |
| Mode 1 | yes | yes |

Blocked reads return `FF`; blocked writes are ignored. During OAM DMA the DMA has OAM, the CPU is
already blocked (M2) and the PPU itself reads `FF` from OAM. The exact dot at which access opens
and closes around mode changes is what `intr_2_oam_ok_timing`, `lcdon_timing-GS` and
`lcdon_write_timing-GS` measure.

Not targeted in M3: the OAM corruption bug (16-bit INC/DEC with a register pointing into OAM
during mode 2 corrupts OAM on DMG).

## 5. What is drawn

**Tiles.** 8x8 pixels, 2 bits per pixel, 16 bytes. Each row is 2 bytes: the first holds bit 0 of
each pixel's colour index, the second bit 1; bit 7 is the leftmost pixel. Tile data is
`8000-97FF` in three blocks of 128 tiles; with LCDC bit 4 = 0, index `00-7F` is in `9000-97FF` and
`80-FF` in `8800-8FFF`.

**Background.** A 32x32 tile map (256x256 pixels) at `9800` or `9C00`. The screen shows the
160x144 window into it starting at (SCX, SCY), wrapping at 256 in both directions. SCX and SCY are
read again for each tile fetched, except `SCX % 8`, which is read once at the start of the line.

**Window.** A second background layer, drawn from its own tile map with no scrolling, whose
top-left pixel is at screen (WX - 7, WY).
- It is shown when WX is 0-166 and WY is 0-143.
- From the first line on which LY = WY, the "Y condition" holds for the rest of the frame; it is
  reset at VBlank.
- The window has its own line counter. It only advances on lines where the window was actually
  drawn, so hiding the window for some lines and showing it again continues where it stopped
  (dmg-acid2 tests this: "eye displayed instead of right chin").
- Quirks: WX = 0 shifts the window left by `SCX % 8`; WX = 166 on DMG spans the screen shifted
  down a line. Not targeted in M3 beyond what dmg-acid2 needs.

**Objects.** 40 entries of 4 bytes in OAM: Y + 16, X + 8, tile index, attributes.
- Attributes: bit 7 = background colours 1-3 drawn over the object, bit 6 = Y flip, bit 5 = X
  flip, bit 4 = palette (OBP0 or OBP1). Bits 3-0 are CGB only.
- In 8x16 mode bit 0 of the tile index is ignored: the top tile is `NN & FE`, the bottom
  `NN | 01`.
- OAM scan takes the first 10 objects in OAM order whose Y range covers LY. Objects off the left
  or right edge still count; dmg-acid2's "Hello World" checks the limit.
- Between overlapping objects, only opaque pixels compete: smaller X wins, then earlier in OAM.
  The winning pixel's priority bit is checked only after that, so a high-priority object with
  bit 7 set can hide a lower-priority one even where the background shows through.
- Colour index 0 is transparent.

**Colours.** BGP, OBP0 and OBP1 map colour index 0-3 to a shade 0-3 (bits 1-0 for index 0, up to
bits 7-6 for index 3; OBP bits 1-0 are unused). Shade 0 is white, 3 black. Screenshots compared
with the reference images use `FF`, `AA`, `55`, `00` for shades 0-3 (dmg-acid2 README).

## 6. Design for M3

- **Scanline renderer** (ADR-014). The whole line is drawn at one moment (the start of mode 3),
  using the registers as they are then. The brief chose this; dmg-acid2 is built for it ("does
  not perform register writes during mode 3").
- **STAT is one signal.** The four conditions are ORed into a single internal line and the
  interrupt is requested on its rising edge only, as in section 3. Nothing requests a STAT
  interrupt directly.
- **Mode timing is still per dot.** The PPU keeps counting dots inside each `Bus::tick(4)`, and
  mode 3's length comes from the formula in section 2, so STAT, interrupts and the access rules
  move at the right dot even though pixels are produced all at once.
- **What this gives up:** register writes during mode 3 (mid-line scroll, palette or window
  changes) take effect from the next line, not mid-line. Pixel FIFO behaviour is listed as a
  simplification in accuracy.md.
- **Frame output:** a 160x144 buffer of shades 0-3, exposed by `GameBoy`. The SDL window and the
  PNG dump both read from it.

## 7. Targets

**Exit:** dmg-acid2 matches `dmg-acid2-dmg.png` pixel for pixel in CI. The ROM ends with
`LD B, B`; the frame on screen at that point is compared. The ROM and reference image come from
the c-sp collection already downloaded (ADR-011).

**Mooneye `acceptance/ppu/`**, all twelve fail today:

| Test | Needs | M3 target |
|---|---|---|
| `intr_2_0_timing` | Mode 2 and mode 0 STAT interrupts, mode 3 length without objects | yes |
| `intr_2_mode0_timing` | Same, read through the STAT mode bits | yes |
| `intr_2_mode3_timing` | Exact dot mode 3 starts after mode 2 | yes |
| `intr_2_oam_ok_timing` | Exact dot OAM becomes readable after mode 2 | yes |
| `intr_1_2_timing-GS` | Mode 1 to mode 2 timing (line 153 to line 0) | yes |
| `vblank_stat_intr-GS` | Mode 2 select also fires at line 144 | yes |
| `stat_irq_blocking` | Rising-edge STAT line | yes |
| `stat_lyc_onoff` | LYC bit kept while the LCD is off | yes |
| `hblank_ly_scx_timing-GS` | `SCX % 8` penalty in mode 3 | yes |
| `intr_2_mode0_timing_sprites` | Object penalties in mode 3, all cases | stretch: the formula is in section 2, but the test covers many object layouts |
| `lcdon_timing-GS` | The odd first line after LCD on, to the dot | stretch |
| `lcdon_write_timing-GS` | Same, for writes | stretch |

**Tetris playable** needs the joypad (`FF00`) and the SDL window, also in M3. SDL2 is linked into
`app` only; `core` stays without dependencies. CI builds `app` on Linux and macOS but never opens
the window: `--headless` is the CI path.

**M3 exit:** dmg-acid2 pixel-identical in CI, the nine "yes" tests above green in CI, Tetris
playable in the SDL window.

**Order of work.** Timing first, pixels last, because timing bugs are invisible once there is a
picture to look at:

1. Mode and dot state machine, the STAT line, the VRAM and OAM access rules.
2. The nine "yes" Mooneye tests green.
3. Background, then window, then objects.
4. dmg-acid2 pixel-identical.
5. Joypad and the SDL window; Tetris.

## 8. Decisions (settled in review)

1. **Reading the reference PNG: `stb_image`, test code only.** It goes under
   `tests/third_party/` with its licence file, is never linked into `core` or `app`, and its
   version is pinned in an ADR. The test converts the reference's four colours to shades 0-3 once
   and compares shades against the framebuffer, so it never depends on the palette the SDL window
   uses. On a mismatch it writes the actual frame as a PNG next to the reference and prints the
   first differing (x, y) with both values.
2. **Line 153: left out**, because no M3 target needs it (section 2). Listed in accuracy.md.
