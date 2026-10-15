#!/usr/bin/env python3
"""App smoke test: the window writes the save file when stopped by a signal.

Runs gameboy-emu in window mode with SDL's dummy video and audio drivers (no
real window), on a tiny ROM that writes a marker byte to its battery RAM, waits
about 0.1 s, then fills all 8 KiB with a known pattern. Sends SIGTERM or SIGINT
and checks that the app exits with status 0 and that game.sav holds exactly the
pattern (docs/cartridge-notes.md, section 6).

The timing makes the write on exit the only way to pass. The window writes the
save at most once a second while RAM changes: the first write catches the
marker, and the next periodic one cannot come until a second later. The signal
is sent 0.5 s after the first write, when the fill (done about 0.17 s after the
marker) is complete, so the full pattern can only reach the file through the
write on exit. The emulator has to run at least at about a third of real speed
for that margin to hold.

Usage: app_smoke.py PATH_TO_GAMEBOY_EMU TERM|INT
"""

import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TIMEOUT_SECONDS = 10
RAM_SIZE = 0x2000


MARKER_OFFSET = 0x0001
MARKER = 0xEE  # differs from the pattern's 0x01 at that offset


def pattern_rom() -> bytes:
    """MBC1 + RAM + battery, 8 KiB RAM. Writes MARKER at A001, waits about
    0.1 s (15000 turns of a 28-tick loop), then fills A000-BFFF with the low
    byte of each address, then loops forever."""
    program = bytes([
        0x3E, 0x0A,        # 0100: LD A, 0x0A
        0xEA, 0x00, 0x00,  # 0102: LD [0x0000], A   ; enable RAM
        0x3E, MARKER,      # 0105: LD A, MARKER
        0xEA, 0x01, 0xA0,  # 0107: LD [0xA001], A
        0x01, 0x98, 0x3A,  # 010A: LD BC, 15000
        0x0B,              # 010D: DEC BC
        0x78,              # 010E: LD A, B
        0xB1,              # 010F: OR C
        0x20, 0xFB,        # 0110: JR NZ, 0x010D
        0x21, 0x00, 0xA0,  # 0112: LD HL, 0xA000
        0x7D,              # 0115: LD A, L
        0x22,              # 0116: LD [HL+], A
        0x7C,              # 0117: LD A, H
        0xFE, 0xC0,        # 0118: CP 0xC0
        0x20, 0xF9,        # 011A: JR NZ, 0x0115
        0x18, 0xFE,        # 011C: JR 0x011C
    ])
    rom = bytearray(0x8000)
    rom[0x100:0x100 + len(program)] = program
    rom[0x147] = 0x03  # MBC1 + RAM + battery
    rom[0x149] = 0x02  # 8 KiB RAM
    return bytes(rom)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[2] not in ("TERM", "INT"):
        print(__doc__)
        sys.exit(64)
    app = sys.argv[1]
    signal_to_send = signal.SIGTERM if sys.argv[2] == "TERM" else signal.SIGINT
    expected = bytes(i & 0xFF for i in range(RAM_SIZE))

    with tempfile.TemporaryDirectory() as directory:
        rom_path = Path(directory) / "game.gb"
        save_path = Path(directory) / "game.sav"
        rom_path.write_bytes(pattern_rom())
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        env.pop("SDL_NO_SIGNAL_HANDLERS", None)
        deadline = time.monotonic() + TIMEOUT_SECONDS
        app_process = subprocess.Popen([app, str(rom_path)], env=env)

        # The first save is written in the first frames, with only the marker.
        while not save_path.exists():
            if app_process.poll() is not None:
                fail(f"the app exited early with status {app_process.returncode}")
            if time.monotonic() > deadline:
                app_process.kill()
                fail("no save file appeared")
            time.sleep(0.02)
        first_save = save_path.read_bytes()
        marker_only = bytearray(RAM_SIZE)
        marker_only[MARKER_OFFSET] = MARKER
        if first_save != bytes(marker_only):
            app_process.kill()
            fail("the first save is not the marker alone, so the timing this test relies on "
                 "does not hold")
        time.sleep(0.5)

        app_process.send_signal(signal_to_send)
        try:
            status = app_process.wait(timeout=max(0.1, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            app_process.kill()
            fail(f"the app did not exit within {TIMEOUT_SECONDS} s of starting")

        if status != 0:
            fail(f"exit status {status}, expected 0")
        saved = save_path.read_bytes()
        if len(saved) != RAM_SIZE:
            fail(f"game.sav is {len(saved)} bytes, expected {RAM_SIZE}")
        if saved != expected:
            first = next(i for i in range(RAM_SIZE) if saved[i] != expected[i])
            fail(f"game.sav differs from the RAM the ROM wrote at offset {first:#06x}: "
                 f"{saved[first]:#04x} instead of {expected[first]:#04x}")
        if (Path(directory) / "game.sav.tmp").exists():
            fail("game.sav.tmp was left behind")
    print(f"PASS: SIG{sys.argv[2]} wrote the save and exited with status 0")


if __name__ == "__main__":
    main()
