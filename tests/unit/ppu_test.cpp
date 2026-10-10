#include "core/ppu.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using core::Ppu;

constexpr std::uint8_t kLcdOn = 0x91;

// Advances the clock, ignoring whether a VBlank started.
void advance(Ppu& ppu, std::uint32_t ticks) {
    static_cast<void>(ppu.tick(ticks));
}
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
    advance(ppu, Ppu::kTicksPerLine - 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    advance(ppu, 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
    advance(ppu, Ppu::kTicksPerLine * 142);
    EXPECT_EQ(ppu.read(Ppu::kLy), 143);
}

TEST(Ppu, LyCountsThroughVblankAndWrapsAfterLine153) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 153);
    EXPECT_EQ(ppu.read(Ppu::kLy), 153);
    advance(ppu, Ppu::kTicksPerLine);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, LyIsReadOnly) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 5);
    ppu.write(Ppu::kLy, 0x77);
    EXPECT_EQ(ppu.read(Ppu::kLy), 5);
}

TEST(Ppu, TurningTheLcdOffResetsLyAndStopsIt) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 10);
    ppu.write(Ppu::kLcdc, kLcdOff);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    advance(ppu, Ppu::kTicksPerLine * 10);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, TurningTheLcdBackOnStartsFromLineZero) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 10 + 100);
    ppu.write(Ppu::kLcdc, kLcdOff);
    ppu.write(Ppu::kLcdc, kLcdOn);
    advance(ppu, Ppu::kTicksPerLine - 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    advance(ppu, 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
}

TEST(Ppu, WritingLcdcWhileOnKeepsTheLinePosition) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 7);
    ppu.write(Ppu::kLcdc, 0x93);
    EXPECT_EQ(ppu.read(Ppu::kLy), 7);
}

TEST(Ppu, VblankStartsExactlyAtTheFirstTickOfLine144) {
    Ppu ppu;
    advance(ppu, Ppu::kTicksPerLine * 144 - 4);
    EXPECT_FALSE(ppu.tick(3));
    EXPECT_TRUE(ppu.tick(1));
    EXPECT_EQ(ppu.read(Ppu::kLy), 144);
}

TEST(Ppu, VblankIsReportedOncePerFrame) {
    Ppu ppu;
    int starts = 0;
    for (std::uint32_t tick = 0; tick < Ppu::kTicksPerLine * Ppu::kLinesPerFrame * 3; tick += 4) {
        starts += ppu.tick(4) ? 1 : 0;
    }
    EXPECT_EQ(starts, 3);
}

TEST(Ppu, NoVblankWhileTheLcdIsOff) {
    Ppu ppu;
    ppu.write(Ppu::kLcdc, kLcdOff);
    EXPECT_FALSE(ppu.tick(Ppu::kTicksPerLine * Ppu::kLinesPerFrame * 2));
}

}  // namespace
