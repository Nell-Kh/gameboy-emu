#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {

// Everything the CPU can address: 16-bit addresses, so 64 KiB.
// The CPU never touches memory directly; every read and write goes through here
// and is routed to whatever lives at that address.
//
//   0x0000-0x7FFF  cartridge ROM (read-only)
//   0x8000-0xFFFF  RAM for now; split into video RAM, work RAM, I/O registers
//                  and high RAM as those parts are built
class Bus {
public:
    static constexpr std::size_t kAddressSpace = 0x10000;
    static constexpr std::size_t kRomRegionSize = 0x8000;

    // Inserts a cartridge. Only the first 32 KiB are visible until bank
    // switching (MBC) arrives in M2.
    void load_rom(std::span<const std::uint8_t> rom);

    [[nodiscard]] std::uint8_t read8(std::uint16_t address) const noexcept;
    void write8(std::uint16_t address, std::uint8_t value) noexcept;

    // Advances the rest of the machine by `t_cycles` clock ticks.
    // The CPU calls this on every memory access (see ADR-005). For now it only
    // counts; the timer, PPU and APU will hang off it.
    void tick(std::uint32_t t_cycles) noexcept;

    // Total clock ticks since power-on.
    [[nodiscard]] std::uint64_t cycles() const noexcept;

private:
    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, kAddressSpace> memory_{};
    std::uint64_t cycles_ = 0;
};

}  // namespace core
