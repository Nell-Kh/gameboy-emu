#include "core/cartridge/cartridge.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/cartridge/no_mbc.h"

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
    const auto result = make_cartridge(make_image(0x8000, 0x13, 0x00, 0x00));
    EXPECT_EQ(result.cartridge, nullptr);
    EXPECT_EQ(result.error, "cartridge type 0x13 is not supported yet");
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
