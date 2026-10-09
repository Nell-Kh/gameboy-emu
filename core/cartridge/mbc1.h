#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.h"

namespace core {

// The MBC1 mapper: up to 2 MiB of ROM and 32 KiB of RAM, switched through
// four write-only registers in the ROM address range.
//
//   0x0000-0x1FFF  RAMG   RAM enable: 0x_A in the low nibble enables it
//   0x2000-0x3FFF  BANK1  5-bit ROM bank for 0x4000-0x7FFF; 0 is read as 1
//   0x4000-0x5FFF  BANK2  2 more bits: ROM bank bits 5-6, or the RAM bank
//   0x6000-0x7FFF  MODE   0: BANK2 only affects 0x4000-0x7FFF
//                         1: BANK2 also selects the bank at 0x0000-0x3FFF
//                            and the RAM bank
//
// Because BANK1 can never be 0, banks 0x20, 0x40 and 0x60 cannot be mapped
// at 0x4000: asking for them gives 0x21, 0x41 and 0x61. In mode 1 they appear
// at 0x0000 instead.
//
// Bank numbers wrap at the real ROM and RAM sizes, as the unused address
// lines are not connected. MBC1 multicarts (MBC1M), which wire BANK1 to only
// four lines, are not supported.
class Mbc1 final : public Cartridge {
public:
    Mbc1(std::span<const std::uint8_t> rom, std::size_t ram_size);

    [[nodiscard]] std::uint8_t read_rom(std::uint16_t address) const noexcept override;
    void write_rom(std::uint16_t address, std::uint8_t value) noexcept override;
    [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept override;
    void write_ram(std::uint16_t address, std::uint8_t value) noexcept override;

    // The banks currently mapped, for tests and the debugger.
    [[nodiscard]] std::size_t rom_bank_low() const noexcept;   // at 0x0000-0x3FFF
    [[nodiscard]] std::size_t rom_bank_high() const noexcept;  // at 0x4000-0x7FFF
    [[nodiscard]] std::size_t ram_bank() const noexcept;

private:
    [[nodiscard]] bool ram_accessible() const noexcept;
    [[nodiscard]] std::size_t ram_offset(std::uint16_t address) const noexcept;

    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> ram_;
    std::size_t rom_bank_mask_;
    std::size_t ram_bank_mask_;

    bool ram_enabled_ = false;
    std::uint8_t bank1_ = 0x01;
    std::uint8_t bank2_ = 0x00;
    bool mode1_ = false;
};

}  // namespace core
