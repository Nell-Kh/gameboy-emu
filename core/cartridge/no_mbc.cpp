#include "core/cartridge/no_mbc.h"

namespace core {

namespace {

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::uint16_t kRamStart = 0xA000;

}  // namespace

NoMbc::NoMbc(std::span<const std::uint8_t> rom, std::size_t ram_size)
    : rom_(rom.begin(), rom.end()), ram_(ram_size, 0x00) {}

std::uint8_t NoMbc::read_rom(std::uint16_t address) const noexcept {
    return address < rom_.size() ? rom_[address] : kOpenBus;
}

void NoMbc::write_rom(std::uint16_t /*address*/, std::uint8_t /*value*/) noexcept {}

std::uint8_t NoMbc::read_ram(std::uint16_t address) const noexcept {
    const std::size_t offset = address - kRamStart;
    return offset < ram_.size() ? ram_[offset] : kOpenBus;
}

void NoMbc::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
    const std::size_t offset = address - kRamStart;
    if (offset < ram_.size()) {
        ram_[offset] = value;
    }
}

}  // namespace core
