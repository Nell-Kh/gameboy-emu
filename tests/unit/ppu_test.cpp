#include "core/ppu.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "core/interrupts.h"

namespace {

using core::Ppu;
using Mode = core::Ppu::Mode;

constexpr std::uint8_t kLcdOn = 0x91;
constexpr std::uint8_t kLcdOff = 0x11;
constexpr auto kVBlank = static_cast<std::uint8_t>(core::Interrupt::VBlank);
constexpr auto kStat = static_cast<std::uint8_t>(core::Interrupt::LcdStat);

// Advances the PPU and returns the interrupts it requested.
std::uint8_t advance(Ppu& ppu, std::uint32_t dots) {
    return ppu.tick(dots);
}

void write(Ppu& ppu, std::uint16_t address, std::uint8_t value) {
    static_cast<void>(ppu.write(address, value));
}

std::uint8_t stat_mode(const Ppu& ppu) {
    return ppu.read(Ppu::kStat) & 0x03U;
}

TEST(Ppu, PostBootState) {
    const Ppu ppu;
    EXPECT_EQ(ppu.read(Ppu::kLcdc), 0x91);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0x00);
    EXPECT_EQ(ppu.read(Ppu::kBgp), 0xFC);
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
}

TEST(Ppu, PlainRegistersReadBackWhatWasWritten) {
    Ppu ppu;
    for (const std::uint16_t address :
         {Ppu::kScy, Ppu::kScx, Ppu::kLyc, Ppu::kBgp, Ppu::kObp0, Ppu::kObp1, Ppu::kWy, Ppu::kWx}) {
        write(ppu, address, 0xA5);
        EXPECT_EQ(ppu.read(address), 0xA5) << "address " << address;
    }
}

TEST(Ppu, LineGoesThroughModes2Then3Then0) {
    Ppu ppu;
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
    advance(ppu, 79);
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
    advance(ppu, 1);
    EXPECT_EQ(ppu.mode(), Mode::Drawing);
    advance(ppu, 171);
    EXPECT_EQ(ppu.mode(), Mode::Drawing);
    advance(ppu, 1);
    EXPECT_EQ(ppu.mode(), Mode::HBlank);
    advance(ppu, 456 - 252 - 1);
    EXPECT_EQ(ppu.mode(), Mode::HBlank);
    advance(ppu, 1);
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
}

TEST(Ppu, StatReportsTheModeAndBit7) {
    Ppu ppu;
    EXPECT_EQ(ppu.read(Ppu::kStat), 0x80 | 0x04 | 0x02);  // LY = LYC = 0, mode 2
    advance(ppu, 80);
    EXPECT_EQ(stat_mode(ppu), 3);
}

TEST(Ppu, Lines144To153AreVblank) {
    Ppu ppu;
    advance(ppu, 456 * 144);
    EXPECT_EQ(ppu.mode(), Mode::VBlank);
    EXPECT_EQ(ppu.read(Ppu::kLy), 144);
    advance(ppu, 456 * 10 - 1);
    EXPECT_EQ(ppu.mode(), Mode::VBlank);
    advance(ppu, 1);
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, LyShowsTheNextLineDuringTheLastMachineCycle) {
    Ppu ppu;
    advance(ppu, 451);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    advance(ppu, 1);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
    EXPECT_EQ(ppu.mode(), Mode::HBlank);
}

TEST(Ppu, LyIsReadOnly) {
    Ppu ppu;
    advance(ppu, 456 * 5);
    write(ppu, Ppu::kLy, 0x77);
    EXPECT_EQ(ppu.read(Ppu::kLy), 5);
}

TEST(Ppu, VblankInterruptOncePerFrameAtLine144) {
    Ppu ppu;
    EXPECT_EQ(advance(ppu, 456 * 144 - 1) & kVBlank, 0);
    EXPECT_EQ(advance(ppu, 1) & kVBlank, kVBlank);
    EXPECT_EQ(advance(ppu, 456 * 154 - 1) & kVBlank, 0);
    EXPECT_EQ(advance(ppu, 1) & kVBlank, kVBlank);
}

TEST(Ppu, ScxDelaysTheEndOfMode3) {
    for (std::uint8_t scx = 0; scx < 16; ++scx) {
        Ppu ppu;
        write(ppu, Ppu::kScx, scx);
        advance(ppu, 80);
        EXPECT_EQ(ppu.drawing_dots(), 172U + (scx % 8)) << "SCX " << int{scx};
    }
}

TEST(Ppu, EachObjectOnTheLineLengthensMode3) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0x93);  // objects on
    // Leave the post-boot line so OAM is writable, then place an object on line 1.
    advance(ppu, 252);
    ppu.write_oam(0xFE00, 16 + 1);  // Y: covers line 1
    ppu.write_oam(0xFE01, 8);       // X: screen column 0, BG tile offset 0
    advance(ppu, 456 - 252 + 80);
    // Flat 6, plus max(0, 7 - 0 - 2) = 5 for the first object on its BG tile.
    EXPECT_EQ(ppu.drawing_dots(), 172U + 6 + 5);
}

