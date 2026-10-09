#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace core {

// Everything the CPU can address: 16-bit addresses, so 64 KiB.
// The CPU never touches memory directly; every read and write goes through here.
//
// For now this is one flat block of RAM. In M2 it becomes the real memory map
// (cartridge ROM, VRAM, work RAM, I/O registers, ...) behind the same two functions.
class Bus {
public:
    static constexpr std::size_t kAddressSpace = 0x10000;

    [[nodiscard]] std::uint8_t read8(std::uint16_t address) const noexcept;
    void write8(std::uint16_t address, std::uint8_t value) noexcept;

    // Copies `bytes` into memory starting at `address`.
    // Returns false and changes nothing if they would run past 0xFFFF.
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes,
                            std::uint16_t address = 0) noexcept;

private:
    std::array<std::uint8_t, kAddressSpace> memory_{};
};

}  // namespace core
