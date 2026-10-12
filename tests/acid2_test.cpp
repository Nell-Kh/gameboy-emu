// dmg-acid2 (Matt Currie): one screen that exercises the PPU's drawing rules.
//
// The ROM draws a face and then executes LD B, B. At that point the frame must
// match the reference screenshot pixel for pixel. The comparison is in shades
// 0-3, so it never depends on the colours a frontend picks for them.

#include <gtest/gtest.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "core/gameboy.h"
#include "core/ppu.h"

namespace {

using core::GameBoy;
using Frame = core::Ppu::Frame;

constexpr std::uint8_t kBreakpoint = 0x40;  // LD B, B
// The ROM finishes in about a third of an emulated second.
constexpr std::uint64_t kMaxTicks = 10 * GameBoy::kTicksPerSecond;

// The grey levels the reference screenshots use for shades 0-3 (c-sp's
// game-boy-test-roms, dmg-acid2 how-to).
constexpr std::array<std::uint8_t, 4> kGrey = {0xFF, 0xAA, 0x55, 0x00};

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Reads a 160x144 PNG that only uses the four grey levels into shades.
// Returns an error message, or an empty string on success.
std::string read_reference(const std::string& path, Frame& shades) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.c_str(), &width, &height, &channels, 1), &stbi_image_free);
    if (!pixels) {
        return "cannot read " + path + ": " + stbi_failure_reason();
    }
    if (width != GameBoy::kScreenWidth || height != GameBoy::kScreenHeight) {
        return path + " is " + std::to_string(width) + "x" + std::to_string(height);
    }
    const std::span<const stbi_uc> grey(pixels.get(), shades.size());
    for (std::size_t i = 0; i < shades.size(); ++i) {
        const auto* found = std::find(kGrey.begin(), kGrey.end(), grey[i]);
        if (found == kGrey.end()) {
            return path + ": pixel " + std::to_string(i) + " has grey level " +
                   std::to_string(grey[i]) + ", not one of the four shades";
        }
        shades.at(i) = static_cast<std::uint8_t>(found - kGrey.begin());
    }
    return "";
}

bool write_png(const std::string& path, const Frame& shades) {
    std::vector<std::uint8_t> grey(shades.size());
    std::transform(shades.begin(), shades.end(), grey.begin(),
                   [](std::uint8_t shade) { return kGrey.at(shade & 3U); });
    return stbi_write_png(path.c_str(), GameBoy::kScreenWidth, GameBoy::kScreenHeight, 1,
                          grey.data(), GameBoy::kScreenWidth) != 0;
}

TEST(DmgAcid2, FrameMatchesTheReference) {
    const std::string dir = GB_ACID2_DIR;
    const std::vector<std::uint8_t> rom = read_file(dir + "/dmg-acid2.gb");
    ASSERT_FALSE(rom.empty()) << "cannot read " << dir << "/dmg-acid2.gb";

    Frame expected{};
    const std::string error = read_reference(dir + "/dmg-acid2-dmg.png", expected);
    ASSERT_EQ(error, "");

    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    bool finished = false;
    while (!finished && gb.cycles() < kMaxTicks && !gb.cpu().locked()) {
        finished = !gb.cpu().halted() && gb.bus().read8(gb.cpu().registers().pc) == kBreakpoint;
        if (!finished) {
            gb.step();
        }
    }
    ASSERT_TRUE(finished) << "no LD B, B after " << gb.cycles() << " ticks";

    // Written when the frame differs; removed when it matches, so a stale one
    // from an earlier failure never lingers.
    const std::string actual_path = dir + "/dmg-acid2-dmg-actual.png";
    const Frame& actual = gb.frame();
    const auto [first_actual, first_expected] =
        std::mismatch(actual.begin(), actual.end(), expected.begin());
    if (first_actual == actual.end()) {
        std::error_code ignored;
        std::filesystem::remove(actual_path, ignored);
        return;
    }
    const auto index = static_cast<std::size_t>(first_actual - actual.begin());
    const auto differing = std::inner_product(
        actual.begin(), actual.end(), expected.begin(), std::size_t{0}, std::plus<>(),
        [](std::uint8_t a, std::uint8_t b) { return std::size_t{a != b ? 1U : 0U}; });
    const bool written = write_png(actual_path, actual);
    ADD_FAILURE() << differing << " pixels differ. First at (x " << index % GameBoy::kScreenWidth
                  << ", y " << index / GameBoy::kScreenWidth << "): expected shade "
                  << int{*first_expected} << ", got " << int{*first_actual} << "\n"
                  << (written ? "Frame written to " : "Could not write ") << actual_path;
}

}  // namespace
