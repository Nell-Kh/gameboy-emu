// The APU's registers, power, frame sequencer, length counters, envelopes and
// sweep (docs/apu-notes.md, sections 3 and 4). Blargg's dmg_sound ROMs check
// the same behaviour on the whole machine (rom_tests).

#include "core/apu.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using core::Apu;

constexpr std::uint16_t kNr10 = 0xFF10;
constexpr std::uint16_t kNr11 = 0xFF11;
constexpr std::uint16_t kNr12 = 0xFF12;
constexpr std::uint16_t kNr13 = 0xFF13;
constexpr std::uint16_t kNr14 = 0xFF14;
constexpr std::uint16_t kNr30 = 0xFF1A;
constexpr std::uint16_t kNr31 = 0xFF1B;
constexpr std::uint16_t kNr34 = 0xFF1E;
constexpr std::uint16_t kNr41 = 0xFF20;
constexpr std::uint16_t kNr42 = 0xFF21;
constexpr std::uint16_t kNr44 = 0xFF23;
constexpr std::uint16_t kNr50 = 0xFF24;
constexpr std::uint16_t kNr52 = 0xFF26;

constexpr std::uint8_t kTrigger = 0x80;
constexpr std::uint8_t kLengthEnable = 0x40;

// Runs the frame sequencer until the next step is `step` (0-7). A fresh or
// just-powered-on APU's next step is 0.
void advance_to_step(Apu& apu, int from, int step) {
    for (int s = from; s != step; s = (s + 1) % 8) {
        apu.step_frame_sequencer();
    }
}

// A powered-off-then-on APU: every register 0, next step 0.
Apu fresh_apu() {
    Apu apu;
    apu.write(kNr52, 0x00);
    apu.write(kNr52, 0x80);
    return apu;
}

TEST(Apu, StartsAsTheBootRomLeavesIt) {
    const Apu apu;
    EXPECT_EQ(apu.read(kNr52), 0xF1);  // powered, channel 1 still marked on
    EXPECT_EQ(apu.read(kNr11), 0xBF);
    EXPECT_EQ(apu.read(kNr12), 0xF3);
    EXPECT_EQ(apu.read(kNr50), 0x77);
    EXPECT_EQ(apu.read(0xFF25), 0xF3);
}

TEST(Apu, UnusedAndWriteOnlyBitsReadAsOne) {
    const Apu apu = fresh_apu();
    struct Case {
        std::uint16_t address;
        std::uint8_t ones;
    };
    for (const Case c :
         {Case{0xFF10, 0x80}, Case{0xFF11, 0x3F}, Case{0xFF12, 0x00}, Case{0xFF13, 0xFF},
          Case{0xFF14, 0xBF}, Case{0xFF15, 0xFF}, Case{0xFF16, 0x3F}, Case{0xFF17, 0x00},
          Case{0xFF18, 0xFF}, Case{0xFF19, 0xBF}, Case{0xFF1A, 0x7F}, Case{0xFF1B, 0xFF},
          Case{0xFF1C, 0x9F}, Case{0xFF1D, 0xFF}, Case{0xFF1E, 0xBF}, Case{0xFF1F, 0xFF},
          Case{0xFF20, 0xFF}, Case{0xFF21, 0x00}, Case{0xFF22, 0x00}, Case{0xFF23, 0xBF},
          Case{0xFF24, 0x00}, Case{0xFF25, 0x00}}) {
        EXPECT_EQ(apu.read(c.address), c.ones) << std::hex << c.address;
    }
    EXPECT_EQ(apu.read(kNr52), 0xF0);  // powered, no channel on
}

TEST(Apu, PowerOffClearsTheRegistersAndIgnoresWrites) {
    Apu apu = fresh_apu();
    apu.write(kNr50, 0x77);
    apu.write(kNr12, 0xF0);
    apu.write(kNr14, kTrigger);
    ASSERT_TRUE(apu.channel_on(0));
    apu.write(kNr52, 0x00);
    EXPECT_EQ(apu.read(kNr52), 0x70);
    EXPECT_EQ(apu.read(kNr50), 0x00);
    EXPECT_EQ(apu.read(kNr12), 0x00);
    EXPECT_FALSE(apu.channel_on(0));
    apu.write(kNr50, 0x55);  // ignored while off
    EXPECT_EQ(apu.read(kNr50), 0x00);
    apu.write(0xFF30, 0x12);  // wave RAM is not
    EXPECT_EQ(apu.read(0xFF30), 0x12);
}