TEST(Ppu, ObjectAtXZeroCostsEleven) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0x93);
    advance(ppu, 252);
    ppu.write_oam(0xFE00, 16 + 1);
    ppu.write_oam(0xFE01, 0);
    advance(ppu, 456 - 252 + 80);
    EXPECT_EQ(ppu.drawing_dots(), 172U + 11);
}

TEST(Ppu, ObjectsCostNothingWhileObjectsAreOff) {
    Ppu ppu;  // LCDC 0x91: objects off
    advance(ppu, 252);
    ppu.write_oam(0xFE00, 16 + 1);
    ppu.write_oam(0xFE01, 8);
    advance(ppu, 456 - 252 + 80);
    EXPECT_EQ(ppu.drawing_dots(), 172U);
}

TEST(Ppu, OnlyTenObjectsPerLineAreCounted) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0x93);
    advance(ppu, 252);
    for (std::uint16_t i = 0; i < 12; ++i) {
        ppu.write_oam(static_cast<std::uint16_t>(0xFE00 + i * 4), 16 + 1);
        ppu.write_oam(static_cast<std::uint16_t>(0xFE01 + i * 4), 0);  // 11 dots each
    }
    advance(ppu, 456 - 252 + 80);
    EXPECT_EQ(ppu.drawing_dots(), 172U + 10 * 11);
}

TEST(Ppu, WindowAboveWyCostsNothing) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0xB1);
    write(ppu, Ppu::kWy, 2);
    write(ppu, Ppu::kWx, 7);
    advance(ppu, 456 + 80);  // line 1
    EXPECT_EQ(ppu.drawing_dots(), 172U);
}

TEST(Ppu, VisibleWindowLengthensMode3BySix) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0xB1);  // window on
    write(ppu, Ppu::kWy, 1);
    write(ppu, Ppu::kWx, 7);
    advance(ppu, 456 + 80);  // line 1, where LY = WY
    EXPECT_EQ(ppu.drawing_dots(), 172U + 6);
}

TEST(Ppu, VramIsBlockedOnlyDuringMode3) {
    Ppu ppu;
    ppu.write_vram(0x8000, 0x12);
    EXPECT_EQ(ppu.read_vram(0x8000), 0x12);  // mode 2
    advance(ppu, 80);
    EXPECT_EQ(ppu.read_vram(0x8000), 0xFF);  // mode 3
    ppu.write_vram(0x8000, 0x34);
    advance(ppu, 172);
    EXPECT_EQ(ppu.read_vram(0x8000), 0x12);  // mode 0: the write was ignored
}

TEST(Ppu, OamIsBlockedDuringModes2And3) {
    Ppu ppu;
    ppu.write_oam(0xFE00, 0x12);
    EXPECT_EQ(ppu.read_oam(0xFE00), 0xFF);  // mode 2
    advance(ppu, 80);
    EXPECT_EQ(ppu.read_oam(0xFE00), 0xFF);  // mode 3
    advance(ppu, 172);
    EXPECT_EQ(ppu.read_oam(0xFE00), 0x00);  // mode 0: the write in mode 2 was ignored
    ppu.write_oam(0xFE00, 0x56);
    EXPECT_EQ(ppu.read_oam(0xFE00), 0x56);
}

TEST(Ppu, EverythingIsAccessibleWithTheLcdOff) {
    Ppu ppu;
    advance(ppu, 100);
    write(ppu, Ppu::kLcdc, kLcdOff);
    ppu.write_vram(0x9FFF, 0x11);
    ppu.write_oam(0xFE9F, 0x22);
    EXPECT_EQ(ppu.read_vram(0x9FFF), 0x11);
    EXPECT_EQ(ppu.read_oam(0xFE9F), 0x22);
}

TEST(Ppu, DmaAccessIgnoresTheMode) {
    Ppu ppu;
    advance(ppu, 80);  // mode 3
    ppu.dma_write_oam(0x10, 0x77);
    EXPECT_EQ(ppu.read_oam(0xFE10), 0xFF);
    advance(ppu, 172);
    EXPECT_EQ(ppu.read_oam(0xFE10), 0x77);
}

TEST(Ppu, LcdOffResetsLyAndModeAndStopsTheClock) {
    Ppu ppu;
    advance(ppu, 456 * 10 + 100);
    write(ppu, Ppu::kLcdc, kLcdOff);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
    EXPECT_EQ(stat_mode(ppu), 0);
    EXPECT_EQ(advance(ppu, 456 * 200), 0);
    EXPECT_EQ(ppu.read(Ppu::kLy), 0);
}

