#include "core/bus.h"

namespace core {

namespace {

// With no cartridge the data lines float high.
constexpr std::uint8_t kOpenBus = 0xFF;

}  // namespace

void Bus::load_rom(std::span<const std::uint8_t> rom) {
    rom_.assign(rom.begin(), rom.end());
}

std::uint8_t Bus::read8(std::uint16_t address) const noexcept {
    if (address < kRomRegionSize) {
        return address < rom_.size() ? rom_[address] : kOpenBus;
    }
    return memory_[address];
}

void Bus::write8(std::uint16_t address, std::uint8_t value) noexcept {
    if (address < kRomRegionSize) {
        // ROM cannot be written. Cartridges with a mapper chip watch these
        // writes to switch banks; that is M2.
        return;
    }
    memory_[address] = value;
}

void Bus::tick(std::uint32_t t_cycles) noexcept {
    cycles_ += t_cycles;
}

std::uint64_t Bus::cycles() const noexcept {
    return cycles_;
}

}  // namespace core
