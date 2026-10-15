#include "core/cartridge/rtc.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

using core::Rtc;
using Reg = Rtc::Register;

constexpr std::uint32_t kSecond = Rtc::kTicksPerSecond;

// Reads every register through a fresh latch, as rtc3test does.
Rtc::Registers read_all(Rtc& rtc) {
    rtc.latch();
    Rtc::Registers out{};
    for (int i = 0; i < Rtc::kRegisterCount; ++i) {
        out[static_cast<std::size_t>(i)] = rtc.read(static_cast<Reg>(i));
    }
    return out;
}

void set_time(Rtc& rtc, std::uint8_t h, std::uint8_t m, std::uint8_t s) {
    rtc.write(Reg::Hours, h);
    rtc.write(Reg::Minutes, m);
    rtc.write(Reg::Seconds, s);
}

TEST(Rtc, StartsAtZeroAndRunning) {
    Rtc rtc;
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 0, 0, 0}));
    EXPECT_FALSE(rtc.halted());
}

TEST(Rtc, TicksOncePerEmulatedSecond) {
    Rtc rtc;
    rtc.tick(kSecond - 1);
    EXPECT_EQ(read_all(rtc)[0], 0);
    rtc.tick(1);
    EXPECT_EQ(read_all(rtc)[0], 1);
    rtc.tick(3 * kSecond);
    EXPECT_EQ(read_all(rtc)[0], 4);
}

TEST(Rtc, ReadsReturnTheLatchedCopy) {
    Rtc rtc;
    rtc.latch();
    rtc.tick(2 * kSecond);
    EXPECT_EQ(rtc.read(Reg::Seconds), 0);
    rtc.latch();
    EXPECT_EQ(rtc.read(Reg::Seconds), 2);
}

TEST(Rtc, AWriteReadsBackWithoutALatch) {
    Rtc rtc;
    rtc.write(Reg::Minutes, 17);
    EXPECT_EQ(rtc.read(Reg::Minutes), 17);
    EXPECT_EQ(rtc.live()[1], 17);
}

TEST(Rtc, OnlyTheBitsThatExistAreKept) {
    Rtc rtc;
    for (int i = 0; i < Rtc::kRegisterCount; ++i) {
        rtc.write(static_cast<Reg>(i), 0xFF);
    }
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0x3F, 0x3F, 0x1F, 0xFF, 0xC1}));
}

TEST(Rtc, SecondsAndMinutesCarryAt59) {
    Rtc rtc;
    set_time(rtc, 4, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 5, 0, 0}));
}

TEST(Rtc, InvalidSecondsCountOnTo63ThenWrapWithoutACarry) {
    Rtc rtc;
    set_time(rtc, 0, 10, 60);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{61, 10, 0, 0, 0}));
    rtc.write(Reg::Seconds, 63);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 10, 0, 0, 0}));
}

TEST(Rtc, InvalidMinutesAndHoursWrapWithoutACarry) {
    Rtc rtc;
    set_time(rtc, 5, 63, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 5, 0, 0}));
    set_time(rtc, 31, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 0, 0, 0}));
}

TEST(Rtc, HighMinutesAndHoursCountOn) {
    Rtc rtc;
    set_time(rtc, 2, 61, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 62, 2, 0, 0}));
    set_time(rtc, 24, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 25, 0, 0}));
}

TEST(Rtc, Day255RollsIntoTheNinthBit) {
    Rtc rtc;
    rtc.write(Reg::DayLow, 0xFF);
    set_time(rtc, 23, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 0, 0x00, 0x01}));
}

TEST(Rtc, Day511RollsOverAndSetsTheCarry) {
    Rtc rtc;
    rtc.write(Reg::DayLow, 0xFF);
    rtc.write(Reg::DayHigh, 0x01);
    set_time(rtc, 23, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc), (Rtc::Registers{0, 0, 0, 0x00, 0x80}));
}

TEST(Rtc, TheCarryStaysSetUntilWritten) {
    Rtc rtc;
    rtc.write(Reg::DayLow, 0xFF);
    rtc.write(Reg::DayHigh, 0x81);
    set_time(rtc, 23, 59, 59);
    rtc.tick(kSecond);
    EXPECT_EQ(read_all(rtc)[4], 0x80);
    rtc.write(Reg::DayHigh, 0x00);
    EXPECT_EQ(read_all(rtc)[4], 0x00);
}

TEST(Rtc, TheHaltBitStopsTheClockAndTheSecondInProgress) {
    Rtc rtc;
    constexpr std::uint32_t kBefore = kSecond * 6 / 10;
    rtc.tick(kBefore);
    rtc.write(Reg::DayHigh, Rtc::kHaltBit);
    rtc.tick(5 * kSecond);
    EXPECT_EQ(read_all(rtc)[0], 0);
    rtc.write(Reg::DayHigh, 0x00);
    rtc.tick(kSecond - kBefore - 1);
    EXPECT_EQ(read_all(rtc)[0], 0);
    rtc.tick(1);
    EXPECT_EQ(read_all(rtc)[0], 1);
}

