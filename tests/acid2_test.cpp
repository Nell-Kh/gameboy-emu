// dmg-acid2 (Matt Currie): one screen that exercises the PPU's drawing rules.
//
// The ROM draws a face and then executes LD B, B. At that point the frame must
// match the reference screenshot pixel for pixel, in shades (screenshot.h).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "screenshot.h"

namespace {

using core::GameBoy;

constexpr std::uint8_t kBreakpoint = 0x40;  // LD B, B
// The ROM finishes in about a third of an emulated second.
constexpr std::uint64_t kMaxTicks = 10 * GameBoy::kTicksPerSecond;

TEST(DmgAcid2, FrameMatchesTheReference) {
    const std::string dir = std::string(GB_CSP_DIR) + "/dmg-acid2";
    const std::vector<std::uint8_t> rom = test::read_file(dir + "/dmg-acid2.gb");
    ASSERT_FALSE(rom.empty()) << "cannot read " << dir << "/dmg-acid2.gb";

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
    EXPECT_TRUE(test::matches_screenshot(gb.frame(), dir + "/dmg-acid2-dmg.png"));
}

}  // namespace
