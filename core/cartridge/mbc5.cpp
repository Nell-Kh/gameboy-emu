#include "core/cartridge/mbc5.h"

#include "core/cartridge/banking.h"
#include "core/state_io.h"

namespace core {

namespace {

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::size_t kRomBankSize = 0x4000;
constexpr std::size_t kRamBankSize = 0x2000;
constexpr std::uint16_t kRamStart = 0xA000;

constexpr std::uint8_t kRamEnableValue = 0x0A;
constexpr std::uint8_t kRamBankBits = 0x0F;
constexpr std::uint8_t kRumbleRamBankBits = 0x07;
constexpr std::uint16_t kRomBankLowBits = 0x00FF;
constexpr std::uint16_t kRomBankHighBit = 0x0100;

}  // namespace

Mbc5::Mbc5(std::span<const std::uint8_t> rom, std::size_t ram_size, bool rumble, bool battery)
    : Cartridge(ram_size, battery),
      rom_(rom.begin(), rom.end()),
      rom_bank_mask_(bank_mask(rom_.size(), kRomBankSize)),
      ram_bank_mask_(bank_mask(ram_size, kRamBankSize)),
      ram_bank_bits_(rumble ? kRumbleRamBankBits : kRamBankBits) {}

std::size_t Mbc5::rom_bank() const noexcept {
    return rom_bank_ & rom_bank_mask_;
}

std::size_t Mbc5::ram_bank() const noexcept {
    return ram_bank_ & ram_bank_mask_;
}

std::uint8_t Mbc5::read_rom(std::uint16_t address) const noexcept {
    const std::size_t bank = address < kRomBankSize ? 0 : rom_bank();
    const std::size_t offset = bank * kRomBankSize + (address % kRomBankSize);
    return offset < rom_.size() ? rom_[offset] : kOpenBus;
}

void Mbc5::write_rom(std::uint16_t address, std::uint8_t value) noexcept {
    if (address < 0x2000) {
        ram_enabled_ = (value & 0x0FU) == kRamEnableValue;
    } else if (address < 0x3000) {
        rom_bank_ = static_cast<std::uint16_t>((rom_bank_ & kRomBankHighBit) | value);
    } else if (address < 0x4000) {
        const auto high = static_cast<std::uint16_t>((value & 1U) != 0 ? kRomBankHighBit : 0);
        rom_bank_ = static_cast<std::uint16_t>((rom_bank_ & kRomBankLowBits) | high);
    } else if (address < 0x6000) {
        ram_bank_ = value & ram_bank_bits_;
    }
}

bool Mbc5::ram_accessible() const noexcept {
    return ram_enabled_ && ram_size() > 0;
}

// A RAM chip smaller than one bank repeats across the 8 KiB window.
std::size_t Mbc5::ram_offset(std::uint16_t address) const noexcept {
    const std::size_t window = ram_size() < kRamBankSize ? ram_size() : kRamBankSize;
    return ram_bank() * kRamBankSize + ((address - kRamStart) % window);
}

std::uint8_t Mbc5::read_ram(std::uint16_t address) const noexcept {
    return ram_accessible() ? ram_at(ram_offset(address)) : kOpenBus;
}

void Mbc5::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
    if (ram_accessible()) {
        set_ram_at(ram_offset(address), value);
    }
}

void Mbc5::save_registers(StateWriter& out) const {
    out.boolean(ram_enabled_);
    out.u16(rom_bank_);
    out.u8(ram_bank_);
}

void Mbc5::load_registers(StateReader& in) {
    constexpr std::uint32_t kLastRomBank = 0x1FF;
    ram_enabled_ = in.boolean();
    rom_bank_ = static_cast<std::uint16_t>(in.at_most(std::uint32_t{in.u16()}, kLastRomBank));
    ram_bank_ = in.at_most(in.u8(), ram_bank_bits_);
}

}  // namespace core