TEST(Apu, OnTheDmgLengthsSurvivePowerOffAndCanBeWrittenWhileOff) {
    Apu apu = fresh_apu();
    apu.write(kNr11, 20);  // 64 - 20 = 44 steps
    apu.write(kNr52, 0x00);
    EXPECT_EQ(apu.length_counter(0), 44);
    apu.write(kNr31, 6);     // 256 - 6
    apu.write(kNr41, 0x3F);  // 64 - 63
    EXPECT_EQ(apu.length_counter(2), 250);
    EXPECT_EQ(apu.length_counter(3), 1);
    apu.write(kNr52, 0x80);
    EXPECT_EQ(apu.read(kNr11), 0x3F);  // the duty bits were not written
}

TEST(Apu, ATriggerTurnsAChannelOnOnlyWithItsDacOn) {
    Apu apu = fresh_apu();
    apu.write(kNr14, kTrigger);  // NR12 is 0: DAC off
    EXPECT_FALSE(apu.channel_on(0));
    apu.write(kNr12, 0x08);  // volume 0 but direction up: DAC on
    apu.write(kNr14, kTrigger);
    EXPECT_TRUE(apu.channel_on(0));
    apu.write(kNr12, 0x00);  // DAC off turns the channel off
    EXPECT_FALSE(apu.channel_on(0));

    apu.write(kNr30, 0x80);
    apu.write(kNr34, kTrigger);
    EXPECT_TRUE(apu.channel_on(2));
    apu.write(kNr30, 0x00);
    EXPECT_FALSE(apu.channel_on(2));
}

TEST(Apu, TheLengthCounterTurnsTheChannelOffWhenItReachesZero) {
    Apu apu = fresh_apu();
    apu.write(kNr42, 0xF0);
    apu.write(kNr41, 62);  // 2 steps left
    apu.write(kNr44, kTrigger | kLengthEnable);
    ASSERT_TRUE(apu.channel_on(3));
    apu.step_frame_sequencer();  // step 0: length 1
    EXPECT_TRUE(apu.channel_on(3));
    apu.step_frame_sequencer();  // step 1: no length clock
    apu.step_frame_sequencer();  // step 2: length 0
    EXPECT_FALSE(apu.channel_on(3));
    EXPECT_EQ(apu.read(kNr52) & 0x08, 0);
}

TEST(Apu, LengthOnlyCountsWhileEnabled) {
    Apu apu = fresh_apu();
    apu.write(kNr42, 0xF0);
    apu.write(kNr41, 63);
    apu.write(kNr44, kTrigger);  // length not enabled
    for (int i = 0; i < 16; ++i) {
        apu.step_frame_sequencer();
    }
    EXPECT_TRUE(apu.channel_on(3));
    EXPECT_EQ(apu.length_counter(3), 1);
}

TEST(Apu, EnablingLengthBeforeAStepThatSkipsLengthClocksItOnce) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0xF0);
    apu.write(kNr11, 0);  // 64
    apu.write(kNr14, kTrigger);
    apu.step_frame_sequencer();  // step 0 done; next is 1, which skips length
    apu.write(kNr14, kLengthEnable);
    EXPECT_EQ(apu.length_counter(0), 63);

    // Not when the next step clocks length anyway.
    Apu other = fresh_apu();
    other.write(kNr12, 0xF0);
    other.write(kNr11, 0);
    other.write(kNr14, kTrigger);
    other.write(kNr14, kLengthEnable);  // next step is 0
    EXPECT_EQ(other.length_counter(0), 64);
}

TEST(Apu, TheExtraClockReachingZeroTurnsTheChannelOffUnlessItIsATrigger) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0xF0);
    apu.write(kNr11, 63);  // 1 step left
    apu.write(kNr14, kTrigger);
    apu.step_frame_sequencer();  // next step skips length
    apu.write(kNr14, kLengthEnable);
    EXPECT_FALSE(apu.channel_on(0));
    EXPECT_EQ(apu.length_counter(0), 0);
}

