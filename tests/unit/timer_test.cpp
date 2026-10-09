#include "core/timer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <string>

namespace {

using core::Timer;

constexpr std::uint8_t kEnabled = 0x04;

// A timer with the counter at 0, so tests can count ticks from a known point.
Timer reset_timer() {
    Timer timer;
    timer.write(Timer::kDiv, 0x00);
    return timer;
}

TEST(Timer, DivReadsABAfterBoot) {
    const Timer timer;
    EXPECT_EQ(timer.read(Timer::kDiv), 0xAB);
}

TEST(Timer, DivCountsUpEvery256Ticks) {
    Timer timer = reset_timer();
    EXPECT_FALSE(timer.tick(255));
    EXPECT_EQ(timer.read(Timer::kDiv), 0x00);
    EXPECT_FALSE(timer.tick(1));
    EXPECT_EQ(timer.read(Timer::kDiv), 0x01);
    EXPECT_FALSE(timer.tick(256 * 9));
    EXPECT_EQ(timer.read(Timer::kDiv), 0x0A);
}

TEST(Timer, WritingAnyValueToDivResetsIt) {
    Timer timer;
    timer.write(Timer::kDiv, 0x77);
    EXPECT_EQ(timer.read(Timer::kDiv), 0x00);
}

TEST(Timer, DivWrapsAround) {
    Timer timer = reset_timer();
    EXPECT_FALSE(timer.tick(256 * 256));
    EXPECT_EQ(timer.read(Timer::kDiv), 0x00);
}

TEST(Timer, UnusedTacBitsReadAsOne) {
    Timer timer;
    EXPECT_EQ(timer.read(Timer::kTac), 0xF8);
    timer.write(Timer::kTac, 0xFF);
    EXPECT_EQ(timer.read(Timer::kTac), 0xFF);
    timer.write(Timer::kTac, 0x05);
    EXPECT_EQ(timer.read(Timer::kTac), 0xFD);
}

TEST(Timer, TimaDoesNotCountWhileDisabled) {
    Timer timer = reset_timer();
    timer.write(Timer::kTac, 0x01);
    EXPECT_FALSE(timer.tick(10000));
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);
}

struct Rate {
    std::uint8_t tac_bits;
    std::uint32_t ticks_per_count;
};

// Tells GoogleTest how to print a case in the test listing.
void PrintTo(const Rate& rate, std::ostream* os) {
    *os << "one count every " << rate.ticks_per_count << " ticks";
}

class TimerRate : public testing::TestWithParam<Rate> {};

TEST_P(TimerRate, TimaCountsAtTheSelectedRate) {
    const Rate rate = GetParam();
    Timer timer = reset_timer();
    timer.write(Timer::kTac, kEnabled | rate.tac_bits);

    EXPECT_FALSE(timer.tick(rate.ticks_per_count - 1));
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);
    EXPECT_FALSE(timer.tick(1));
    EXPECT_EQ(timer.read(Timer::kTima), 0x01);
    EXPECT_FALSE(timer.tick(rate.ticks_per_count * 9));
    EXPECT_EQ(timer.read(Timer::kTima), 0x0A);
}

INSTANTIATE_TEST_SUITE_P(AllRates, TimerRate,
                         testing::Values(Rate{0x00, 1024}, Rate{0x01, 16}, Rate{0x02, 64},
                                         Rate{0x03, 256}),
                         [](const testing::TestParamInfo<Rate>& info) {
                             return "Every" + std::to_string(info.param.ticks_per_count) + "Ticks";
                         });

TEST(Timer, OverflowReloadsFromTmaOneMachineCycleLater) {
    Timer timer = reset_timer();
    timer.write(Timer::kTma, 0x23);
    timer.write(Timer::kTima, 0xFF);
    timer.write(Timer::kTac, kEnabled | 0x01);

    // The 16th tick overflows TIMA. It reads 0x00 for the next 4 ticks.
    EXPECT_FALSE(timer.tick(16));
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);
    EXPECT_FALSE(timer.tick(3));
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);

    // Then the reload and the interrupt happen together.
    EXPECT_TRUE(timer.tick(1));
    EXPECT_EQ(timer.read(Timer::kTima), 0x23);
}

