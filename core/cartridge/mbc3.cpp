#include "core/cartridge/mbc3.h"

#include "core/cartridge/banking.h"

namespace core {

namespace {

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::size_t kRomBankSize = 0x4000;
constexpr std::size_t kRamBankSize = 0x2000;
constexpr std::uint16_t kRamStart = 0xA000;

constexpr std::uint8_t kEnableValue = 0x0A;
constexpr std::size_t kMbc3MaxRom = 0x200000;  // 2 MiB: beyond it, the MBC30's 8 bank bits
constexpr std::uint8_t kMbc3RomBankBits = 0x7F;
constexpr std::uint8_t kMbc30RomBankBits = 0xFF;
constexpr std::uint8_t kLastRamBank = 0x07;

}  // namespace

Mbc3::Mbc3(std::span<const std::uint8_t> rom, std::size_t ram_size, bool battery)
    : Cartridge(ram_size, battery),
      rom_(rom.begin(), rom.end()),
      rom_bank_mask_(bank_mask(rom_.size(), kRomBankSize)),
      ram_bank_mask_(bank_mask(ram_size, kRamBankSize)),
      rom_bank_bits_(rom_.size() > kMbc3MaxRom ? kMbc30RomBankBits : kMbc3RomBankBits) {}

std::size_t Mbc3::rom_bank() const noexcept {
    return rom_bank_ & rom_bank_mask_;
}

std::size_t Mbc3::ram_bank() const noexcept {
    return select_ & ram_bank_mask_;
}

std::uint8_t Mbc3::read_rom(std::uint16_t address) const noexcept {
    const std::size_t bank = address < kRomBankSize ? 0 : rom_bank();
    const std::size_t offset = bank * kRomBankSize + (address % kRomBankSize);
    return offset < rom_.size() ? rom_[offset] : kOpenBus;
}

void Mbc3::write_rom(std::uint16_t address, std::uint8_t value) noexcept {
    switch (address >> 13U) {
        case 0:  // 0x0000-0x1FFF
            enabled_ = (value & 0x0FU) == kEnableValue;
            break;
        case 1:  // 0x2000-0x3FFF
            rom_bank_ = value & rom_bank_bits_;
            if (rom_bank_ == 0) {
                rom_bank_ = 1;
            }
            break;
        case 2:  // 0x4000-0x5FFF
            select_ = value;
            break;
        default:  // 0x6000-0x7FFF: the clock latch, with the timer types
            break;
    }
}

bool Mbc3::ram_selected() const noexcept {
    return enabled_ && select_ <= kLastRamBank && ram_size() > 0;
}

// A RAM chip smaller than one bank repeats across the 8 KiB window.
std::size_t Mbc3::ram_offset(std::uint16_t address) const noexcept {
    const std::size_t window = ram_size() < kRamBankSize ? ram_size() : kRamBankSize;
    return ram_bank() * kRamBankSize + ((address - kRamStart) % window);
}

std::uint8_t Mbc3::read_ram(std::uint16_t address) const noexcept {
    return ram_selected() ? ram_at(ram_offset(address)) : kOpenBus;
}

void Mbc3::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
    if (ram_selected()) {
        set_ram_at(ram_offset(address), value);
    }
}

}  // namespace core
