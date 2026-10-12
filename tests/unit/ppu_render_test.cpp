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
// LCD on, background on, $8000 tile data, $9800 map, objects on (8x8 / 8x16).
constexpr std::uint8_t kLcdBgObjects = 0x93;
constexpr std::uint8_t kLcdBgTallObjects = 0x97;
// The first line after LCD-on has no OAM scan, so object tests draw on line 1.
constexpr std::uint32_t kObjectLine = 1;

class PpuRender : public testing::Test {
protected:
    void SetUp() override {
        set(Ppu::kLcdc, kLcdOff);
        set(Ppu::kBgp, kIdentityPalette);
        set(Ppu::kObp0, kIdentityPalette);
        set(Ppu::kObp1, kIdentityPalette);
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

    // Writes OAM entry `object` so its top-left pixel is at screen (x, line).
    void put_object(std::uint8_t object, int x, std::uint32_t line, std::uint8_t tile,
                    std::uint8_t attributes = 0) {
        const auto base = static_cast<std::uint16_t>(Ppu::kOamStart + object * 4);
        ppu.write_oam(base, static_cast<std::uint8_t>(line + 16));
        ppu.write_oam(static_cast<std::uint16_t>(base + 1), static_cast<std::uint8_t>(x + 8));
        ppu.write_oam(static_cast<std::uint16_t>(base + 2), tile);
        ppu.write_oam(static_cast<std::uint16_t>(base + 3), attributes);
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

TEST_F(PpuRender, SwitchingTheLcdOffBlanksTheScreen) {
    fill_tile(0x8000, 0xFF, 0xFF);
    draw_until_line(kLcdBg8000, 0);
    ASSERT_EQ(pixel(0, 0), 3);
    set(Ppu::kLcdc, kLcdOff);
    EXPECT_EQ(pixel(0, 0), 0);
    EXPECT_EQ(pixel(159, 0), 0);
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

// Objects. Unless a test says otherwise the background is tile 0, all
// colour 0, and every palette maps index n to shade n.

TEST_F(PpuRender, ObjectTopLeftIsAtXMinus8AndYMinus16) {
    fill_tile(0x8010, 0xFF, 0xFF);  // tile 1: index 3
    put_object(0, 20, kObjectLine, 1);
    draw_until_line(kLcdBgObjects, kObjectLine + 8);
    EXPECT_EQ(pixel(20, kObjectLine - 1), 0);
    EXPECT_EQ(pixel(19, kObjectLine), 0);
    EXPECT_EQ(pixel(20, kObjectLine), 3);
    EXPECT_EQ(pixel(27, kObjectLine), 3);
    EXPECT_EQ(pixel(28, kObjectLine), 0);
    EXPECT_EQ(pixel(20, kObjectLine + 7), 3);
    EXPECT_EQ(pixel(20, kObjectLine + 8), 0);
}

TEST_F(PpuRender, ObjectsNeedLcdc1) {
    fill_tile(0x8010, 0xFF, 0xFF);
    put_object(0, 20, kObjectLine, 1);
    draw_until_line(kLcdBg8000, kObjectLine);
    EXPECT_EQ(pixel(20, kObjectLine), 0);
}

TEST_F(PpuRender, FirstLineAfterLcdOnDrawsNoObjects) {
    fill_tile(0x8010, 0xFF, 0xFF);
    put_object(0, 20, 0, 1);
    draw_until_line(kLcdBgObjects, 1);
    EXPECT_EQ(pixel(20, 0), 0);
    EXPECT_EQ(pixel(20, 1), 3);
}

TEST_F(PpuRender, ObjectColour0IsTransparent) {
    fill_tile(0x8000, 0x00, 0xFF);  // background: index 2
    fill_tile(0x8010, 0x55, 0x33);  // object: indices 0,1,2,3,...
    set(Ppu::kObp0, 0xFC);          // 1, 2, 3 -> shade 3
    put_object(0, 8, kObjectLine, 1);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 2);
    EXPECT_EQ(pixel(9, kObjectLine), 3);
    EXPECT_EQ(pixel(10, kObjectLine), 3);
    EXPECT_EQ(pixel(11, kObjectLine), 3);
    EXPECT_EQ(pixel(12, kObjectLine), 2);
}

TEST_F(PpuRender, AttributeBit4SelectsObp1) {
    fill_tile(0x8010, 0xFF, 0x00);  // index 1
    set(Ppu::kObp0, 0x04);          // 1 -> 1
    set(Ppu::kObp1, 0x0C);          // 1 -> 3
    put_object(0, 8, kObjectLine, 1);
    put_object(1, 40, kObjectLine, 1, 0x10);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 1);
    EXPECT_EQ(pixel(40, kObjectLine), 3);
}

TEST_F(PpuRender, AttributeBit5FlipsHorizontally) {
    fill_tile(0x8010, 0x80, 0x00);  // only column 0: index 1
    put_object(0, 8, kObjectLine, 1, 0x20);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 0);
    EXPECT_EQ(pixel(15, kObjectLine), 1);
}

TEST_F(PpuRender, AttributeBit6FlipsVertically) {
    ppu.write_vram(0x8010, 0xFF);  // tile 1, only row 0: index 1
    put_object(0, 8, kObjectLine, 1, 0x40);
    draw_until_line(kLcdBgObjects, kObjectLine + 7);
    EXPECT_EQ(pixel(8, kObjectLine), 0);
    EXPECT_EQ(pixel(8, kObjectLine + 7), 1);
}

TEST_F(PpuRender, TallObjectsUseTwoTilesAndIgnoreTileBit0) {
    fill_tile(0x8020, 0xFF, 0x00);  // tile 2: index 1
    fill_tile(0x8030, 0x00, 0xFF);  // tile 3: index 2
    put_object(0, 8, kObjectLine, 3);
    draw_until_line(kLcdBgTallObjects, kObjectLine + 16);
    EXPECT_EQ(pixel(8, kObjectLine), 1);
    EXPECT_EQ(pixel(8, kObjectLine + 7), 1);
    EXPECT_EQ(pixel(8, kObjectLine + 8), 2);
    EXPECT_EQ(pixel(8, kObjectLine + 15), 2);
    EXPECT_EQ(pixel(8, kObjectLine + 16), 0);
}

TEST_F(PpuRender, TallObjectsFlipBothTilesVertically) {
    fill_tile(0x8020, 0xFF, 0x00);
    fill_tile(0x8030, 0x00, 0xFF);
    put_object(0, 8, kObjectLine, 2, 0x40);
    draw_until_line(kLcdBgTallObjects, kObjectLine + 15);
    EXPECT_EQ(pixel(8, kObjectLine), 2);
    EXPECT_EQ(pixel(8, kObjectLine + 15), 1);
}

TEST_F(PpuRender, ObjectsClipAtTheLeftEdge) {
    fill_tile(0x8010, 0x0F, 0x00);  // columns 4-7: index 1
    put_object(0, -4, kObjectLine, 1);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(0, kObjectLine), 1);
    EXPECT_EQ(pixel(3, kObjectLine), 1);
    EXPECT_EQ(pixel(4, kObjectLine), 0);
}

