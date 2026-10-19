#include "core/gameboy.h"

#include <algorithm>
#include <string>
#include <utility>

#include "core/cartridge/cartridge.h"

namespace core {

std::string_view GameBoy::version() noexcept {
    return GB_VERSION;
}

std::string GameBoy::load_rom(std::span<const std::uint8_t> rom) {
    CartridgeOrError result = make_cartridge(rom);
    if (result.cartridge) {
        bus_.insert_cartridge(std::move(result.cartridge));
        constexpr std::size_t kHeaderChecksum = 0x014D;
        rom_identity_ = RomIdentity{};
        rom_identity_.header_checksum = rom[kHeaderChecksum];
        rom_identity_.global_checksum = {rom[kHeaderChecksum + 1], rom[kHeaderChecksum + 2]};
        const std::string title = read_header(rom).title;
        std::copy_n(title.begin(), std::min(title.size(), rom_identity_.title.size()),
                    rom_identity_.title.begin());
    }
    return result.error;
}

void GameBoy::set_button(Joypad::Button button, bool pressed) noexcept {
    bus_.set_button(button, pressed);
}

bool GameBoy::has_battery() const noexcept {
    const Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr && cartridge->has_battery();
}

Rtc* GameBoy::clock() noexcept {
    Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr ? cartridge->clock() : nullptr;
}

const Rtc* GameBoy::clock() const noexcept {
    const Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr ? cartridge->clock() : nullptr;
}

std::span<const std::uint8_t> GameBoy::cartridge_ram() const noexcept {
    const Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr ? cartridge->ram() : std::span<const std::uint8_t>{};
}

std::span<const std::uint8_t> GameBoy::battery_ram() const noexcept {
    const Cartridge* cartridge = bus_.cartridge();
    if (cartridge == nullptr || !cartridge->has_battery()) {
        return {};
    }
    return cartridge->ram();
}

bool GameBoy::load_battery_ram(std::span<const std::uint8_t> data) noexcept {
    Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr && cartridge->has_battery() && cartridge->load_ram(data);
}

bool GameBoy::battery_ram_changed() const noexcept {
    const Cartridge* cartridge = bus_.cartridge();
    return cartridge != nullptr && cartridge->has_battery() && cartridge->ram_changed();
}

void GameBoy::clear_battery_ram_changed() noexcept {
    if (Cartridge* cartridge = bus_.cartridge(); cartridge != nullptr) {
        cartridge->clear_ram_changed();
    }
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

std::uint64_t GameBoy::run_frame() {
    const std::uint64_t start = bus_.cycles();
    const std::uint64_t frame = bus_.frames_completed();
    while (bus_.frames_completed() == frame && bus_.cycles() - start < kTicksPerFrame) {
        cpu_.step();
    }
    return bus_.cycles() - start;
}

std::uint64_t GameBoy::cycles() const noexcept {
    return bus_.cycles();
}

const Ppu::Frame& GameBoy::frame() const noexcept {
    return bus_.frame();
}

const std::string& GameBoy::serial_output() const noexcept {
    return bus_.serial_output();
}

void GameBoy::set_audio_enabled(bool enabled) noexcept {
    bus_.apu().set_output_enabled(enabled);
}

void GameBoy::take_audio(std::vector<float>& out) {
    bus_.apu().take_samples(out);
}

const Apu& GameBoy::apu() const noexcept {
    return bus_.apu();
}

const Cpu& GameBoy::cpu() const noexcept {
    return cpu_;
}

const Bus& GameBoy::bus() const noexcept {
    return bus_;
}

}  // namespace core
