#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.h"
#include "core/cartridge/rtc.h"

namespace core {

// The MBC3 mapper: up to 2 MiB of ROM and 32 KiB of RAM, switched through
// write-only registers in the ROM address range (Pan Docs, MBC3;
// docs/cartridge-notes.md, section 3).
//
//   0x0000-0x1FFF  RAM (and clock) enable: 0x_A in the low nibble enables it
//   0x2000-0x3FFF  ROM bank for 0x4000-0x7FFF; 0 is read as 1
//   0x4000-0x5FFF  RAM bank 0x00-0x07, or a clock register 0x08-0x0C
//   0x6000-0x7FFF  latch the clock (0x00 then 0x01)
//
// The ROM bank register is 7 bits wide, as on MBC3, for ROMs of up to 2 MiB.
// For larger ROMs it is 8 bits wide, as on MBC30 (the Japanese Pokemon
// Crystal's chip, which uses the same header types): mbc3-tester, a 4 MiB
// type 0x11 ROM, expects all 256 banks (ADR-022). Bank numbers wrap at the
// real ROM and RAM sizes.
//
// The timer types (0x0F, 0x10) have the real-time clock (rtc.h); without it, a
// clock register select reads 0xFF and ignores writes.
class Mbc3 final : public Cartridge {
public:
    Mbc3(std::span<const std::uint8_t> rom, std::size_t ram_size, bool battery = false,
         bool clock = false);

    [[nodiscard]] std::uint8_t read_rom(std::uint16_t address) const noexcept override;
    void write_rom(std::uint16_t address, std::uint8_t value) noexcept override;
    [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept override;
    void write_ram(std::uint16_t address, std::uint8_t value) noexcept override;
    void tick(std::uint32_t t_cycles) noexcept override;

    // The banks currently mapped, for tests and the debugger.
    [[nodiscard]] std::size_t rom_bank() const noexcept;  // at 0x4000-0x7FFF
    [[nodiscard]] std::size_t ram_bank() const noexcept;

    [[nodiscard]] Rtc* clock() noexcept override;
    [[nodiscard]] const Rtc* clock() const noexcept override;

protected:
    void save_registers(StateWriter& out) const override;
    void load_registers(StateReader& in) override;
    void save_clock(StateWriter& out) const override;
    void load_clock(StateReader& in) override;

private:
    [[nodiscard]] bool ram_selected() const noexcept;
    [[nodiscard]] bool clock_selected() const noexcept;
    [[nodiscard]] Rtc::Register clock_register() const noexcept;
    [[nodiscard]] std::size_t ram_offset(std::uint16_t address) const noexcept;

    std::vector<std::uint8_t> rom_;
    std::size_t rom_bank_mask_;
    std::size_t ram_bank_mask_;
    std::uint8_t rom_bank_bits_;  // 0x7F (MBC3) or 0xFF (MBC30)
    bool clock_;
    Rtc rtc_;

    bool enabled_ = false;
    std::uint8_t rom_bank_ = 0x01;
    std::uint8_t select_ = 0x00;  // RAM bank or clock register
    bool latch_armed_ = false;    // the last write to 0x6000-0x7FFF was 0x00
};

}  // namespace core