TEST_F(PpuRender, ObjectsClipAtTheRightEdge) {
    fill_tile(0x8010, 0xFF, 0x00);
    put_object(0, 156, kObjectLine, 1);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(155, kObjectLine), 0);
    EXPECT_EQ(pixel(156, kObjectLine), 1);
    EXPECT_EQ(pixel(159, kObjectLine), 1);
}

TEST_F(PpuRender, SmallerXWinsWhereObjectsOverlap) {
    fill_tile(0x8010, 0xFF, 0x00);      // tile 1: index 1
    fill_tile(0x8020, 0x00, 0xFF);      // tile 2: index 2
    put_object(0, 12, kObjectLine, 1);  // 12-19
    put_object(1, 8, kObjectLine, 2);   // 8-15, smaller X
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(11, kObjectLine), 2);
    EXPECT_EQ(pixel(12, kObjectLine), 2);
    EXPECT_EQ(pixel(15, kObjectLine), 2);
    EXPECT_EQ(pixel(16, kObjectLine), 1);
}

TEST_F(PpuRender, SameXEarlierInOamWins) {
    fill_tile(0x8010, 0xFF, 0x00);
    fill_tile(0x8020, 0x00, 0xFF);
    put_object(0, 8, kObjectLine, 1);
    put_object(1, 8, kObjectLine, 2);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 1);
}

