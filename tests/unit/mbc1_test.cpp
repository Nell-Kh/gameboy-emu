#include "core/cartridge/mbc1.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using core::Mbc1;

// A ROM of `banks` 16 KiB banks whose bytes at offsets 0x0000 and 0x3FFF of
// each bank hold the bank number.
std::vector<std::uint8_t> banked_rom(std::size_t banks) {
    std::vector<std::uint8_t> rom(banks * 0x4000, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000] = static_cast<std::uint8_t>(bank);
        rom[bank * 0x4000 + 0x3FFF] = static_cast<std::uint8_t>(bank);
    }
    return rom;
}

constexpr std::uint16_t kRamg = 0x0000;
constexpr std::uint16_t kBank1 = 0x2000;
constexpr std::uint16_t kBank2 = 0x4000;
constexpr std::uint16_t kMode = 0x6000;

TEST(Mbc1, StartsWithBank0LowAndBank1High) {
    const auto rom = banked_rom(8);
    const Mbc1 cart(rom, 0);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
    EXPECT_EQ(cart.read_rom(0x4000), 1);
    EXPECT_EQ(cart.read_rom(0x7FFF), 1);
}

TEST(Mbc1, Bank1SelectsTheHighBank) {
    const auto rom = banked_rom(32);
    Mbc1 cart(rom, 0);
    for (std::uint8_t bank = 1; bank < 32; ++bank) {
        cart.write_rom(kBank1, bank);
        EXPECT_EQ(cart.read_rom(0x4000), bank);
        EXPECT_EQ(cart.read_rom(0x0000), 0);
    }
}

TEST(Mbc1, Bank1ZeroIsReadAsOne) {
    const auto rom = banked_rom(8);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank1, 0x00);
    EXPECT_EQ(cart.rom_bank_high(), 1U);
    EXPECT_EQ(cart.read_rom(0x4000), 1);
}

TEST(Mbc1, Bank1KeepsOnlyFiveBitsAndTheZeroCheckUsesThem) {
    const auto rom = banked_rom(32);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank1, 0xE3);
    EXPECT_EQ(cart.read_rom(0x4000), 0x03);
    // 0x20 has its five low bits clear, so it counts as 0 and becomes 1.
    cart.write_rom(kBank1, 0x20);
    EXPECT_EQ(cart.read_rom(0x4000), 0x01);
}

TEST(Mbc1, BanksWrapAtTheRomSize) {
    const auto rom = banked_rom(4);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank1, 0x06);
    EXPECT_EQ(cart.read_rom(0x4000), 2);
}

TEST(Mbc1, Bank2AddsBitsFiveAndSix) {
    const auto rom = banked_rom(128);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank1, 0x05);
    cart.write_rom(kBank2, 0x02);
    EXPECT_EQ(cart.read_rom(0x4000), 0x45);
}

TEST(Mbc1, Banks20h40h60hCannotBeMappedHigh) {
    const auto rom = banked_rom(128);
    Mbc1 cart(rom, 0);
    for (const std::uint8_t bank2 : {1, 2, 3}) {
        cart.write_rom(kBank2, bank2);
        cart.write_rom(kBank1, 0x00);
        EXPECT_EQ(cart.read_rom(0x4000), (bank2 << 5) | 1) << "BANK2 " << int{bank2};
    }
}

TEST(Mbc1, InMode0TheLowAreaIsAlwaysBank0) {
    const auto rom = banked_rom(128);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank2, 0x03);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
}

TEST(Mbc1, InMode1Bank2AlsoSelectsTheLowArea) {
    const auto rom = banked_rom(128);
    Mbc1 cart(rom, 0);
    cart.write_rom(kMode, 0x01);
    cart.write_rom(kBank2, 0x02);
    EXPECT_EQ(cart.read_rom(0x0000), 0x40);
    EXPECT_EQ(cart.read_rom(0x3FFF), 0x40);
}

TEST(Mbc1, InMode1OnASmallRomTheLowAreaStaysBank0) {
    const auto rom = banked_rom(32);
    Mbc1 cart(rom, 0);
    cart.write_rom(kMode, 0x01);
    cart.write_rom(kBank2, 0x01);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
}

TEST(Mbc1, ModeKeepsOnlyBit0) {
    const auto rom = banked_rom(128);
    Mbc1 cart(rom, 0);
    cart.write_rom(kBank2, 0x01);
    cart.write_rom(kMode, 0xFE);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
    cart.write_rom(kMode, 0x01);
    EXPECT_EQ(cart.read_rom(0x0000), 0x20);
}

TEST(Mbc1, RamIsDisabledAfterPowerOn) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x2000);
    cart.write_ram(0xA000, 0x12);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(Mbc1, RamEnableNeedsAInTheLowNibble) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x2000);
    cart.write_rom(kRamg, 0xFA);
    cart.write_ram(0xA000, 0x12);
    EXPECT_EQ(cart.read_ram(0xA000), 0x12);
    cart.write_rom(kRamg, 0x0B);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
    cart.write_rom(kRamg, 0x0A);
    EXPECT_EQ(cart.read_ram(0xA000), 0x12);
}

TEST(Mbc1, NoRamReadsFFEvenWhenEnabled) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0);
    cart.write_rom(kRamg, 0x0A);
    cart.write_ram(0xA000, 0x12);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(Mbc1, InMode1Bank2SelectsTheRamBank) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x8000);
    cart.write_rom(kRamg, 0x0A);
    cart.write_rom(kMode, 0x01);
    for (std::uint8_t bank = 0; bank < 4; ++bank) {
        cart.write_rom(kBank2, bank);
        cart.write_ram(0xA000, static_cast<std::uint8_t>(0x10 + bank));
    }
    for (std::uint8_t bank = 0; bank < 4; ++bank) {
        cart.write_rom(kBank2, bank);
        EXPECT_EQ(cart.ram_bank(), bank);
        EXPECT_EQ(cart.read_ram(0xA000), 0x10 + bank);
    }
}

TEST(Mbc1, InMode0TheRamBankIsAlways0) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x8000);
    cart.write_rom(kRamg, 0x0A);
    cart.write_ram(0xA000, 0x11);
    cart.write_rom(kBank2, 0x02);
    EXPECT_EQ(cart.ram_bank(), 0U);
    EXPECT_EQ(cart.read_ram(0xA000), 0x11);
}

TEST(Mbc1, EightKiBRamIgnoresTheRamBank) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x2000);
    cart.write_rom(kRamg, 0x0A);
    cart.write_ram(0xA123, 0x5A);
    cart.write_rom(kMode, 0x01);
    cart.write_rom(kBank2, 0x03);
    EXPECT_EQ(cart.read_ram(0xA123), 0x5A);
}

TEST(Mbc1, TwoKiBRamRepeatsAcrossTheWindow) {
    const auto rom = banked_rom(2);
    Mbc1 cart(rom, 0x800);
    cart.write_rom(kRamg, 0x0A);
    cart.write_ram(0xA001, 0x77);
    EXPECT_EQ(cart.read_ram(0xA801), 0x77);
    EXPECT_EQ(cart.read_ram(0xB801), 0x77);
}

}  // namespace
