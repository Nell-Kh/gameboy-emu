// The APU's registers, power, frame sequencer, length counters, envelopes and
// sweep (docs/apu-notes.md, sections 3 and 4). Blargg's dmg_sound ROMs check
// the same behaviour on the whole machine (rom_tests).

#include "core/apu.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

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

// --- The square channels (docs/apu-notes.md, section 5) ---

constexpr std::uint16_t kNr21 = 0xFF16;
constexpr std::uint16_t kNr22 = 0xFF17;
constexpr std::uint16_t kNr23 = 0xFF18;
constexpr std::uint16_t kNr24 = 0xFF19;
constexpr std::uint32_t kTicksPerSecond = 4'194'304;

// Starts square channel `channel` (0 or 1) at full volume with the given
// duty (0-3) and period.
void start_square(Apu& apu, int channel, std::uint8_t duty, std::uint16_t period,
                  std::uint8_t volume = 15) {
    const std::uint16_t nr1 = channel == 0 ? kNr11 : kNr21;
    const std::uint16_t nr2 = channel == 0 ? kNr12 : kNr22;
    const std::uint16_t nr3 = channel == 0 ? kNr13 : kNr23;
    const std::uint16_t nr4 = channel == 0 ? kNr14 : kNr24;
    apu.write(nr1, static_cast<std::uint8_t>(duty << 6U));
    apu.write(nr2, static_cast<std::uint8_t>(volume << 4U));
    apu.write(nr3, static_cast<std::uint8_t>(period));
    apu.write(nr4, static_cast<std::uint8_t>(kTrigger | ((period >> 8U) & 0x07)));
}

// The channel's output at each of the next `steps` duty steps, for period
// 2047, where one step is 4 clock ticks.
std::vector<std::uint8_t> sample_steps(Apu& apu, int channel, int steps) {
    std::vector<std::uint8_t> out;
    for (int i = 0; i < steps; ++i) {
        out.push_back(apu.output(channel));
        apu.tick(4);
    }
    return out;
}

TEST(ApuSquare, EachDutyPatternPlaysItsEightSteps) {
    const std::array<std::vector<std::uint8_t>, 4> expected = {{
        {0, 0, 0, 0, 0, 0, 0, 15},       // 12.5 %
        {15, 0, 0, 0, 0, 0, 0, 15},      // 25 %
        {15, 0, 0, 0, 0, 15, 15, 15},    // 50 %
        {0, 15, 15, 15, 15, 15, 15, 0},  // 75 %
    }};
    for (const int channel : {0, 1}) {
        for (std::uint8_t duty = 0; duty < 4; ++duty) {
            Apu apu = fresh_apu();  // the duty step starts at 0
            start_square(apu, channel, duty, 2047);
            EXPECT_EQ(sample_steps(apu, channel, 8), expected.at(duty))
                << "channel " << channel << ", duty " << int{duty};
        }
    }
}

// Counts how often the output goes from 0 to above 0 in one emulated second.
int rising_edges_in_a_second(Apu& apu, int channel) {
    int edges = 0;
    std::uint8_t last = apu.output(channel);
    for (std::uint32_t t = 0; t < kTicksPerSecond; t += 4) {
        apu.tick(4);
        const std::uint8_t now = apu.output(channel);
        if (last == 0 && now != 0) {
            ++edges;
        }
        last = now;
    }
    return edges;
}

TEST(ApuSquare, ThePeriodSetsTheFrequency) {
    // 131072 / (2048 - period) Hz: period 1792 gives 512 Hz, 1024 gives 128 Hz.
    for (const auto& [period, hertz] : {std::pair{1792, 512}, std::pair{1024, 128}}) {
        Apu apu = fresh_apu();
        start_square(apu, 1, 2, static_cast<std::uint16_t>(period));
        EXPECT_NEAR(rising_edges_in_a_second(apu, 1), hertz, 1) << "period " << period;
    }
}

TEST(ApuSquare, TheOutputIsTheEnvelopeVolume) {
    Apu apu = fresh_apu();
    start_square(apu, 0, 3, 2047, 7);
    EXPECT_EQ(sample_steps(apu, 0, 8), (std::vector<std::uint8_t>{0, 7, 7, 7, 7, 7, 7, 0}));
}

TEST(ApuSquare, AChannelThatIsOffOutputsZero) {
    Apu apu = fresh_apu();
    start_square(apu, 0, 3, 2047);
    apu.tick(4);
    ASSERT_EQ(apu.output(0), 15);
    apu.write(kNr12, 0x00);  // DAC off
    EXPECT_EQ(apu.output(0), 0);
    apu.write(kNr52, 0x00);
    EXPECT_EQ(apu.output(1), 0);
}

