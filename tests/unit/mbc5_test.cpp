#include "core/cartridge/mbc5.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using core::Mbc5;

// A ROM of `banks` 16 KiB banks. Each bank starts with its 9-bit bank number,
// low byte first, so tests can see which bank is mapped.
std::vector<std::uint8_t> banked_rom(std::size_t banks) {
    std::vector<std::uint8_t> rom(banks * 0x4000, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000] = static_cast<std::uint8_t>(bank & 0xFFU);
        rom[bank * 0x4000 + 1] = static_cast<std::uint8_t>(bank >> 8U);
    }
    return rom;
}

std::size_t mapped_high_bank(const Mbc5& cart) {
    return cart.read_rom(0x4000) | (static_cast<std::size_t>(cart.read_rom(0x4001)) << 8U);
}

constexpr std::uint16_t kRamEnable = 0x0000;
constexpr std::uint16_t kRomBankLow = 0x2000;
constexpr std::uint16_t kRomBankHigh = 0x3000;
constexpr std::uint16_t kRamBank = 0x4000;
constexpr bool kNoRumble = false;
constexpr bool kRumble = true;

TEST(Mbc5, StartsWithBank0LowAndBank1High) {
    const auto rom = banked_rom(8);
    const Mbc5 cart(rom, 0, kNoRumble);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
    EXPECT_EQ(mapped_high_bank(cart), 1U);
}

TEST(Mbc5, TheLowRegisterSelectsBanks0To255) {
    const auto rom = banked_rom(512);
    Mbc5 cart(rom, 0, kNoRumble);
    for (std::size_t bank = 0; bank < 256; ++bank) {
        cart.write_rom(kRomBankLow, static_cast<std::uint8_t>(bank));
        EXPECT_EQ(mapped_high_bank(cart), bank);
        EXPECT_EQ(cart.read_rom(0x0000), 0);
    }
}

TEST(Mbc5, Bank0CanBeMappedHigh) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(kRomBankLow, 0x00);
    EXPECT_EQ(cart.rom_bank(), 0U);
    EXPECT_EQ(mapped_high_bank(cart), 0U);
}

TEST(Mbc5, The3000RegisterIsBit8AndOnlyItsBit0Counts) {
    const auto rom = banked_rom(512);
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(kRomBankLow, 0x23);
    cart.write_rom(kRomBankHigh, 0xFF);  // Mooneye's mbc5 tests set bits 1-7 too
    EXPECT_EQ(mapped_high_bank(cart), 0x123U);
    cart.write_rom(kRomBankHigh, 0xFE);
    EXPECT_EQ(mapped_high_bank(cart), 0x023U);
}

TEST(Mbc5, EachRegisterKeepsTheOtherPartOfTheBankNumber) {
    const auto rom = banked_rom(512);
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(kRomBankHigh, 0x01);
    cart.write_rom(kRomBankLow, 0x45);
    EXPECT_EQ(mapped_high_bank(cart), 0x145U);
    cart.write_rom(kRomBankLow, 0x00);
    EXPECT_EQ(mapped_high_bank(cart), 0x100U);
}

TEST(Mbc5, TheLowRegisterSpansAddresses2000To2FFF) {
    const auto rom = banked_rom(512);
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(0x2FFF, 0x07);
    EXPECT_EQ(mapped_high_bank(cart), 7U);
    cart.write_rom(0x3FFF, 0x01);  // the high register spans 3000-3FFF
    EXPECT_EQ(mapped_high_bank(cart), 0x107U);
}

TEST(Mbc5, BanksWrapAtTheRomSize) {
    const auto rom = banked_rom(64);  // 1 MiB
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(kRomBankLow, 70);
    EXPECT_EQ(mapped_high_bank(cart), 70U % 64);
    cart.write_rom(kRomBankHigh, 0x01);
    EXPECT_EQ(mapped_high_bank(cart), (256U + 70) % 64);
}

TEST(Mbc5, WritesTo6000To7FFFChangeNothing) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x2000, kNoRumble);
    cart.write_rom(kRomBankLow, 3);
    cart.write_rom(0x6000, 0xFF);
    cart.write_rom(0x7FFF, 0x01);
    EXPECT_EQ(mapped_high_bank(cart), 3U);
}

TEST(Mbc5, RamIsDisabledAfterPowerOn) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x2000, kNoRumble);
    cart.write_ram(0xA000, 0x42);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(Mbc5, RamEnableNeedsAInTheLowNibble) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x2000, kNoRumble);
    cart.write_rom(kRamEnable, 0x1A);
    cart.write_ram(0xA000, 0x42);
    EXPECT_EQ(cart.read_ram(0xA000), 0x42);
    cart.write_rom(kRamEnable, 0x0B);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
    cart.write_rom(kRamEnable, 0x0A);
    EXPECT_EQ(cart.read_ram(0xA000), 0x42);
}

TEST(Mbc5, NoRamReadsFFEvenWhenEnabled) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0, kNoRumble);
    cart.write_rom(kRamEnable, 0x0A);
    cart.write_ram(0xA000, 0x42);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(Mbc5, TheRamBankRegisterSelectsSixteenBanks) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x20000, kNoRumble);  // 128 KiB
    cart.write_rom(kRamEnable, 0x0A);
    for (std::uint8_t bank = 0; bank < 16; ++bank) {
        cart.write_rom(kRamBank, bank);
        cart.write_ram(0xA000, static_cast<std::uint8_t>(0x80 | bank));
    }
    for (std::uint8_t bank = 0; bank < 16; ++bank) {
        cart.write_rom(kRamBank, bank);
        EXPECT_EQ(cart.ram_bank(), bank);
        EXPECT_EQ(cart.read_ram(0xA000), 0x80 | bank) << "bank " << int{bank};
    }
}

TEST(Mbc5, RamBanksWrapAtTheRamSize) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x8000, kNoRumble);  // 32 KiB: 4 banks
    cart.write_rom(kRamEnable, 0x0A);
    cart.write_rom(kRamBank, 0x01);
    cart.write_ram(0xA123, 0x55);
    cart.write_rom(kRamBank, 0x05);
    EXPECT_EQ(cart.ram_bank(), 1U);
    EXPECT_EQ(cart.read_ram(0xA123), 0x55);
}

TEST(Mbc5, WithARumbleMotorBit3DoesNotSelectRam) {
    const auto rom = banked_rom(8);
    Mbc5 cart(rom, 0x20000, kRumble);
    cart.write_rom(kRamBank, 0x08);  // motor on, RAM bank 0
    EXPECT_EQ(cart.ram_bank(), 0U);
    cart.write_rom(kRamBank, 0x0F);
    EXPECT_EQ(cart.ram_bank(), 7U);
}

}  // namespace
