#include "core/ppu.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

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
    // 6 for the object, 5 for being first on its BG tile at offset 0, 3 less per line.
    EXPECT_EQ(ppu.drawing_dots(), 172U + 6 + 5 - 3);
}

TEST(Ppu, AnObjectAtXZeroFollowsTheSameRuleAsAnyOther) {
    Ppu ppu;
    write(ppu, Ppu::kLcdc, 0x93);
    advance(ppu, 252);
    ppu.write_oam(0xFE00, 16 + 1);
    ppu.write_oam(0xFE01, 0);
    advance(ppu, 456 - 252 + 80);
    EXPECT_EQ(ppu.drawing_dots(), 172U + 6 + 5 - 3);  // not the Pan Docs' flat 11
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
        ppu.write_oam(static_cast<std::uint16_t>(0xFE01 + i * 4), 0);
    }
    advance(ppu, 456 - 252 + 80);
    // 6 each for the first ten, 5 once for the shared BG tile, 3 less per line.
    EXPECT_EQ(ppu.drawing_dots(), 172U + 10 * 6 + 5 - 3);
}

// Mooneye's intr_2_mode0_timing_sprites (acceptance/ppu), all 105 cases: the
// OAM X positions of the objects on the line, and how many machine cycles later
// than with no objects mode 0 starts on hardware (the test's "extra cycles"). The
// test sees the end of mode 3 to the machine cycle. With no objects, mode 3 here
// ends where the test's machine-cycle check expects, so the extra machine cycles
// it measures are the object penalty in dots rounded up to whole machine cycles.
struct SpriteTimingCase {
    std::uint32_t extra_machine_cycles;
    std::vector<std::uint8_t> xs;
};