TEST(ApuSquare, ATriggerKeepsTheDutyStepButPowerOffResetsIt) {
    Apu apu = fresh_apu();
    start_square(apu, 0, 2, 2047);
    apu.tick(4 * 5);  // at step 5
    start_square(apu, 0, 2, 2047);
    EXPECT_EQ(sample_steps(apu, 0, 3), (std::vector<std::uint8_t>{15, 15, 15}));  // steps 5-7

    apu.write(kNr52, 0x00);
    apu.write(kNr52, 0x80);
    start_square(apu, 0, 2, 2047);
    EXPECT_EQ(sample_steps(apu, 0, 2), (std::vector<std::uint8_t>{15, 0}));  // steps 0-1
}

TEST(ApuSquare, ANewPeriodTakesEffectAtTheNextStep) {
    Apu apu = fresh_apu();
    start_square(apu, 1, 2, 1024);  // 4096 ticks per step
    apu.write(kNr23, 0xFF);
    apu.write(kNr24, 0x07);  // period 2047, no trigger
    apu.tick(4095);
    EXPECT_EQ(apu.output(1), 15);  // still step 0
    apu.tick(1);                   // step 1, and from now on 4 ticks per step
    EXPECT_EQ(apu.output(1), 0);
    apu.tick(4 * 4);  // step 5
    EXPECT_EQ(apu.output(1), 15);
}

// --- The noise channel (docs/apu-notes.md, section 5) ---

constexpr std::uint16_t kNr43 = 0xFF22;

// Starts the noise channel at full volume with NR43 = `nr43`.
void start_noise(Apu& apu, std::uint8_t nr43) {
    apu.write(kNr42, 0xF0);
    apu.write(kNr43, nr43);
    apu.write(kNr44, kTrigger);
}

// The output after each of the next `count` shifts, for NR43 divisor code 0
// and shift 0, where one shift takes 8 clock ticks.
std::vector<std::uint8_t> noise_after_shifts(Apu& apu, int count) {
    std::vector<std::uint8_t> out;
    for (int i = 0; i < count; ++i) {
        apu.tick(8);
        out.push_back(apu.output(3));
    }
    return out;
}

// Worked out by hand: a trigger fills the register with ones, the XOR of
// bits 0 and 1 stays 0 while both are 1, so zeros shift in from the top; bit
// 0 (which the output inverts) first becomes 0 after the 15th shift.
TEST(ApuNoise, ATriggerFillsTheRegisterWithOnesSoTheOutputStartsLow) {
    Apu apu = fresh_apu();
    start_noise(apu, 0x00);
    EXPECT_EQ(apu.output(3), 0);
    const auto out = noise_after_shifts(apu, 15);
    for (int i = 0; i < 14; ++i) {
        EXPECT_EQ(out.at(static_cast<std::size_t>(i)), 0) << "after shift " << i + 1;
    }
    EXPECT_EQ(out.at(14), 15);
}

// True if `seq` repeats every `period` values over its whole length.
bool repeats_every(const std::vector<std::uint8_t>& seq, std::size_t period) {
    for (std::size_t i = 0; i + period < seq.size(); ++i) {
        if (seq[i] != seq[i + period]) {
            return false;
        }
    }
    return true;
}

TEST(ApuNoise, TheLongModeSequenceRepeatsEvery32767Shifts) {
    Apu apu = fresh_apu();
    start_noise(apu, 0x00);
    const auto seq = noise_after_shifts(apu, 32767 + 1000);
    EXPECT_TRUE(repeats_every(seq, 32767));
    // 32767 = 7 * 31 * 151: no shorter period divides it.
    for (const std::size_t shorter : {32767 / 7, 32767 / 31, 32767 / 151}) {
        EXPECT_FALSE(repeats_every(seq, shorter)) << shorter;
    }
}

TEST(ApuNoise, TheShortModeSequenceRepeatsEvery127Shifts) {
    Apu apu = fresh_apu();
    start_noise(apu, 0x08);  // NR43 bit 3: 7-bit mode
    const auto seq = noise_after_shifts(apu, 127 * 4);
    EXPECT_TRUE(repeats_every(seq, 127));
    EXPECT_FALSE(repeats_every(seq, 1));  // 127 is prime: the only shorter period
}

TEST(ApuNoise, TheDivisorAndShiftSetTheShiftRate) {
    // Divisor code 1, shift 2: 16 << 2 = 64 clock ticks per shift. The output
    // first goes high on the 15th shift.
    Apu apu = fresh_apu();
    start_noise(apu, 0x21);
    apu.tick(15 * 64 - 1);
    EXPECT_EQ(apu.output(3), 0);
    apu.tick(1);
    EXPECT_EQ(apu.output(3), 15);

    // Divisor code 5, shift 0: 80 ticks per shift.
    Apu other = fresh_apu();
    start_noise(other, 0x05);
    other.tick(15 * 80 - 1);
    EXPECT_EQ(other.output(3), 0);
    other.tick(1);
    EXPECT_EQ(other.output(3), 15);
}

