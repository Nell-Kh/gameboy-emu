#include "core/ppu.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using core::Ppu;

constexpr std::uint8_t kLcdOn = 0x91;
constexpr std::uint8_t kLcdOff = 0x11;

TEST(Ppu, LcdIsOnAfterBoot) {
    const Ppu ppu;
    EXPECT_EQ(ppu.read(Ppu::kLcdc), 0x91);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0x00);
}

TEST(Ppu, LcdcReadsBackWhatWasWritten) {
    Ppu ppu;
    ppu.write(Ppu::kLcdc, 0xE3);
    EXPECT_EQ(ppu.read(Ppu::kLcdc), 0xE3);
}

TEST(Ppu, LyAdvancesOneLineEvery456Ticks) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine - 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    ppu.tick(1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
    ppu.tick(Ppu::kTicksPerLine * 142);
    EXPECT_EQ(ppu.read(Ppu::kLy), 143);
}

TEST(Ppu, LyCountsThroughVblankAndWrapsAfterLine153) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine * 153);
    EXPECT_EQ(ppu.read(Ppu::kLy), 153);
    ppu.tick(Ppu::kTicksPerLine);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, LyIsReadOnly) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine * 5);
    ppu.write(Ppu::kLy, 0x77);
    EXPECT_EQ(ppu.read(Ppu::kLy), 5);
}

TEST(Ppu, TurningTheLcdOffResetsLyAndStopsIt) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine * 10);
    ppu.write(Ppu::kLcdc, kLcdOff);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    ppu.tick(Ppu::kTicksPerLine * 10);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, TurningTheLcdBackOnStartsFromLineZero) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine * 10 + 100);
    ppu.write(Ppu::kLcdc, kLcdOff);
    ppu.write(Ppu::kLcdc, kLcdOn);
    ppu.tick(Ppu::kTicksPerLine - 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    ppu.tick(1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
}

TEST(Ppu, WritingLcdcWhileOnKeepsTheLinePosition) {
    Ppu ppu;
    ppu.tick(Ppu::kTicksPerLine * 7);
    ppu.write(Ppu::kLcdc, 0x93);
    EXPECT_EQ(ppu.read(Ppu::kLy), 7);
}

}  // namespace
