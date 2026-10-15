#include "core/cartridge/mbc3.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/rtc.h"

namespace {

using core::Mbc3;

// A ROM of `banks` 16 KiB banks; each bank starts with its bank number.
std::vector<std::uint8_t> banked_rom(std::size_t banks) {
    std::vector<std::uint8_t> rom(banks * 0x4000, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000] = static_cast<std::uint8_t>(bank);
    }
    return rom;
}

constexpr std::uint16_t kEnable = 0x0000;
constexpr std::uint16_t kRomBank = 0x2000;
constexpr std::uint16_t kSelect = 0x4000;

TEST(Mbc3, StartsWithBank0LowAndBank1High) {
    const auto rom = banked_rom(8);
    const Mbc3 cart(rom, 0);
    EXPECT_EQ(cart.read_rom(0x0000), 0);
    EXPECT_EQ(cart.read_rom(0x4000), 1);
}

TEST(Mbc3, TheBankRegisterReachesEveryBankOfA2MiBRom) {
    const auto rom = banked_rom(128);
    Mbc3 cart(rom, 0);
    for (std::size_t bank = 1; bank < 128; ++bank) {
        cart.write_rom(kRomBank, static_cast<std::uint8_t>(bank));
        EXPECT_EQ(cart.read_rom(0x4000), bank);
    }
}

TEST(Mbc3, Bank0IsReadAsBank1) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0);
    cart.write_rom(kRomBank, 0x00);
    EXPECT_EQ(cart.rom_bank(), 1U);
}

TEST(Mbc3, UpTo2MiBTheBankRegisterHasSevenBits) {
    const auto rom = banked_rom(128);
    Mbc3 cart(rom, 0);
    cart.write_rom(kRomBank, 0x85);
    EXPECT_EQ(cart.read_rom(0x4000), 0x05);
    cart.write_rom(kRomBank, 0x80);  // 0 in seven bits, so bank 1
    EXPECT_EQ(cart.read_rom(0x4000), 0x01);
}

TEST(Mbc3, AboveTwoMiBTheBankRegisterHasEightBitsAsOnMbc30) {
    const auto rom = banked_rom(256);  // 4 MiB, like mbc3-tester
    Mbc3 cart(rom, 0);
    cart.write_rom(kRomBank, 0x80);
    EXPECT_EQ(cart.read_rom(0x4000), 0x80);
    cart.write_rom(kRomBank, 0xFF);
    EXPECT_EQ(cart.read_rom(0x4000), 0xFF);
}

TEST(Mbc3, BanksWrapAtTheRomSize) {
    const auto rom = banked_rom(16);
    Mbc3 cart(rom, 0);
    cart.write_rom(kRomBank, 0x13);
    EXPECT_EQ(cart.read_rom(0x4000), 0x03);
}

TEST(Mbc3, RamIsDisabledAfterPowerOnAndEnabledByAInTheLowNibble) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0x2000);
    cart.write_ram(0xA000, 0x42);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
    cart.write_rom(kEnable, 0x1A);
    cart.write_ram(0xA000, 0x42);
    EXPECT_EQ(cart.read_ram(0xA000), 0x42);
    cart.write_rom(kEnable, 0x00);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(Mbc3, TheSelectRegisterPicksOneOfFourRamBanks) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0x8000);  // 32 KiB
    cart.write_rom(kEnable, 0x0A);
    for (std::uint8_t bank = 0; bank < 4; ++bank) {
        cart.write_rom(kSelect, bank);
        cart.write_ram(0xA000, static_cast<std::uint8_t>(0x50 + bank));
    }
    for (std::uint8_t bank = 0; bank < 4; ++bank) {
        cart.write_rom(kSelect, bank);
        EXPECT_EQ(cart.read_ram(0xA000), 0x50 + bank) << "bank " << int{bank};
    }
}

TEST(Mbc3, ClockRegisterSelectsReadFFWithoutATimer) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0x2000);
    cart.write_rom(kEnable, 0x0A);
    cart.write_ram(0xA000, 0x42);
    for (const std::uint8_t select : {0x08, 0x0C, 0x0D, 0xFF}) {
        cart.write_rom(kSelect, select);
        EXPECT_EQ(cart.read_ram(0xA000), 0xFF) << "select " << int{select};
        cart.write_ram(0xA000, 0x99);  // ignored
    }
    cart.write_rom(kSelect, 0x00);
    EXPECT_EQ(cart.read_ram(0xA000), 0x42);
}

constexpr std::uint16_t kLatch = 0x6000;
constexpr std::uint32_t kSecond = core::Rtc::kTicksPerSecond;

TEST(Mbc3, TheClockIsLatchedByWriting00Then01) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0, true, true);
    cart.write_rom(kEnable, 0x0A);
    cart.write_rom(kSelect, 0x08);  // seconds
    cart.tick(kSecond);
    cart.write_rom(kLatch, 0x01);  // no 0x00 before it: no latch
    EXPECT_EQ(cart.read_ram(0xA000), 0);
    cart.write_rom(kLatch, 0x00);
    cart.write_rom(kLatch, 0x01);
    EXPECT_EQ(cart.read_ram(0xA000), 1);
    cart.tick(kSecond);
    EXPECT_EQ(cart.read_ram(0xA000), 1);  // the copy stays put
    cart.write_rom(kLatch, 0x01);
    EXPECT_EQ(cart.read_ram(0xA000), 1);
    cart.write_rom(kLatch, 0x00);
    cart.write_rom(kLatch, 0x01);
    EXPECT_EQ(cart.read_ram(0xA000), 2);
}

TEST(Mbc3, EachClockRegisterHasItsOwnSelect) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0, true, true);
    cart.write_rom(kEnable, 0x0A);
    for (std::uint8_t select = 0x08; select <= 0x0C; ++select) {
        cart.write_rom(kSelect, select);
        cart.write_ram(0xA000, select);
    }
    EXPECT_EQ(cart.rtc().live(), (core::Rtc::Registers{0x08, 0x09, 0x0A, 0x0B, 0x00}));
    cart.write_rom(kSelect, 0x0B);
    EXPECT_EQ(cart.read_ram(0xBFFF), 0x0B);  // the whole window maps the register
}

TEST(Mbc3, TheClockRegistersNeedTheEnable) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0, true, true);
    cart.write_rom(kSelect, 0x09);
    cart.write_ram(0xA000, 0x22);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
    cart.write_rom(kEnable, 0x0A);
    EXPECT_EQ(cart.read_ram(0xA000), 0x00);
}

TEST(Mbc3, WithAClockTheRamAndUnusedSelectsAreUnchanged) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0x8000, true, true);
    cart.write_rom(kEnable, 0x0A);
    cart.write_rom(kSelect, 0x02);
    cart.write_ram(0xA000, 0x77);
    cart.write_rom(kSelect, 0x0D);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
    cart.write_rom(kSelect, 0x02);
    EXPECT_EQ(cart.read_ram(0xA000), 0x77);
}

TEST(Mbc3, WithoutAClockTickDoesNothing) {
    const auto rom = banked_rom(8);
    Mbc3 cart(rom, 0);
    cart.tick(10 * kSecond);
    EXPECT_FALSE(cart.has_clock());
    EXPECT_EQ(cart.rtc().live(), (core::Rtc::Registers{}));
}

}  // namespace
