#include "core/gameboy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "test_rom.h"

namespace {

using core::GameBoy;
using test::kEntry;

// A program that sends "OK" over the serial port and then loops forever.
std::vector<std::uint8_t> ok_rom() {
    return test::make_rom({
        0x3E, 'O',   // LD A, 'O'
        0xE0, 0x01,  // LDH [0xFF01], A   ; SB = byte to send
        0x3E, 0x81,  // LD A, 0x81
        0xE0, 0x02,  // LDH [0xFF02], A   ; SC = start transfer
        0x3E, 'K',   // LD A, 'K'
        0xE0, 0x01,  // LDH [0xFF01], A
        0x3E, 0x81,  // LD A, 0x81
        0xE0, 0x02,  // LDH [0xFF02], A
        0x18, 0xFE,  // JR -2             ; loop here
    });
}

TEST(GameBoy, StartsAtTheCartridgeEntryPointWithNoTimeElapsed) {
    const GameBoy gb;
    EXPECT_EQ(gb.cpu().registers().pc, kEntry);
    EXPECT_EQ(gb.cycles(), 0U);
    EXPECT_EQ(gb.serial_output(), "");
}

TEST(GameBoy, StepRunsOneInstructionOfTheLoadedRom) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");

    EXPECT_EQ(gb.step(), 8U);
    EXPECT_EQ(gb.cpu().registers().a, 'O');
    EXPECT_EQ(gb.cycles(), 8U);
}

TEST(GameBoy, AProgramCanPrintOverTheSerialPort) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");

    gb.run_for(1000);
    EXPECT_EQ(gb.serial_output(), "OK");
}

TEST(GameBoy, RunForStopsOnAnInstructionBoundaryAtOrAfterTheTarget) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");

    // The first instructions take 8, 12, 8, 12 ticks: boundaries at 8, 20, 28, 40.
    EXPECT_EQ(gb.run_for(10), 20U);
    EXPECT_EQ(gb.cycles(), 20U);
    EXPECT_EQ(gb.run_for(8), 8U);
    EXPECT_EQ(gb.cycles(), 28U);
}

TEST(GameBoy, RunForZeroDoesNothing) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");
    EXPECT_EQ(gb.run_for(0), 0U);
    EXPECT_EQ(gb.cycles(), 0U);
}

TEST(GameBoy, OneEmulatedSecondIsTheClockRate) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");

    const std::uint64_t ran = gb.run_for(GameBoy::kTicksPerSecond);
    EXPECT_GE(ran, GameBoy::kTicksPerSecond);
    EXPECT_LT(ran, GameBoy::kTicksPerSecond + 24);
}

TEST(GameBoy, LoadRomRejectsAnUnusableImageAndKeepsTheOldCartridge) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");
    const std::vector<std::uint8_t> tiny(16, 0x00);
    EXPECT_NE(gb.load_rom(tiny), "");
    EXPECT_EQ(gb.bus().read8(kEntry), 0x3E);
}

TEST(GameBoy, AButtonPressEndsStop) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(test::make_rom({
                  0x10, 0x00,  // STOP
                  0x00,        // NOP
              })),
              "");
    gb.step();
    ASSERT_TRUE(gb.cpu().stopped());
    gb.run_for(GameBoy::kTicksPerSecond / 100);
    EXPECT_TRUE(gb.cpu().stopped());
    EXPECT_EQ(gb.cpu().registers().pc, kEntry + 2);

    gb.set_button(core::Joypad::Button::A, true);  // both groups are selected after boot
    gb.step();
    EXPECT_FALSE(gb.cpu().stopped());
    EXPECT_EQ(gb.cpu().registers().pc, kEntry + 3);
}

TEST(GameBoy, RunFrameStopsWhenLine144Starts) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(test::make_rom({0x18, 0xFE})), "");  // JR -2: loop
    constexpr std::uint64_t kLongestInstruction = 24;

    // After boot the PPU is at the start of line 0, 144 lines before VBlank.
    constexpr std::uint64_t kTicksToVBlank = std::uint64_t{144} * 456;
    const std::uint64_t first = gb.run_frame();
    EXPECT_GE(first, kTicksToVBlank);
    EXPECT_LT(first, kTicksToVBlank + kLongestInstruction);
    EXPECT_EQ(gb.bus().read8(0xFF44), 144);

    const std::uint64_t second = gb.run_frame();
    EXPECT_GT(second, GameBoy::kTicksPerFrame - kLongestInstruction);
    EXPECT_LT(second, GameBoy::kTicksPerFrame + kLongestInstruction);
    EXPECT_EQ(gb.bus().read8(0xFF44), 144);
}

TEST(GameBoy, RunFrameWithTheLcdOffRunsOneFrameOfTicks) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(test::make_rom({
                  0xAF,        // XOR A
                  0xE0, 0x40,  // LDH [0xFF40], A   ; LCD off
                  0x18, 0xFE,  // JR -2
              })),
              "");
    gb.step();
    gb.step();
    const std::uint64_t ran = gb.run_frame();
    EXPECT_GE(ran, GameBoy::kTicksPerFrame);
    EXPECT_LT(ran, GameBoy::kTicksPerFrame + 24);
}

TEST(GameBoy, BusIsReadableFromOutside) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(ok_rom()), "");
    EXPECT_EQ(gb.bus().read8(kEntry), 0x3E);
}

}  // namespace
