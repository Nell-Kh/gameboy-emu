#include "core/gameboy.h"

namespace core {

std::string_view GameBoy::version() noexcept {
    return GB_VERSION;
}

void GameBoy::load_rom(std::span<const std::uint8_t> rom) {
    bus_.load_rom(rom);
}

std::uint32_t GameBoy::step() {
    return cpu_.step();
}

std::uint64_t GameBoy::run_for(std::uint64_t t_cycles) {
    const std::uint64_t start = bus_.cycles();
    while (bus_.cycles() - start < t_cycles) {
        cpu_.step();
    }
    return bus_.cycles() - start;
}

std::uint64_t GameBoy::cycles() const noexcept {
    return bus_.cycles();
}

const std::string& GameBoy::serial_output() const noexcept {
    return bus_.serial_output();
}

const Cpu& GameBoy::cpu() const noexcept {
    return cpu_;
}

const Bus& GameBoy::bus() const noexcept {
    return bus_;
}

}  // namespace core
