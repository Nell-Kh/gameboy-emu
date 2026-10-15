#include "core/cartridge/mbc1.h"

#include "core/cartridge/banking.h"

namespace core {

namespace {

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::size_t kRomBankSize = 0x4000;
constexpr std::size_t kRamBankSize = 0x2000;
constexpr std::uint16_t kRamStart = 0xA000;

constexpr std::uint8_t kRamEnableValue = 0x0A;
constexpr std::uint8_t kBank1Mask = 0x1F;
constexpr std::uint8_t kBank2Mask = 0x03;
constexpr unsigned kBank2Shift = 5;

}  // namespace

Mbc1::Mbc1(std::span<const std::uint8_t> rom, std::size_t ram_size, bool battery)
    : Cartridge(ram_size, battery),
      rom_(rom.begin(), rom.end()),
      rom_bank_mask_(bank_mask(rom_.size(), kRomBankSize)),
      ram_bank_mask_(bank_mask(ram_size, kRamBankSize)) {}

std::size_t Mbc1::rom_bank_low() const noexcept {
    const std::size_t bank = mode1_ ? static_cast<std::size_t>(bank2_) << kBank2Shift : 0;
    return bank & rom_bank_mask_;
}

std::size_t Mbc1::rom_bank_high() const noexcept {
    const std::size_t bank = (static_cast<std::size_t>(bank2_) << kBank2Shift) | bank1_;
    return bank & rom_bank_mask_;
}

std::size_t Mbc1::ram_bank() const noexcept {
    return mode1_ ? (bank2_ & ram_bank_mask_) : 0;
}

std::uint8_t Mbc1::read_rom(std::uint16_t address) const noexcept {
    const std::size_t bank = address < kRomBankSize ? rom_bank_low() : rom_bank_high();
    const std::size_t offset = bank * kRomBankSize + (address % kRomBankSize);
    return offset < rom_.size() ? rom_[offset] : kOpenBus;
}

void Mbc1::write_rom(std::uint16_t address, std::uint8_t value) noexcept {
    switch (address >> 13U) {
        case 0:  // 0x0000-0x1FFF
            ram_enabled_ = (value & 0x0FU) == kRamEnableValue;
            break;
        case 1:  // 0x2000-0x3FFF
            bank1_ = value & kBank1Mask;
            if (bank1_ == 0) {
                bank1_ = 1;
            }
            break;
        case 2:  // 0x4000-0x5FFF
            bank2_ = value & kBank2Mask;
            break;
        default:  // 0x6000-0x7FFF
            mode1_ = (value & 1U) != 0;
            break;
    }
}

bool Mbc1::ram_accessible() const noexcept {
    return ram_enabled_ && ram_size() > 0;
}

// A RAM chip smaller than one bank (2 KiB) repeats across the 8 KiB window.
std::size_t Mbc1::ram_offset(std::uint16_t address) const noexcept {
    const std::size_t window = ram_size() < kRamBankSize ? ram_size() : kRamBankSize;
    return ram_bank() * kRamBankSize + ((address - kRamStart) % window);
}

std::uint8_t Mbc1::read_ram(std::uint16_t address) const noexcept {
    return ram_accessible() ? ram_at(ram_offset(address)) : kOpenBus;
}

void Mbc1::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
    if (ram_accessible()) {
        set_ram_at(ram_offset(address), value);
    }
}

}  // namespace core
