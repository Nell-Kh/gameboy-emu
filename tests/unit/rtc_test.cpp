#include "core/cartridge/rtc.h"

#include <gtest/gtest.h>

#include <cstdint>

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

}  // namespace