TEST(ApuNoise, ClockShifts14And15StopTheRegister) {
    for (const std::uint8_t nr43 : {std::uint8_t{0xE0}, std::uint8_t{0xF0}}) {
        Apu apu = fresh_apu();
        start_noise(apu, nr43);
        apu.tick(kTicksPerSecond);
        EXPECT_EQ(apu.output(3), 0) << int{nr43};
        EXPECT_TRUE(apu.channel_on(3));
    }
}

TEST(ApuNoise, ARetriggerRestartsTheSequence) {
    Apu apu = fresh_apu();
    start_noise(apu, 0x00);
    const auto first = noise_after_shifts(apu, 40);
    start_noise(apu, 0x00);
    EXPECT_EQ(noise_after_shifts(apu, 40), first);
}

TEST(ApuNoise, TheOutputIsTheEnvelopeVolume) {
    Apu apu = fresh_apu();
    apu.write(kNr42, 0x90);  // volume 9
    apu.write(kNr43, 0x00);
    apu.write(kNr44, kTrigger);
    apu.tick(15 * 8);
    EXPECT_EQ(apu.output(3), 9);
}

// --- The wave channel (docs/apu-notes.md, sections 5 and 6) ---

constexpr std::uint16_t kNr32 = 0xFF1C;
constexpr std::uint16_t kNr33 = 0xFF1D;
constexpr std::uint16_t kWave = 0xFF30;

// Fills wave RAM with 0x01, 0x23, ... 0xEF: samples 0, 1, 2, ... 15, twice.
void load_counting_wave(Apu& apu) {
    for (std::uint16_t i = 0; i < 16; ++i) {
        apu.write(kWave + i, static_cast<std::uint8_t>(((2 * i) % 16) << 4U | ((2 * i + 1) % 16)));
    }
}

// Starts the wave channel at full volume with the given 11-bit period.
void start_wave(Apu& apu, std::uint16_t period, std::uint8_t volume_code = 1) {
    apu.write(kNr30, 0x80);
    apu.write(kNr32, static_cast<std::uint8_t>(volume_code << 5U));
    apu.write(kNr33, static_cast<std::uint8_t>(period));
    apu.write(kNr34, static_cast<std::uint8_t>(kTrigger | ((period >> 8U) & 0x07)));
}

// With period 2046 a sample lasts 4 clock ticks; the first fetch after a
// trigger comes one sample plus 4 ticks later (ADR-023).
constexpr std::uint32_t kSampleTicks = 4;
constexpr std::uint32_t kFirstFetch = kSampleTicks + 4;

TEST(ApuWave, ItPlaysWaveRamFromSampleOneHighNibbleFirst) {
    Apu apu = fresh_apu();
    load_counting_wave(apu);
    start_wave(apu, 2046);
    apu.tick(kFirstFetch);  // the first fetch: position 1, byte 0
    std::vector<std::uint8_t> out;
    for (int i = 0; i < 6; ++i) {
        out.push_back(apu.output(2));
        apu.tick(kSampleTicks);
    }
    EXPECT_EQ(out, (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6}));
}

TEST(ApuWave, ATriggerPlaysTheSampleAlreadyFetchedFirst) {
    Apu apu = fresh_apu();
    load_counting_wave(apu);
    start_wave(apu, 2046);
    apu.tick(kFirstFetch + 2 * kSampleTicks);  // position 3: byte 1 (0x23) fetched
    ASSERT_EQ(apu.output(2), 3);
    apu.write(kNr34, kTrigger | 0x07);  // retrigger (not near a fetch: no corruption)
    EXPECT_EQ(apu.output(2), 2);        // position 0: the high nibble of the old byte
}

TEST(ApuWave, TheVolumeCodeShiftsTheSample) {
    // Code 0 mutes; 1, 2, 3 shift right by 0, 1, 2. Sample 1 at position 1
    // after the first fetch; use a wave of all 0xFF to see the shifts.
    for (const auto& [code, expected] :
         {std::pair{0, 0}, std::pair{1, 15}, std::pair{2, 7}, std::pair{3, 3}}) {
        Apu apu = fresh_apu();
        for (std::uint16_t i = 0; i < 16; ++i) {
            apu.write(kWave + i, 0xFF);
        }
        start_wave(apu, 2046, static_cast<std::uint8_t>(code));
        apu.tick(kFirstFetch);
        EXPECT_EQ(apu.output(2), expected) << "code " << code;
    }
}