TEST(Timer, InterruptIsRequestedOncePerOverflow) {
    Timer timer = reset_timer();
    timer.write(Timer::kTima, 0xFE);
    timer.write(Timer::kTac, kEnabled | 0x01);

    int interrupts = 0;
    for (int tick = 0; tick < 16 * 3; ++tick) {
        interrupts += timer.tick(1) ? 1 : 0;
    }
    EXPECT_EQ(interrupts, 1);
}

TEST(Timer, AfterReloadTimaKeepsCountingFromTma) {
    Timer timer = reset_timer();
    timer.write(Timer::kTma, 0xFE);
    timer.write(Timer::kTima, 0xFF);
    timer.write(Timer::kTac, kEnabled | 0x01);

    EXPECT_TRUE(timer.tick(16 + 4));
    EXPECT_EQ(timer.read(Timer::kTima), 0xFE);
    EXPECT_FALSE(timer.tick(12));
    EXPECT_EQ(timer.read(Timer::kTima), 0xFF);
}

TEST(Timer, WritingTimaDuringTheDelayCancelsReloadAndInterrupt) {
    Timer timer = reset_timer();
    timer.write(Timer::kTma, 0x23);
    timer.write(Timer::kTima, 0xFF);
    timer.write(Timer::kTac, kEnabled | 0x01);
    EXPECT_FALSE(timer.tick(16));

    timer.write(Timer::kTima, 0x50);
    EXPECT_FALSE(timer.tick(8));
    EXPECT_EQ(timer.read(Timer::kTima), 0x50);
}

TEST(Timer, WritingTimaInTheReloadCycleIsIgnored) {
    Timer timer = reset_timer();
    timer.write(Timer::kTma, 0x23);
    timer.write(Timer::kTima, 0xFF);
    timer.write(Timer::kTac, kEnabled | 0x01);
    EXPECT_TRUE(timer.tick(16 + 4));

    timer.write(Timer::kTima, 0x50);
    EXPECT_EQ(timer.read(Timer::kTima), 0x23);
}

TEST(Timer, WritingTmaInTheReloadCycleAlsoUpdatesTima) {
    Timer timer = reset_timer();
    timer.write(Timer::kTma, 0x23);
    timer.write(Timer::kTima, 0xFF);
    timer.write(Timer::kTac, kEnabled | 0x01);
    EXPECT_TRUE(timer.tick(16 + 4));

    timer.write(Timer::kTma, 0x77);
    EXPECT_EQ(timer.read(Timer::kTima), 0x77);
}

TEST(Timer, ResettingDivWhileTheWatchedBitIsHighCountsOnce) {
    Timer timer = reset_timer();
    timer.write(Timer::kTac, kEnabled | 0x01);
    // Rate 01 watches bit 3, which is 1 from tick 8 to tick 15.
    EXPECT_FALSE(timer.tick(8));
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);

    timer.write(Timer::kDiv, 0x00);
    EXPECT_EQ(timer.read(Timer::kTima), 0x01);
}

TEST(Timer, ResettingDivWhileTheWatchedBitIsLowDoesNotCount) {
    Timer timer = reset_timer();
    timer.write(Timer::kTac, kEnabled | 0x01);
    EXPECT_FALSE(timer.tick(4));

    timer.write(Timer::kDiv, 0x00);
    EXPECT_EQ(timer.read(Timer::kTima), 0x00);
}

TEST(Timer, DisablingWhileTheWatchedBitIsHighCountsOnce) {
    Timer timer = reset_timer();
    timer.write(Timer::kTac, kEnabled | 0x01);
    EXPECT_FALSE(timer.tick(8));

    timer.write(Timer::kTac, 0x01);
    EXPECT_EQ(timer.read(Timer::kTima), 0x01);
}

}  // namespace
