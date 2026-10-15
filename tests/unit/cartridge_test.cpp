#include "core/cartridge/cartridge.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/cartridge/no_mbc.h"
#include "core/cartridge/rtc.h"

namespace {

using core::Cartridge;
using core::make_cartridge;
using core::NoMbc;

// A ROM image of `size` bytes with the given header fields. Each 16 KiB bank
// starts with its own bank number, so tests can see which bank is mapped.
std::vector<std::uint8_t> make_image(std::size_t size, std::uint8_t type, std::uint8_t rom_code,
                                     std::uint8_t ram_code) {
    std::vector<std::uint8_t> rom(size, 0x00);
    for (std::size_t bank = 0; bank * 0x4000 < size; ++bank) {
        rom[bank * 0x4000] = static_cast<std::uint8_t>(bank);
    }
    const std::string title = "TESTCART";
    for (std::size_t i = 0; i < title.size(); ++i) {
        rom[0x0134 + i] = static_cast<std::uint8_t>(title[i]);
    }
    rom[0x0147] = type;
    rom[0x0148] = rom_code;
    rom[0x0149] = ram_code;
    return rom;
}

TEST(CartridgeHeader, DecodesTitleTypeAndSizes) {
    const auto rom = make_image(0x10000, 0x03, 0x01, 0x03);
    const core::CartridgeHeader header = core::read_header(rom);
    EXPECT_EQ(header.title, "TESTCART");
    EXPECT_EQ(header.type, 0x03);
    EXPECT_TRUE(header.sizes_valid);
    EXPECT_EQ(header.rom_size, 0x10000U);
    EXPECT_EQ(header.ram_size, 0x8000U);
}

TEST(CartridgeHeader, RomSizeCodesDoubleFrom32KiB) {
    for (std::uint8_t code = 0; code <= 8; ++code) {
        auto rom = make_image(0x8000, 0x00, code, 0x00);
        EXPECT_EQ(core::read_header(rom).rom_size, std::size_t{0x8000} << code) << int{code};
    }
}

TEST(CartridgeHeader, RamSizeCodes) {
    const std::vector<std::size_t> expected = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    for (std::size_t code = 0; code < expected.size(); ++code) {
        auto rom = make_image(0x8000, 0x00, 0x00, static_cast<std::uint8_t>(code));
        EXPECT_EQ(core::read_header(rom).ram_size, expected[code]) << code;
    }
}

TEST(CartridgeHeader, UnknownSizeCodesAreFlagged) {
    EXPECT_FALSE(core::read_header(make_image(0x8000, 0x00, 0x09, 0x00)).sizes_valid);
    EXPECT_FALSE(core::read_header(make_image(0x8000, 0x00, 0x00, 0x06)).sizes_valid);
}

TEST(MakeCartridge, RomOnlyIsAccepted) {
    const auto result = make_cartridge(make_image(0x8000, 0x00, 0x00, 0x00));
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    EXPECT_EQ(result.error, "");
}

TEST(MakeCartridge, Mbc1IsAccepted) {
    for (const std::uint8_t type : {0x01, 0x02, 0x03}) {
        const auto result = make_cartridge(make_image(0x10000, type, 0x01, 0x02));
        EXPECT_NE(result.cartridge, nullptr) << "type " << int{type} << ": " << result.error;
    }
}

TEST(MakeCartridge, Mbc3IsAccepted) {
    for (const std::uint8_t type : {0x11, 0x12, 0x13}) {
        const auto result = make_cartridge(make_image(0x10000, type, 0x01, 0x03));
        EXPECT_NE(result.cartridge, nullptr) << "type " << int{type} << ": " << result.error;
    }
    const auto battery = make_cartridge(make_image(0x10000, 0x13, 0x01, 0x03));
    ASSERT_NE(battery.cartridge, nullptr);
    EXPECT_TRUE(battery.cartridge->has_battery());
}

TEST(MakeCartridge, Mbc3WithATimerIsAccepted) {
    const auto timer = make_cartridge(make_image(0x8000, 0x0F, 0x00, 0x00));
    ASSERT_NE(timer.cartridge, nullptr) << timer.error;
    const auto timer_ram = make_cartridge(make_image(0x8000, 0x10, 0x00, 0x03));
    ASSERT_NE(timer_ram.cartridge, nullptr) << timer_ram.error;
    EXPECT_TRUE(timer_ram.cartridge->has_battery());

    // The clock is there: after an emulated second, a latched read gives 1.
    auto& cart = *timer.cartridge;
    cart.write_rom(0x0000, 0x0A);
    cart.write_rom(0x4000, 0x08);
    cart.tick(core::Rtc::kTicksPerSecond);
    cart.write_rom(0x6000, 0x00);
    cart.write_rom(0x6000, 0x01);
    EXPECT_EQ(cart.read_ram(0xA000), 1);
}

TEST(MakeCartridge, Mbc5IsAccepted) {
    for (const std::uint8_t type : {0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E}) {
        const auto result = make_cartridge(make_image(0x10000, type, 0x01, 0x03));
        EXPECT_NE(result.cartridge, nullptr) << "type " << int{type} << ": " << result.error;
    }
}

TEST(MakeCartridge, Mbc5MapsBank0High) {
    const auto rom = make_image(0x10000, 0x19, 0x01, 0x00);
    const auto result = make_cartridge(rom);
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    result.cartridge->write_rom(0x2000, 0x00);
    EXPECT_EQ(result.cartridge->read_rom(0x4000), 0);  // on MBC1 this would be bank 1
}

TEST(BatteryRam, OnlyBatteryTypesWithRamHaveIt) {
    for (const std::uint8_t type : {0x03, 0x09, 0x1B, 0x1E}) {
        const auto result = make_cartridge(make_image(0x8000, type, 0x00, 0x02));
        ASSERT_NE(result.cartridge, nullptr) << result.error;
        EXPECT_TRUE(result.cartridge->has_battery()) << "type " << int{type};
    }
    for (const std::uint8_t type : {0x02, 0x08, 0x1A, 0x1D}) {
        const auto result = make_cartridge(make_image(0x8000, type, 0x00, 0x02));
        ASSERT_NE(result.cartridge, nullptr) << result.error;
        EXPECT_FALSE(result.cartridge->has_battery()) << "type " << int{type};
    }
    const auto no_ram = make_cartridge(make_image(0x8000, 0x03, 0x00, 0x00));
    ASSERT_NE(no_ram.cartridge, nullptr) << no_ram.error;
    EXPECT_FALSE(no_ram.cartridge->has_battery());
}

TEST(BatteryRam, CoversEveryBankOfTheChip) {
    const auto result = make_cartridge(make_image(0x8000, 0x03, 0x00, 0x03));  // 32 KiB
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    EXPECT_EQ(result.cartridge->ram().size(), 0x8000U);
}

TEST(BatteryRam, ChangedOnlyWhenAWriteChangesAByte) {
    const auto result = make_cartridge(make_image(0x8000, 0x03, 0x00, 0x02));
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    Cartridge& cart = *result.cartridge;
    cart.write_rom(0x0000, 0x0A);  // enable RAM
    cart.write_ram(0xA000, 0x00);  // same as power-on
    EXPECT_FALSE(cart.ram_changed());
    cart.write_ram(0xA000, 0x05);
    EXPECT_TRUE(cart.ram_changed());
    cart.clear_ram_changed();
    EXPECT_FALSE(cart.ram_changed());
    cart.write_ram(0xA000, 0x05);
    EXPECT_FALSE(cart.ram_changed());
}

TEST(BatteryRam, DisabledRamIsNotChanged) {
    const auto result = make_cartridge(make_image(0x8000, 0x03, 0x00, 0x02));
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    result.cartridge->write_ram(0xA000, 0x05);  // RAM not enabled
    EXPECT_FALSE(result.cartridge->ram_changed());
    EXPECT_EQ(result.cartridge->ram()[0], 0x00);
}

TEST(BatteryRam, LoadReplacesTheContentsAndRefusesAWrongSize) {
    const auto result = make_cartridge(make_image(0x8000, 0x03, 0x00, 0x02));
    ASSERT_NE(result.cartridge, nullptr) << result.error;
    Cartridge& cart = *result.cartridge;
    std::vector<std::uint8_t> save(0x2000, 0x00);
    save[0] = 0x11;
    save[0x1FFF] = 0x22;
    EXPECT_FALSE(cart.load_ram(std::vector<std::uint8_t>(0x1FFF, 0x33)));
    EXPECT_EQ(cart.ram()[0], 0x00);
    ASSERT_TRUE(cart.load_ram(save));
    EXPECT_FALSE(cart.ram_changed());
    cart.write_rom(0x0000, 0x0A);
    EXPECT_EQ(cart.read_ram(0xA000), 0x11);
    EXPECT_EQ(cart.read_ram(0xBFFF), 0x22);
}

TEST(MakeCartridge, TooSmallFileIsRejected) {
    const std::vector<std::uint8_t> tiny(0x100, 0x00);
    const auto result = make_cartridge(tiny);
    EXPECT_EQ(result.cartridge, nullptr);
    EXPECT_NE(result.error.find("too small"), std::string::npos) << result.error;
}

TEST(MakeCartridge, SizeThatDisagreesWithTheHeaderIsRejected) {
    const auto result = make_cartridge(make_image(0x8000, 0x01, 0x01, 0x00));
    EXPECT_EQ(result.cartridge, nullptr);
    EXPECT_NE(result.error.find("65536"), std::string::npos) << result.error;
}

TEST(MakeCartridge, UnknownSizeCodeIsRejected) {
    const auto result = make_cartridge(make_image(0x8000, 0x00, 0x0A, 0x00));
    EXPECT_EQ(result.cartridge, nullptr);
    EXPECT_NE(result.error.find("size code"), std::string::npos) << result.error;
}

TEST(MakeCartridge, UnsupportedMapperIsRejectedByName) {
    const auto result = make_cartridge(make_image(0x8000, 0x05, 0x00, 0x00));  // MBC2
    EXPECT_EQ(result.cartridge, nullptr);
    EXPECT_EQ(result.error, "cartridge type 0x05 is not supported yet");
}

TEST(MakeCartridge, RomOnlyWithMoreThan8KiBOfRamIsRejected) {
    const auto result = make_cartridge(make_image(0x8000, 0x08, 0x00, 0x03));
    EXPECT_EQ(result.cartridge, nullptr);
}

TEST(NoMbc, RomIsMappedStraightThrough) {
    const auto rom = make_image(0x8000, 0x00, 0x00, 0x00);
    const NoMbc cart(rom, 0);
    EXPECT_EQ(cart.read_rom(0x0000), 0x00);
    EXPECT_EQ(cart.read_rom(0x4000), 0x01);
    EXPECT_EQ(cart.read_rom(0x0134), 'T');
}

TEST(NoMbc, WritesToTheRomAreaChangeNothing) {
    const auto rom = make_image(0x8000, 0x00, 0x00, 0x00);
    NoMbc cart(rom, 0);
    cart.write_rom(0x2000, 0x05);
    cart.write_rom(0x4000, 0x55);
    EXPECT_EQ(cart.read_rom(0x4000), 0x01);
}

TEST(NoMbc, WithoutRamTheRamAreaReadsFF) {
    const auto rom = make_image(0x8000, 0x00, 0x00, 0x00);
    NoMbc cart(rom, 0);
    cart.write_ram(0xA000, 0x12);
    EXPECT_EQ(cart.read_ram(0xA000), 0xFF);
}

TEST(NoMbc, RamIsAlwaysEnabled) {
    const auto rom = make_image(0x8000, 0x08, 0x00, 0x02);
    NoMbc cart(rom, 0x2000);
    cart.write_ram(0xA000, 0x12);
    cart.write_ram(0xBFFF, 0x34);
    EXPECT_EQ(cart.read_ram(0xA000), 0x12);
    EXPECT_EQ(cart.read_ram(0xBFFF), 0x34);
}

TEST(NoMbc, IsACartridge) {
    const auto rom = make_image(0x8000, 0x00, 0x00, 0x00);
    const NoMbc cart(rom, 0);
    const Cartridge& base = cart;
    EXPECT_EQ(base.read_rom(0x4000), 0x01);
}

}  // namespace
