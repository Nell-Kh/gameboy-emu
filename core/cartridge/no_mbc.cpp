#include "core/cartridge/no_mbc.h"

namespace core {

namespace {

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::uint16_t kRamStart = 0xA000;

}  // namespace

NoMbc::NoMbc(std::span<const std::uint8_t> rom, std::size_t ram_size, bool battery)
    : Cartridge(ram_size, battery), rom_(rom.begin(), rom.end()) {}

std::uint8_t NoMbc::read_rom(std::uint16_t address) const noexcept {
    return address < rom_.size() ? rom_[address] : kOpenBus;
}

void NoMbc::write_rom(std::uint16_t /*address*/, std::uint8_t /*value*/) noexcept {}

std::uint8_t NoMbc::read_ram(std::uint16_t address) const noexcept {
    const std::size_t offset = address - kRamStart;
    return offset < ram_size() ? ram_at(offset) : kOpenBus;
}

void NoMbc::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
    const std::size_t offset = address - kRamStart;
    if (offset < ram_size()) {
        set_ram_at(offset, value);
    }
}

}  // namespace core
