// What the PPU draws. Each test sets up VRAM with the LCD off (so every
// address is writable), switches the LCD on and runs to the line it checks.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include "core/ppu.h"

namespace {

using core::Ppu;

constexpr std::uint8_t kLcdOff = 0x00;
// LCD on, background on, $8000 tile data, $9800 map.
constexpr std::uint8_t kLcdBg8000 = 0x91;
constexpr std::uint8_t kIdentityPalette = 0xE4;  // index n -> shade n
// As above, plus the window on with the $9C00 map.
constexpr std::uint8_t kLcdBgWindow9c00 = 0xF1;

class PpuRender : public testing::Test {
protected:
    void SetUp() override {
        set(Ppu::kLcdc, kLcdOff);
        set(Ppu::kBgp, kIdentityPalette);
    }

    void set(std::uint16_t address, std::uint8_t value) {
        static_cast<void>(ppu.write(address, value));
    }

    // Writes one 8x8 tile whose every row is `lo`, `hi`.
    void fill_tile(std::uint16_t address, std::uint8_t lo, std::uint8_t hi) {
        for (std::uint16_t row = 0; row < 8; ++row) {
            ppu.write_vram(static_cast<std::uint16_t>(address + row * 2), lo);
            ppu.write_vram(static_cast<std::uint16_t>(address + row * 2 + 1), hi);
        }
    }

    // Switches the LCD on with `lcdc` and runs until line `line` is drawn.
    void draw_until_line(std::uint8_t lcdc, std::uint32_t line) {
        set(Ppu::kLcdc, lcdc);
        static_cast<void>(ppu.tick(line * Ppu::kTicksPerLine + Ppu::kOamScanDots));
    }

    [[nodiscard]] std::uint8_t pixel(std::uint32_t x, std::uint32_t y) const {
        return ppu.frame().at(y * Ppu::kScreenWidth + x);
    }

    Ppu ppu;
};

TEST_F(PpuRender, EmptyVramDrawsColour0) {
    draw_until_line(kLcdBg8000, 0);
    for (std::uint32_t x = 0; x < Ppu::kScreenWidth; ++x) {
        EXPECT_EQ(pixel(x, 0), 0) << "x " << x;
    }
}

TEST_F(PpuRender, TileRowBitsGiveTheColourIndex) {
    // Tile 0 at $8000: lo = 0b01010101, hi = 0b00110011 -> indices 0,1,2,3,0,1,2,3.
    fill_tile(0x8000, 0x55, 0x33);
    draw_until_line(kLcdBg8000, 0);
    const std::initializer_list<std::uint8_t> expected = {0, 1, 2, 3, 0, 1, 2, 3};
    std::uint32_t x = 0;
    for (const std::uint8_t index : expected) {
        EXPECT_EQ(pixel(x, 0), index) << "x " << x;
        ++x;
    }
    // The map is all tile 0, so the pattern repeats across the line.
    EXPECT_EQ(pixel(8 + 3, 0), 3);
}

TEST_F(PpuRender, BgpMapsColourIndicesToShades) {
    fill_tile(0x8000, 0x55, 0x33);
    set(Ppu::kBgp, 0x1B);  // 0->3, 1->2, 2->1, 3->0
    draw_until_line(kLcdBg8000, 0);
    EXPECT_EQ(pixel(0, 0), 3);
    EXPECT_EQ(pixel(1, 0), 2);
    EXPECT_EQ(pixel(2, 0), 1);
    EXPECT_EQ(pixel(3, 0), 0);
}

TEST_F(PpuRender, MapEntriesSelectTiles) {
    fill_tile(0x8010, 0xFF, 0xFF);  // tile 1: all index 3
    ppu.write_vram(0x9801, 1);      // second tile of the first map row
    draw_until_line(kLcdBg8000, 0);
    EXPECT_EQ(pixel(7, 0), 0);
    EXPECT_EQ(pixel(8, 0), 3);
    EXPECT_EQ(pixel(15, 0), 3);
    EXPECT_EQ(pixel(16, 0), 0);
}

TEST_F(PpuRender, EachLineUsesItsOwnTileRow) {
    // Tile 0 with only row 3 set to index 1.
    ppu.write_vram(0x8000 + 3 * 2, 0xFF);
    draw_until_line(kLcdBg8000, 3);
    EXPECT_EQ(pixel(0, 2), 0);
    EXPECT_EQ(pixel(0, 3), 1);
}

TEST_F(PpuRender, MapRowChangesEveryEightLines) {
    fill_tile(0x8010, 0xFF, 0x00);   // tile 1: index 1
    ppu.write_vram(0x9800 + 32, 1);  // map row 1, column 0
    draw_until_line(kLcdBg8000, 8);
    EXPECT_EQ(pixel(0, 7), 0);
    EXPECT_EQ(pixel(0, 8), 1);
}

TEST_F(PpuRender, SignedTileDataPutsIndex0At9000And80At8800) {
    fill_tile(0x9000, 0xFF, 0x00);  // index 0 in the signed mode: index 1
    fill_tile(0x8800, 0x00, 0xFF);  // index 0x80: index 2
    fill_tile(0x8000, 0xFF, 0xFF);  // unsigned tile 0: must not be used
    ppu.write_vram(0x9801, 0x80);
    draw_until_line(0x81, 0);  // LCD on, background on, $8800 tile data
    EXPECT_EQ(pixel(0, 0), 1);
    EXPECT_EQ(pixel(8, 0), 2);
}

TEST_F(PpuRender, Lcdc3SelectsThe9c00Map) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    draw_until_line(0x99, 0);  // + bit 3
    EXPECT_EQ(pixel(0, 0), 3);
}