TEST(Apu, ATriggerWithLengthZeroLoadsTheFullLength) {
    Apu apu = fresh_apu();
    apu.write(kNr30, 0x80);
    apu.write(kNr31, 0);                         // 256
    apu.write(kNr34, kTrigger | kLengthEnable);  // next step 0: no extra clock
    EXPECT_EQ(apu.length_counter(2), 256);
    for (int i = 0; i < 512; ++i) {
        apu.step_frame_sequencer();
    }
    EXPECT_EQ(apu.length_counter(2), 0);
    EXPECT_FALSE(apu.channel_on(2));
    // Triggered again with length 0, before a step that skips length: 255.
    apu.step_frame_sequencer();  // next step is 1
    apu.write(kNr34, kTrigger | kLengthEnable);
    EXPECT_EQ(apu.length_counter(2), 255);
}

TEST(Apu, PoweringOnRestartsTheFrameSequencerAtStepZero) {
    Apu apu = fresh_apu();
    apu.step_frame_sequencer();  // next step 1
    apu.write(kNr52, 0x00);
    apu.write(kNr52, 0x80);  // next step 0 again
    apu.write(kNr12, 0xF0);
    apu.write(kNr11, 0);
    apu.write(kNr14, kTrigger | kLengthEnable);
    EXPECT_EQ(apu.length_counter(0), 64);  // no extra clock: step 0 clocks length
}

TEST(Apu, TheEnvelopeStepsOnStepSevenAtItsPeriod) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0x52);  // volume 5, down, period 2
    apu.write(kNr14, kTrigger);
    EXPECT_EQ(apu.volume(0), 5);
    advance_to_step(apu, 0, 7);
    apu.step_frame_sequencer();  // step 7: timer 2 -> 1
    EXPECT_EQ(apu.volume(0), 5);
    advance_to_step(apu, 0, 7);
    apu.step_frame_sequencer();  // timer 1 -> 0: volume down
    EXPECT_EQ(apu.volume(0), 4);
    for (int i = 0; i < 8 * 20; ++i) {
        apu.step_frame_sequencer();
    }
    EXPECT_EQ(apu.volume(0), 0);     // stops at 0
    EXPECT_TRUE(apu.channel_on(0));  // a silent channel is still on
}

TEST(Apu, AnEnvelopeWithPeriodZeroDoesNotChange) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0x78);  // volume 7, up, period 0
    apu.write(kNr14, kTrigger);
    for (int i = 0; i < 64; ++i) {
        apu.step_frame_sequencer();
    }
    EXPECT_EQ(apu.volume(0), 7);
}

TEST(Apu, SweepOverflowOnTriggerTurnsChannelOneOff) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0xF0);
    apu.write(kNr10, 0x01);  // period 0, add, shift 1
    apu.write(kNr13, 0xFF);
    apu.write(kNr14, kTrigger | 0x07);  // period 2047: 2047 + 1023 > 2047
    EXPECT_FALSE(apu.channel_on(0));
    // With shift 0 no calculation happens on trigger.
    apu.write(kNr10, 0x10);
    apu.write(kNr14, kTrigger | 0x07);
    EXPECT_TRUE(apu.channel_on(0));
}

TEST(Apu, TheSweepWritesTheNewPeriodBack) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0xF0);
    apu.write(kNr10, 0x11);  // period 1, add, shift 1
    apu.write(kNr13, 0x00);
    apu.write(kNr14, kTrigger | 0x01);  // period 0x100
    advance_to_step(apu, 0, 2);
    apu.step_frame_sequencer();  // step 2: sweep
    EXPECT_EQ(apu.square1_period(), 0x180);
    EXPECT_TRUE(apu.channel_on(0));
    EXPECT_EQ(apu.read(kNr14) & 0x07, 0x07);  // still write-only: reads 1s
}

TEST(Apu, LeavingNegateAfterANegatedCalculationTurnsChannelOneOff) {
    Apu apu = fresh_apu();
    apu.write(kNr12, 0xF0);
    apu.write(kNr10, 0x19);  // period 1, negate, shift 1
    apu.write(kNr13, 0x00);
    apu.write(kNr14, kTrigger | 0x04);  // the trigger's calculation uses negate
    ASSERT_TRUE(apu.channel_on(0));
    apu.write(kNr10, 0x11);  // negate cleared
    EXPECT_FALSE(apu.channel_on(0));
}

TEST(Apu, NothingIsClockedWhilePoweredOff) {
    Apu apu = fresh_apu();
    apu.write(kNr11, 60);
    apu.write(kNr52, 0x00);
    for (int i = 0; i < 64; ++i) {
        apu.step_frame_sequencer();
    }
    EXPECT_EQ(apu.length_counter(0), 4);
}

}  // namespace