TEST_F(PpuRender, LowerPriorityObjectShowsThroughTransparentPixels) {
    fill_tile(0x8010, 0x0F, 0x00);  // columns 4-7: index 1, columns 0-3 transparent
    fill_tile(0x8020, 0x00, 0xFF);
    put_object(0, 8, kObjectLine, 1);
    put_object(1, 8, kObjectLine, 2);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 2);
    EXPECT_EQ(pixel(11, kObjectLine), 2);
    EXPECT_EQ(pixel(12, kObjectLine), 1);
}

TEST_F(PpuRender, AttributeBit7PutsTheObjectBehindBackgroundColours1To3) {
    fill_tile(0x8000, 0xF0, 0x00);  // background: columns 0-3 index 1, 4-7 index 0
    fill_tile(0x8010, 0xFF, 0xFF);
    put_object(0, 0, kObjectLine, 1, 0x80);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(0, kObjectLine), 1);
    EXPECT_EQ(pixel(3, kObjectLine), 1);
    EXPECT_EQ(pixel(4, kObjectLine), 3);
    EXPECT_EQ(pixel(7, kObjectLine), 3);
}

TEST_F(PpuRender, HiddenWinnerStillHidesTheObjectBelowIt) {
    fill_tile(0x8000, 0xFF, 0x00);  // background: index 1
    fill_tile(0x8010, 0xFF, 0xFF);
    fill_tile(0x8020, 0x00, 0xFF);
    put_object(0, 8, kObjectLine, 1, 0x80);  // wins, but is behind the background
    put_object(1, 8, kObjectLine, 2);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 1);
}

TEST_F(PpuRender, ObjectsDrawWithTheBackgroundOff) {
    fill_tile(0x8000, 0xFF, 0xFF);
    fill_tile(0x8010, 0xFF, 0xFF);
    put_object(0, 8, kObjectLine, 1, 0x80);  // background is colour 0 when off
    draw_until_line(0x92, kObjectLine);
    EXPECT_EQ(pixel(0, kObjectLine), 0);
    EXPECT_EQ(pixel(8, kObjectLine), 3);
}

TEST_F(PpuRender, AtMostTenObjectsPerLine) {
    fill_tile(0x8010, 0xFF, 0xFF);
    for (std::uint8_t i = 0; i < 11; ++i) {
        put_object(i, i * 8, kObjectLine, 1);
    }
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(72, kObjectLine), 3);
    EXPECT_EQ(pixel(80, kObjectLine), 0);
}

TEST_F(PpuRender, OffScreenObjectsStillCountTowardsTheTen) {
    fill_tile(0x8010, 0xFF, 0xFF);
    for (std::uint8_t i = 0; i < 10; ++i) {
        put_object(i, -8, kObjectLine, 1);  // OAM X = 0: on the line, never visible
    }
    put_object(10, 8, kObjectLine, 1);
    draw_until_line(kLcdBgObjects, kObjectLine);
    EXPECT_EQ(pixel(8, kObjectLine), 0);
}

TEST_F(PpuRender, ObjectsDrawOverTheWindow) {
    fill_tile(0x8010, 0xFF, 0x00);  // window tile: index 1
    fill_tile(0x8020, 0xFF, 0xFF);
    ppu.write_vram(0x9C00, 1);
    set(Ppu::kWx, 7);
    set(Ppu::kWy, 0);
    put_object(0, 2, kObjectLine, 2);
    draw_until_line(0xF3, kObjectLine);  // window setup plus objects on
    EXPECT_EQ(pixel(0, kObjectLine), 1);
    EXPECT_EQ(pixel(2, kObjectLine), 3);
}

}  // namespace
