# Architecture

peasoup is two layers: `core/`, the emulator as a library with no dependencies and no I/O, and
`app/`, the program around it. Only `app/sdl_frontend.cpp` knows about SDL (ADR-002).

```mermaid
flowchart TB
    subgraph app["app/ (the program)"]
        main["main.cpp<br/>arguments, --headless runner"]
        window["sdl_frontend.cpp<br/>window, keys, audio queue, pacing"]
        frontend["frontend library (no SDL)<br/>battery saves, state slot, PNG, RAM verdict"]
    end

    subgraph core["core/ (the emulator, no I/O)"]
        gb["GameBoy<br/>load_rom, run_frame, reset, save states"]
        cpu["Cpu<br/>SM83 instructions, interrupts, HALT"]
        bus["Bus<br/>memory map, work and high RAM, IF / IE"]
        ppu["Ppu<br/>modes 0-3, the frame"]
        apu["Apu<br/>4 channels, frame sequencer, mixer"]
        timer["Timer<br/>DIV, TIMA"]
        dma["OamDma"]
        serial["Serial"]
        joypad["Joypad"]
        cart["Cartridge<br/>none, MBC1, MBC3 + Rtc, MBC5"]
    end

    main --> gb
    main --> window
    window --> gb
    window --> frontend
    frontend --> gb
    gb --> cpu
    gb --> bus
    cpu -- "read / write, one machine cycle each,<br/>then tick(4)" --> bus
    bus -- tick --> ppu
    bus -- tick --> apu
    bus -- tick --> timer
    bus -- tick --> dma
    bus -- tick --> serial
    bus -- tick --> cart
    bus -- buttons --> joypad
    timer -- "DIV bit 4 falls: frame-sequencer step" --> apu
    ppu -- "VBlank, STAT" --> bus
    timer -- "timer interrupt" --> bus
    serial -- "serial interrupt" --> bus
    joypad -- "joypad interrupt" --> bus
```

## How time moves

The CPU drives everything. Each memory access an instruction makes is one machine cycle: the CPU
reads or writes through the `Bus`, which then ticks every component by 4 clock ticks (ADR-005).
So the PPU, timer, APU, OAM DMA, serial port and cartridge clock all see time pass between the
accesses of a single instruction, which is what the Blargg `mem_timing` and Mooneye timing tests
measure. The frame sequencer of the APU is clocked by the falling edge of DIV bit 4, as on
hardware, so a write to DIV can step it (`docs/apu-notes.md`, section 4).

Interrupt sources set bits in IF on the bus; the CPU checks IF and IE after each opcode fetch and
dispatches (ADR-009).

## The program around it

- `main.cpp` parses the arguments and either runs a ROM headless (its serial output, or the
  verdict `dmg_sound` writes to cartridge RAM) or opens the window. The Mooneye breakpoint and the
  screenshots are checked by the test programs in `tests/`, not by the app.
- `sdl_frontend.cpp` runs one frame per loop: input, `GameBoy::run_frame()`, the picture, the
  sound. The audio queue sets the pace (ADR-025); with `--mute`, and while Tab fast-forwards,
  the wall clock does (ADR-016, ADR-027).
- The `frontend` library holds what the unit tests need without SDL: battery saves with their
  clock footer (ADR-018, ADR-019), the save-state slot, the PNG writer for screenshots, and the
  reader for verdicts written to cartridge RAM.

## Where to look

| To understand | Read |
|---|---|
| Why a design is the way it is | [DECISIONS.md](DECISIONS.md), the ADRs |
| What is verified and what is not | [accuracy.md](accuracy.md) |
| The PPU | [ppu-notes.md](ppu-notes.md) |
| Cartridges, battery saves, save states | [cartridge-notes.md](cartridge-notes.md) |
| Sound | [apu-notes.md](apu-notes.md) |
