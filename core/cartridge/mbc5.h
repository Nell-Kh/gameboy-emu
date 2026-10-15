#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.h"

namespace core {

// The MBC5 mapper: up to 8 MiB of ROM (512 banks) and 128 KiB of RAM (16
// banks), switched through write-only registers in the ROM address range
// (Pan Docs, MBC5; docs/cartridge-notes.md, section 2).
//
//   0x0000-0x1FFF  RAM enable: 0x_A in the low nibble enables it
//   0x2000-0x2FFF  ROM bank, bits 0-7
//   0x3000-0x3FFF  ROM bank, bit 8 (only bit 0 of the value counts)
//   0x4000-0x5FFF  RAM bank, bits 0-3
//   0x6000-0x7FFF  nothing
//
// Unlike MBC1 and MBC3, bank 0 can be mapped at 0x4000-0x7FFF. Bank numbers
// wrap at the real ROM and RAM sizes. On a cartridge with a rumble motor, bit
// 3 of the RAM bank register drives the motor instead of the RAM chip, so only
// bits 0-2 select a bank; the motor itself is not emulated.
class Mbc5 final : public Cartridge {
public:
    Mbc5(std::span<const std::uint8_t> rom, std::size_t ram_size, bool rumble,
         bool battery = false);

    [[nodiscard]] std::uint8_t read_rom(std::uint16_t address) const noexcept override;
    void write_rom(std::uint16_t address, std::uint8_t value) noexcept override;
    [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept override;
    void write_ram(std::uint16_t address, std::uint8_t value) noexcept override;

    // The banks currently mapped, for tests and the debugger.
    [[nodiscard]] std::size_t rom_bank() const noexcept;  // at 0x4000-0x7FFF
    [[nodiscard]] std::size_t ram_bank() const noexcept;

private:
    [[nodiscard]] bool ram_accessible() const noexcept;
    [[nodiscard]] std::size_t ram_offset(std::uint16_t address) const noexcept;

    std::vector<std::uint8_t> rom_;
    std::size_t rom_bank_mask_;
    std::size_t ram_bank_mask_;
    std::uint8_t ram_bank_bits_;  // 0x0F, or 0x07 with a rumble motor

    bool ram_enabled_ = false;
    std::uint16_t rom_bank_ = 0x001;
    std::uint8_t ram_bank_ = 0x00;
};

}  // namespace core