TEST_F(PpuRender, ScxAndScyScrollTheBackground) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9800 + 32 * 2 + 3, 1);  // map row 2, column 3: pixels (24-31, 16-23)
    set(Ppu::kScx, 20);
    set(Ppu::kScy, 10);
    draw_until_line(kLcdBg8000, 6);  // background y = 16
    EXPECT_EQ(pixel(3, 6), 0);       // background x 23
    EXPECT_EQ(pixel(4, 6), 3);       // background x 24
    EXPECT_EQ(pixel(11, 6), 3);
    EXPECT_EQ(pixel(12, 6), 0);
}

TEST_F(PpuRender, ScrollingWrapsAround256) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9800, 1);  // map row 0, column 0
    set(Ppu::kScx, 250);
    set(Ppu::kScy, 252);
    draw_until_line(kLcdBg8000, 4);  // background y = 0
    EXPECT_EQ(pixel(5, 4), 0);       // background x 255
    EXPECT_EQ(pixel(6, 4), 3);       // background x 0
}

TEST_F(PpuRender, BackgroundOffDrawsBgpColour0) {
    fill_tile(0x8000, 0xFF, 0xFF);
    set(Ppu::kBgp, 0xE6);      // colour 0 -> shade 2
    draw_until_line(0x90, 0);  // LCD on, bit 0 clear
    EXPECT_EQ(pixel(0, 0), 2);
    EXPECT_EQ(pixel(159, 0), 2);
}

TEST_F(PpuRender, LinesAreDrawnWhenMode3Starts) {
    fill_tile(0x8000, 0xFF, 0xFF);
    set(Ppu::kLcdc, kLcdBg8000);
    static_cast<void>(ppu.tick(Ppu::kOamScanDots - 1));
    EXPECT_EQ(pixel(0, 0), 0);
    static_cast<void>(ppu.tick(1));
    EXPECT_EQ(pixel(0, 0), 3);
}

TEST_F(PpuRender, RegisterChangesDuringMode3ApplyFromTheNextLine) {
    fill_tile(0x8000, 0xFF, 0xFF);
    draw_until_line(kLcdBg8000, 0);
    set(Ppu::kBgp, 0x00);  // everything white, written during line 0's mode 3
    EXPECT_EQ(pixel(0, 0), 3);
    static_cast<void>(ppu.tick(Ppu::kTicksPerLine));
    EXPECT_EQ(pixel(0, 1), 0);
}

// The window. These use the $9C00 map for it, so the background (on $9800,
// all tile 0, all colour 0) shows wherever the window does not.

TEST_F(PpuRender, WindowStartsAtWxMinus7OnLineWy) {
    fill_tile(0x8010, 0xFF, 0xFF);  // tile 1: index 3
    for (std::uint16_t i = 0; i < 32 * 32; ++i) {
        ppu.write_vram(static_cast<std::uint16_t>(0x9C00 + i), 1);
    }
    set(Ppu::kWx, 47);
    set(Ppu::kWy, 10);
    draw_until_line(kLcdBgWindow9c00, 10);
    EXPECT_EQ(pixel(40, 9), 0);
    EXPECT_EQ(pixel(39, 10), 0);
    EXPECT_EQ(pixel(40, 10), 3);
    EXPECT_EQ(pixel(159, 10), 3);
}

