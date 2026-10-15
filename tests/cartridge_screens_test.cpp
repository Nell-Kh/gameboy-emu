// Cartridge test ROMs checked by screenshot (docs/cartridge-notes.md,
// section 8), in shades against the reference images in c-sp's collection.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "screenshot.h"

namespace {

using core::GameBoy;

// mbc3-tester (HyperHacker; disassembled by Matt Currie): a 4 MiB type 0x11
// ROM that checks all 256 ROM banks and ticks them off on screen. c-sp's how-to:
// it loops forever and the result can be checked after the first 40 frames.
TEST(Mbc3Tester, AllBanksAreTickedOff) {
    const std::string dir = std::string(GB_CSP_DIR) + "/mbc3-tester";
    const std::vector<std::uint8_t> rom = test::read_file(dir + "/mbc3-tester.gb");
    ASSERT_FALSE(rom.empty()) << "cannot read " << dir << "/mbc3-tester.gb";
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    constexpr int kFrames = 60;
    for (int frame = 0; frame < kFrames; ++frame) {
        gb.run_frame();
    }
    EXPECT_FALSE(gb.cpu().locked());
    EXPECT_TRUE(test::matches_screenshot(gb.frame(), dir + "/mbc3-tester-dmg.png"));
}

}  // namespace