const std::vector<SpriteTimingCase>& sprite_timing_cases() {
    static const std::vector<SpriteTimingCase> cases = {
        {2, {0}},                                                  // case 0
        {4, {0, 0}},                                               // case 1
        {5, {0, 0, 0}},                                            // case 2
        {7, {0, 0, 0, 0}},                                         // case 3
        {8, {0, 0, 0, 0, 0}},                                      // case 4
        {10, {0, 0, 0, 0, 0, 0}},                                  // case 5
        {11, {0, 0, 0, 0, 0, 0, 0}},                               // case 6
        {13, {0, 0, 0, 0, 0, 0, 0, 0}},                            // case 7
        {14, {0, 0, 0, 0, 0, 0, 0, 0, 0}},                         // case 8
        {16, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},                      // case 9
        {16, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},                      // case 10
        {15, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}},                      // case 11
        {15, {3, 3, 3, 3, 3, 3, 3, 3, 3, 3}},                      // case 12
        {15, {4, 4, 4, 4, 4, 4, 4, 4, 4, 4}},                      // case 13
        {15, {5, 5, 5, 5, 5, 5, 5, 5, 5, 5}},                      // case 14
        {15, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6}},                      // case 15
        {15, {7, 7, 7, 7, 7, 7, 7, 7, 7, 7}},                      // case 16
        {16, {8, 8, 8, 8, 8, 8, 8, 8, 8, 8}},                      // case 17
        {16, {9, 9, 9, 9, 9, 9, 9, 9, 9, 9}},                      // case 18
        {15, {10, 10, 10, 10, 10, 10, 10, 10, 10, 10}},            // case 19
        {15, {11, 11, 11, 11, 11, 11, 11, 11, 11, 11}},            // case 20
        {15, {12, 12, 12, 12, 12, 12, 12, 12, 12, 12}},            // case 21
        {15, {13, 13, 13, 13, 13, 13, 13, 13, 13, 13}},            // case 22
        {15, {14, 14, 14, 14, 14, 14, 14, 14, 14, 14}},            // case 23
        {15, {15, 15, 15, 15, 15, 15, 15, 15, 15, 15}},            // case 24
        {16, {16, 16, 16, 16, 16, 16, 16, 16, 16, 16}},            // case 25
        {16, {17, 17, 17, 17, 17, 17, 17, 17, 17, 17}},            // case 26
        {16, {32, 32, 32, 32, 32, 32, 32, 32, 32, 32}},            // case 27
        {16, {33, 33, 33, 33, 33, 33, 33, 33, 33, 33}},            // case 28
        {16, {160, 160, 160, 160, 160, 160, 160, 160, 160, 160}},  // case 29
        {16, {161, 161, 161, 161, 161, 161, 161, 161, 161, 161}},  // case 30
        {15, {162, 162, 162, 162, 162, 162, 162, 162, 162, 162}},  // case 31
        {15, {167, 167, 167, 167, 167, 167, 167, 167, 167, 167}},  // case 32
        {15, {167, 167, 167, 167, 167, 167, 167, 167, 167, 167}},  // case 33
        {0, {168, 168, 168, 168, 168, 168, 168, 168, 168, 168}},   // case 34
        {0, {169, 169, 169, 169, 169, 169, 169, 169, 169, 169}},   // case 35
        {17, {0, 0, 0, 0, 0, 160, 160, 160, 160, 160}},            // case 36
        {17, {1, 1, 1, 1, 1, 161, 161, 161, 161, 161}},            // case 37
        {16, {2, 2, 2, 2, 2, 162, 162, 162, 162, 162}},            // case 38
        {16, {3, 3, 3, 3, 3, 163, 163, 163, 163, 163}},            // case 39
        {15, {4, 4, 4, 4, 4, 164, 164, 164, 164, 164}},            // case 40
        {15, {5, 5, 5, 5, 5, 165, 165, 165, 165, 165}},            // case 41
        {15, {6, 6, 6, 6, 6, 166, 166, 166, 166, 166}},            // case 42
        {15, {7, 7, 7, 7, 7, 167, 167, 167, 167, 167}},            // case 43
        {17, {64, 64, 64, 64, 64, 160, 160, 160, 160, 160}},       // case 44
        {17, {65, 65, 65, 65, 65, 161, 161, 161, 161, 161}},       // case 45
        {16, {66, 66, 66, 66, 66, 162, 162, 162, 162, 162}},       // case 46
        {16, {67, 67, 67, 67, 67, 163, 163, 163, 163, 163}},       // case 47
        {15, {68, 68, 68, 68, 68, 164, 164, 164, 164, 164}},       // case 48
        {15, {69, 69, 69, 69, 69, 165, 165, 165, 165, 165}},       // case 49
        {15, {70, 70, 70, 70, 70, 166, 166, 166, 166, 166}},       // case 50
        {15, {71, 71, 71, 71, 71, 167, 167, 167, 167, 167}},       // case 51
        {2, {0}},                                                  // case 52
        {2, {1}},                                                  // case 53
        {2, {2}},                                                  // case 54
        {2, {3}},                                                  // case 55
        {1, {4}},                                                  // case 56
        {1, {5}},                                                  // case 57
        {1, {6}},                                                  // case 58
        {1, {7}},                                                  // case 59
        {2, {8}},                                                  // case 60
        {2, {9}},                                                  // case 61
        {2, {10}},                                                 // case 62
        {2, {11}},                                                 // case 63
        {1, {12}},                                                 // case 64
        {1, {13}},                                                 // case 65
        {1, {14}},                                                 // case 66
        {1, {15}},                                                 // case 67
        {2, {16}},                                                 // case 68
        {2, {17}},                                                 // case 69
        {2, {160}},                                                // case 70
        {2, {161}},                                                // case 71
        {2, {162}},                                                // case 72
        {2, {163}},                                                // case 73
        {1, {164}},                                                // case 74
        {1, {165}},                                                // case 75
        {1, {166}},                                                // case 76
        {1, {167}},                                                // case 77
        {5, {0, 8}},                                               // case 78
        {5, {1, 9}},                                               // case 79
        {4, {2, 10}},                                              // case 80
        {4, {3, 11}},                                              // case 81
        {3, {4, 12}},                                              // case 82
        {3, {5, 13}},                                              // case 83
        {3, {6, 14}},                                              // case 84
        {3, {7, 15}},                                              // case 85
        {5, {8, 16}},                                              // case 86
        {5, {9, 17}},                                              // case 87
        {4, {10, 18}},                                             // case 88
        {4, {11, 19}},                                             // case 89
        {3, {12, 20}},                                             // case 90
        {3, {13, 21}},                                             // case 91
        {3, {14, 22}},                                             // case 92
        {3, {15, 23}},                                             // case 93
        {5, {16, 24}},                                             // case 94
        {27, {0, 8, 16, 24, 32, 40, 48, 56, 64, 72}},              // case 95
        {25, {1, 9, 17, 25, 33, 41, 49, 57, 65, 73}},              // case 96
        {22, {2, 10, 18, 26, 34, 42, 50, 58, 66, 74}},             // case 97
        {20, {3, 11, 19, 27, 35, 43, 51, 59, 67, 75}},             // case 98
        {17, {4, 12, 20, 28, 36, 44, 52, 60, 68, 76}},             // case 99
        {15, {5, 13, 21, 29, 37, 45, 53, 61, 69, 77}},             // case 100
        {15, {6, 14, 22, 30, 38, 46, 54, 62, 70, 78}},             // case 101
        {15, {7, 15, 23, 31, 39, 47, 55, 63, 71, 79}},             // case 102
        {27, {72, 64, 56, 48, 40, 32, 24, 16, 8, 0}},              // case 103
        {25, {73, 65, 57, 49, 41, 33, 25, 17, 9, 1}},              // case 104
    };
    return cases;
}

TEST(Ppu, ObjectPenaltyMatchesAllIntr2Mode0TimingSpritesCases) {
    for (std::size_t i = 0; i < sprite_timing_cases().size(); ++i) {
        const SpriteTimingCase& test_case = sprite_timing_cases().at(i);
        Ppu ppu;
        write(ppu, Ppu::kLcdc, 0x93);
        advance(ppu, 252);  // leave the post-boot line so OAM is writable
        for (std::size_t n = 0; n < test_case.xs.size(); ++n) {
            const auto entry = static_cast<std::uint16_t>(0xFE00 + n * 4);
            ppu.write_oam(entry, 16 + 1);  // on line 1
            ppu.write_oam(static_cast<std::uint16_t>(entry + 1), test_case.xs.at(n));
        }
        advance(ppu, 456 - 252 + 80);  // line 1, start of mode 3
        const std::uint32_t penalty = ppu.drawing_dots() - 172U;
        EXPECT_EQ((penalty + 3) / 4, test_case.extra_machine_cycles) << "case " << i;
    }
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
