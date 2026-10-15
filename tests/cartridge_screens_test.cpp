// Cartridge test ROMs checked by screenshot (docs/cartridge-notes.md,
// section 8), in shades against the reference images in c-sp's collection.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "screenshot.h"

namespace {

using core::GameBoy;
using Button = core::Joypad::Button;

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

// rtc3test (aaaaaa123456789) checks the MBC3 clock against the Game Boy's own
// timer. Its subtest is picked on a menu, so each test plays a fixed input
// script: every button is held for kHoldFrames frames, kGapFrames apart, from
// kFirstPress on (the menu is up well before then). Then it runs for the
// subtest's duration from c-sp's how-to plus kSpareSeconds, and the screen is
// compared with c-sp's reference.
constexpr int kFirstPress = 60;
constexpr int kHoldFrames = 5;
constexpr int kGapFrames = 20;
constexpr std::uint64_t kSpareSeconds = 2;

void run_rtc3test(std::initializer_list<Button> buttons, std::uint64_t seconds,
                  const std::string& reference) {
    const std::string dir = std::string(GB_CSP_DIR) + "/rtc3test";
    const std::vector<std::uint8_t> rom = test::read_file(dir + "/rtc3test.gb");
    ASSERT_FALSE(rom.empty()) << "cannot read " << dir << "/rtc3test.gb";
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");

    int frame = 0;
    const auto run_frames = [&gb, &frame](int count) {
        for (int i = 0; i < count; ++i, ++frame) {
            gb.run_frame();
        }
    };
    run_frames(kFirstPress);
    for (const Button button : buttons) {
        gb.set_button(button, true);
        run_frames(kHoldFrames);
        gb.set_button(button, false);
        run_frames(kGapFrames - kHoldFrames);
    }
    const std::uint64_t end = gb.cycles() + (seconds + kSpareSeconds) * GameBoy::kTicksPerSecond;
    while (gb.cycles() < end && !gb.cpu().locked()) {
        gb.run_frame();
    }
    EXPECT_FALSE(gb.cpu().locked());
    EXPECT_TRUE(test::matches_screenshot(gb.frame(), dir + "/" + reference));
}

TEST(Rtc3Test, BasicTests) {
    run_rtc3test({Button::A}, 13, "rtc3test-basic-tests-dmg.png");
}

TEST(Rtc3Test, RangeTests) {
    run_rtc3test({Button::Down, Button::A}, 8, "rtc3test-range-tests-dmg.png");
}

TEST(Rtc3Test, SubSecondWrites) {
    run_rtc3test({Button::Down, Button::Down, Button::A}, 26, "rtc3test-sub-second-writes-dmg.png");
}

}  // namespace