TEST(Ppu, FirstLineAfterLcdOnStartsInMode0) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, kLcdOff);
    write(ppu, Ppu::kLcdc, kLcdOn);
    EXPECT_EQ(ppu.mode(), Mode::HBlank);
    advance(ppu, 80);
    EXPECT_EQ(ppu.mode(), Mode::Drawing);
    advance(ppu, 456 - 80);
    EXPECT_EQ(ppu.mode(), Mode::OamScan);
    EXPECT_EQ(ppu.read(Ppu::kLy), 1);
}

TEST(Ppu, LycEqualFlagFollowsLy) {
    Ppu ppu;
    write(ppu, Ppu::kLyc, 2);
    EXPECT_EQ(ppu.read(Ppu::kStat) & 0x04, 0);
    advance(ppu, 456 * 2);
    EXPECT_EQ(ppu.read(Ppu::kStat) & 0x04, 0x04);
    advance(ppu, 456);
    EXPECT_EQ(ppu.read(Ppu::kStat) & 0x04, 0);
}

TEST(Ppu, LycEqualFlagIsKeptWhileTheLcdIsOff) {
    Ppu ppu;
    write(ppu, Ppu::kLyc, 0x90);
    advance(ppu, 456 * 144);
    write(ppu, Ppu::kLcdc, kLcdOff);
    write(ppu, Ppu::kLyc, 0x01);
    EXPECT_EQ(ppu.read(Ppu::kStat), 0x84);
    write(ppu, Ppu::kLcdc, kLcdOn);
    EXPECT_EQ(ppu.read(Ppu::kStat) & 0x04, 0);
}

TEST(Ppu, StatInterruptOnEachSelectedCondition) {
    {
        Ppu ppu;
        write(ppu, Ppu::kStat, 0x08);  // mode 0
        EXPECT_EQ(advance(ppu, 251) & kStat, 0);
        EXPECT_EQ(advance(ppu, 1) & kStat, kStat);
    }
    {
        Ppu ppu;
        write(ppu, Ppu::kStat, 0x10);  // mode 1
        EXPECT_EQ(advance(ppu, 456 * 144 - 1) & kStat, 0);
        EXPECT_EQ(advance(ppu, 1) & kStat, kStat);
    }
    {
        Ppu ppu;
        write(ppu, Ppu::kStat, 0x20);  // mode 2
        EXPECT_EQ(advance(ppu, 455) & kStat, 0);
        EXPECT_EQ(advance(ppu, 1) & kStat, kStat);
    }
    {
        Ppu ppu;
        write(ppu, Ppu::kLyc, 3);
        write(ppu, Ppu::kStat, 0x40);  // LYC = LY
        EXPECT_EQ(advance(ppu, 456 * 3 - 1) & kStat, 0);
        EXPECT_EQ(advance(ppu, 1) & kStat, kStat);
    }
}

TEST(Ppu, Mode2SelectAlsoFiresWhenVblankStarts) {
    Ppu ppu;
    advance(ppu, 456 * 143 + 300);  // line 143, mode 0
    write(ppu, Ppu::kStat, 0x20);
    const std::uint8_t at_vblank = advance(ppu, 156);
    EXPECT_EQ(at_vblank & kStat, kStat);
    EXPECT_EQ(at_vblank & kVBlank, kVBlank);
}

TEST(Ppu, StatLineOnlyFiresOnARisingEdge) {
    // Mode 0 and mode 1 both selected: line 143's HBlank runs straight into
    // VBlank, so the line never falls and VBlank adds no STAT interrupt.
    Ppu ppu;
    advance(ppu, 456 * 143 + 300);
    write(ppu, Ppu::kStat, 0x18);
    EXPECT_EQ(advance(ppu, 156) & kStat, 0);
}

TEST(Ppu, LycHeldAcrossMode3BlocksLaterStatInterrupts) {
    Ppu ppu;
    write(ppu, Ppu::kLyc, 5);
    advance(ppu, 456 * 5);
    write(ppu, Ppu::kStat, 0x78);  // everything; LYC = LY already holds
    EXPECT_EQ(advance(ppu, 455) & kStat, 0);
}

TEST(Ppu, SelectingAConditionThatHoldsFiresAtOnce) {
    Ppu ppu;
    advance(ppu, 456 * 144 + 10);
    EXPECT_EQ(ppu.write(Ppu::kStat, 0x10) & kStat, kStat);
}

TEST(Ppu, NoInterruptsWhileTheLcdIsOff) {
    Ppu ppu;
    write(ppu, Ppu::kStat, 0x78);
    write(ppu, Ppu::kLcdc, kLcdOff);
    EXPECT_EQ(advance(ppu, 456 * 300), 0);
}

}  // namespace
