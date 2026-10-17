#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace frontend {

// The verdict some Blargg test ROMs (dmg_sound, cgb_sound and others) write to
// cartridge RAM instead of the serial port (docs/apu-notes.md, section 2):
//
//   0xA000          status: 0x80 while running, then 0x00 for a pass or the
//                   number of the failing test
//   0xA001-0xA003   the signature DE B0 61
//   0xA004-         the text the ROM shows on screen, zero-terminated
struct RamVerdict {
    std::uint8_t status = 0;
    std::string text;

    [[nodiscard]] bool passed() const noexcept {
        return status == 0;
    }
};

// Reads the verdict from the whole cartridge RAM. Returns nothing while the
// test is still running, and nothing at all unless the signature is there,
// so a random RAM byte is never taken for a verdict.
[[nodiscard]] std::optional<RamVerdict> read_ram_verdict(std::span<const std::uint8_t> ram);

}  // namespace frontend