TEST(ApuWave, ThePeriodSetsTheFrequency) {
    // 65536 / (2048 - period) Hz for the 32-sample wave: period 1536 gives
    // 128 Hz. One rising edge per cycle with a wave of 16 zeros, 16 fifteens.
    Apu apu = fresh_apu();
    for (std::uint16_t i = 0; i < 16; ++i) {
        apu.write(kWave + i, i < 8 ? 0x00 : 0xFF);
    }
    start_wave(apu, 1536);
    int edges = 0;
    std::uint8_t last = apu.output(2);
    for (std::uint32_t t = 0; t < kTicksPerSecond; t += 4) {
        apu.tick(4);
        const std::uint8_t now = apu.output(2);
        edges += (last == 0 && now != 0) ? 1 : 0;
        last = now;
    }
    EXPECT_NEAR(edges, 128, 1);
}

TEST(ApuWave, WaveRamIsPlainMemoryWhileTheChannelIsOff) {
    Apu apu = fresh_apu();
    apu.write(kWave + 5, 0xA5);
    EXPECT_EQ(apu.read(kWave + 5), 0xA5);
}

// While the channel plays, every wave RAM address means the byte the channel
// is playing, and only right after a fetch (ADR-023); otherwise reads give
// 0xFF and writes are lost.
TEST(ApuWave, WhilePlayingTheCpuReachesOnlyTheCurrentByteRightAfterAFetch) {
    Apu apu = fresh_apu();
    load_counting_wave(apu);
    start_wave(apu, 2046);
    apu.tick(kFirstFetch + kSampleTicks);  // the fetch for position 2 (byte 1) just happened
    EXPECT_EQ(apu.read(kWave + 9), 0xFF);  // 0 ticks after the fetch: not yet
    apu.tick(2);
    EXPECT_EQ(apu.read(kWave + 5), 0x23);  // 2 ticks after: byte 1, not byte 5 (0xAB)
    apu.write(kWave + 12, 0x77);           // lands on byte 1
    apu.tick(1);
    EXPECT_EQ(apu.read(kWave + 9), 0xFF);  // 3 ticks after: gone again
    apu.write(kWave + 12, 0x99);           // lost
    apu.write(kNr30, 0x00);                // stop the channel to look at the RAM
    EXPECT_EQ(apu.read(kWave + 1), 0x77);
    EXPECT_EQ(apu.read(kWave + 12), 0x89);
}

// A retrigger 4 ticks before a fetch rewrites the start of wave RAM from the
// byte at the current position (ADR-023). Wave RAM holds 0x00, 0x11, ... 0xFF
// so every byte is different.
TEST(ApuWave, ARetriggerJustBeforeAFetchCorruptsTheStartOfWaveRam) {
    struct Case {
        std::uint32_t samples_played;  // after the first fetch, at position 1
        std::vector<std::uint8_t> first_four;
    };
    // Position 5 is byte 2, one of bytes 0-3: only byte 0 is rewritten.
    // Position 9 is byte 4: bytes 0-3 become bytes 4-7. Position 21 is byte
    // 10: bytes 0-3 become bytes 8-11.
    for (const Case& c : {Case{4, {0x22, 0x11, 0x22, 0x33}}, Case{8, {0x44, 0x55, 0x66, 0x77}},
                          Case{20, {0x88, 0x99, 0xAA, 0xBB}}}) {
        Apu apu = fresh_apu();
        for (std::uint16_t i = 0; i < 16; ++i) {
            apu.write(kWave + i, static_cast<std::uint8_t>(i * 0x11));
        }
        start_wave(apu, 2046);
        apu.tick(kFirstFetch + c.samples_played * kSampleTicks);  // the timer is now at 4
        apu.write(kNr34, kTrigger | 0x07);
        apu.write(kNr30, 0x00);
        std::vector<std::uint8_t> first_four;
        for (std::uint16_t i = 0; i < 4; ++i) {
            first_four.push_back(apu.read(kWave + i));
        }
        EXPECT_EQ(first_four, c.first_four) << "after " << c.samples_played << " samples";
    }
}

TEST(ApuWave, ARetriggerAtAnyOtherMomentLeavesWaveRamAlone) {
    Apu apu = fresh_apu();
    load_counting_wave(apu);
    start_wave(apu, 2046);
    apu.tick(kFirstFetch + 8 * kSampleTicks + 1);  // position 9, but the timer is at 3
    apu.write(kNr34, kTrigger | 0x07);
    apu.write(kNr30, 0x00);
    EXPECT_EQ(apu.read(kWave + 0), 0x01);
    EXPECT_EQ(apu.read(kWave + 3), 0x67);
}

}  // namespace
