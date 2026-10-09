#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.h"

namespace core {

// A cartridge with no mapper: up to 32 KiB of ROM wired straight to the
// address bus, and optionally up to 8 KiB of RAM, always enabled.
class NoMbc final : public Cartridge {
public:
    NoMbc(std::span<const std::uint8_t> rom, std::size_t ram_size);

    [[nodiscard]] std::uint8_t read_rom(std::uint16_t address) const noexcept override;
    void write_rom(std::uint16_t address, std::uint8_t value) noexcept override;
    [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept override;
    void write_ram(std::uint16_t address, std::uint8_t value) noexcept override;

private:
    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> ram_;
};

}  // namespace core