TEST_F(PpuRender, WindowAtWy0ShowsOnTheFirstLineAfterLcdOn) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    set(Ppu::kWy, 0);
    draw_until_line(kLcdBgWindow9c00, 0);
    EXPECT_EQ(pixel(0, 0), 3);
}

TEST_F(PpuRender, WindowRowsCountFromWy) {
    ppu.write_vram(0x8010, 0xFF);  // tile 1, row 0: index 1
    ppu.write_vram(0x8013, 0xFF);  // tile 1, row 1: index 2
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    set(Ppu::kWy, 10);
    draw_until_line(kLcdBgWindow9c00, 11);
    EXPECT_EQ(pixel(0, 10), 1);
    EXPECT_EQ(pixel(0, 11), 2);
}

TEST_F(PpuRender, WindowIgnoresScx) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    set(Ppu::kScx, 3);
    set(Ppu::kScy, 5);
    draw_until_line(kLcdBgWindow9c00, 0);
    EXPECT_EQ(pixel(0, 0), 3);
    EXPECT_EQ(pixel(7, 0), 3);
    EXPECT_EQ(pixel(8, 0), 0);
}

TEST_F(PpuRender, WindowLineOnlyAdvancesOnLinesThatShowIt) {
    ppu.write_vram(0x8010, 0xFF);  // tile 1, row 0: index 1
    ppu.write_vram(0x8013, 0xFF);  // tile 1, row 1: index 2
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    draw_until_line(kLcdBgWindow9c00, 0);
    EXPECT_EQ(pixel(0, 0), 1);
    set(Ppu::kWx, 200);  // hidden on line 1
    static_cast<void>(ppu.tick(Ppu::kTicksPerLine));
    EXPECT_EQ(pixel(0, 1), 0);
    set(Ppu::kWx, 7);  // back on line 2: it carries on from window row 1
    static_cast<void>(ppu.tick(Ppu::kTicksPerLine));
    EXPECT_EQ(pixel(0, 2), 2);
}

TEST_F(PpuRender, Lcdc6ClearSelectsThe9800WindowMap) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9800, 1);  // window map entry; the background uses $9C00 here
    set(Ppu::kWx, 15);
    draw_until_line(0xB9, 0);  // LCD, window ($9800), $8000 data, background ($9C00)
    EXPECT_EQ(pixel(7, 0), 0);
    EXPECT_EQ(pixel(8, 0), 3);
    EXPECT_EQ(pixel(15, 0), 3);
    EXPECT_EQ(pixel(16, 0), 0);
}

TEST_F(PpuRender, WindowNeedsLcdc5) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    draw_until_line(0xD1, 0);  // window map bit set, window enable clear
    EXPECT_EQ(pixel(0, 0), 0);
}

TEST_F(PpuRender, Lcdc0ClearHidesTheWindowToo) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    draw_until_line(0xF0, 0);
    EXPECT_EQ(pixel(0, 0), 0);
}

TEST_F(PpuRender, Wx166ShowsOneColumnAnd167None) {
    fill_tile(0x8010, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 166);
    draw_until_line(kLcdBgWindow9c00, 0);
    EXPECT_EQ(pixel(158, 0), 0);
    EXPECT_EQ(pixel(159, 0), 3);
    set(Ppu::kWx, 167);
    static_cast<void>(ppu.tick(Ppu::kTicksPerLine));
    EXPECT_EQ(pixel(159, 1), 0);
}

TEST_F(PpuRender, WxBelow7CutsOffTheWindowsLeftColumns) {
    fill_tile(0x8010, 0x55, 0x33);  // indices 0,1,2,3,0,1,2,3
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 4);  // screen x 0 shows window x 3
    draw_until_line(kLcdBgWindow9c00, 0);
    EXPECT_EQ(pixel(0, 0), 3);
    EXPECT_EQ(pixel(1, 0), 0);
    EXPECT_EQ(pixel(4, 0), 3);
    EXPECT_EQ(pixel(5, 0), 0);  // window x 8: map column 1, tile 0
}

}  // namespace