TEST(Rtc, WritingSecondsRestartsTheSecondInProgress) {
    Rtc rtc;
    rtc.tick(kSecond / 2);
    rtc.write(Reg::Seconds, 10);
    rtc.tick(kSecond - 1);
    EXPECT_EQ(read_all(rtc)[0], 10);
    rtc.tick(1);
    EXPECT_EQ(read_all(rtc)[0], 11);
}

TEST(Rtc, WritingTheOtherRegistersLeavesTheSecondInProgress) {
    for (const Reg reg : {Reg::Minutes, Reg::Hours, Reg::DayLow, Reg::DayHigh}) {
        Rtc rtc;
        rtc.tick(kSecond / 2);
        rtc.write(reg, 0x01);
        rtc.tick(kSecond / 2);
        EXPECT_EQ(read_all(rtc)[0], 1) << "register " << static_cast<int>(reg);
    }
}

TEST(Rtc, RestoreSetsBothCopiesWithOnlyTheValidBits) {
    Rtc rtc;
    rtc.tick(kSecond / 2);
    rtc.restore({0xFF, 0x01, 0x02, 0x03, 0xFF}, {0x04, 0x05, 0xFF, 0x07, 0x00});
    EXPECT_EQ(rtc.live(), (Rtc::Registers{0x3F, 0x01, 0x02, 0x03, 0xC1}));
    EXPECT_EQ(rtc.latched(), (Rtc::Registers{0x04, 0x05, 0x1F, 0x07, 0x00}));
    EXPECT_EQ(rtc.sub_second(), 0U);
}

// The catch-up at load must give exactly what ticking would (ADR-018). The
// reference ticks one second at a time.
Rtc::Registers advanced(const Rtc::Registers& start, std::uint64_t seconds) {
    Rtc rtc;
    rtc.restore(start, start);
    rtc.advance_seconds(seconds);
    return rtc.live();
}

Rtc::Registers ticked(const Rtc::Registers& start, std::uint64_t seconds) {
    Rtc rtc;
    rtc.restore(start, start);
    for (std::uint64_t i = 0; i < seconds; ++i) {
        rtc.tick(kSecond);
    }
    return rtc.live();
}

struct CatchUp {
    Rtc::Registers start;
    std::uint64_t seconds;
};

constexpr std::uint64_t kDay = 86'400;

TEST(Rtc, AdvanceMatchesTickingSecondBySecond) {
    const std::vector<CatchUp> cases = {
        {{0, 0, 0, 0, 0}, 0},
        {{0, 0, 0, 0, 0}, 1},
        {{59, 59, 23, 0, 0}, 1},
        {{12, 34, 5, 6, 0}, kDay - 1},
        {{12, 34, 5, 6, 0}, kDay},
        {{0, 0, 0, 0xFF, 0}, (3 * kDay) + 17},        // across day 255 -> 256
        {{0, 59, 23, 0xFE, 0x01}, (2 * kDay) + 100},  // across day 511: carry
        {{30, 30, 12, 0xFF, 0x81}, kDay},             // carry already set stays set
        {{62, 63, 30, 4, 0}, (2 * kDay) + 9},         // invalid S, M and H
        {{60, 61, 24, 0xFF, 0x01}, 40'000},           // invalid, then the overflow
        {{63, 59, 31, 0xFF, 0x01}, 1},                // 63 wraps without a carry
        {{5, 5, 5, 5, 0x80}, 3600},                   // carry set, no overflow
    };
    for (const CatchUp& c : cases) {
        EXPECT_EQ(advanced(c.start, c.seconds), ticked(c.start, c.seconds))
            << "start " << int{c.start[0]} << " " << int{c.start[1]} << " " << int{c.start[2]}
            << " " << int{c.start[3]} << " " << int{c.start[4]} << ", seconds " << c.seconds;
    }
}

TEST(Rtc, AdvanceMatchesTickingForRandomStates) {
    std::mt19937 random(2026);  // fixed seed: the same cases every run
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<std::uint64_t> seconds(0, 100'000);
    for (int i = 0; i < 40; ++i) {
        Rtc::Registers start{};
        for (auto& reg : start) {
            reg = static_cast<std::uint8_t>(byte(random));
        }
        start[4] &= static_cast<std::uint8_t>(~Rtc::kHaltBit);  // running
        const std::uint64_t n = seconds(random);
        EXPECT_EQ(advanced(start, n), ticked(start, n)) << "case " << i;
    }
}

TEST(Rtc, AdvanceSetsTheCarryAfterManyDayOverflows) {
    // 1000 days from day 0: the counter keeps its low 9 bits (1000 - 512).
    EXPECT_EQ(advanced({0, 0, 0, 0, 0}, 1000 * kDay), (Rtc::Registers{0, 0, 0, 0xE8, 0x81}));
}

TEST(Rtc, AdvanceDoesNothingWhileHalted) {
    const Rtc::Registers start{1, 2, 3, 4, Rtc::kHaltBit};
    EXPECT_EQ(advanced(start, 5 * kDay), start);
}

TEST(Rtc, AdvanceKeepsTheSecondInProgress) {
    Rtc rtc;
    rtc.tick(kSecond / 2);
    rtc.advance_seconds(10);
    EXPECT_EQ(rtc.live()[0], 10);
    rtc.tick(kSecond / 2);
    EXPECT_EQ(rtc.live()[0], 11);
}

}  // namespace
