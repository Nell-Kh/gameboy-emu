// Interrupts, HALT and STOP.
//
// An interrupt is serviced when three things are true at once: its bit is set
// in IF (requested), its bit is set in IE (enabled) and IME is on.

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <string>

#include "core/bus.h"
#include "core/interrupts.h"
#include "core/timer.h"
#include "machine.h"

namespace {

using core::Bus;
using core::Interrupt;
using test::kEntry;
using test::Machine;

constexpr std::uint8_t kNop = 0x00;
constexpr std::uint8_t kStopOp = 0x10;
constexpr std::uint8_t kIncA = 0x3C;
constexpr std::uint8_t kHalt = 0x76;
constexpr std::uint8_t kReti = 0xD9;
constexpr std::uint8_t kDi = 0xF3;
constexpr std::uint8_t kEi = 0xFB;

// Clears the request the boot ROM leaves behind and enables `enabled` in IE.
void set_interrupts(Machine& m, std::uint8_t enabled, std::uint8_t requested = 0x00) {
    m.bus.write8(Bus::kInterruptEnable, enabled);
    m.bus.write8(Bus::kInterruptFlag, requested);
}

// Runs the first two instructions, which every test here makes EI and NOP.
// After them IME is on.
void enable_ime(Machine& m) {
    m.run(2);
    ASSERT_TRUE(m.cpu.interrupts_enabled());
}

TEST(CpuInterrupt, IsNotServicedWhileImeIsOff) {
    Machine m({kNop, kNop});
    set_interrupts(m, 0x04, 0x04);

    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().pc, kEntry + 1);
    EXPECT_TRUE(m.bus.interrupt_requested(Interrupt::Timer));
}

TEST(CpuInterrupt, IsNotServicedUnlessEnabledInIe) {
    Machine m({kEi, kNop, kNop});
    set_interrupts(m, 0x01, 0x04);
    enable_ime(m);

    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().pc, kEntry + 3);
}

TEST(CpuInterrupt, ServicingPushesPcJumpsAndTurnsImeOff) {
    Machine m({kEi, kNop, kNop});
    set_interrupts(m, 0x04);
    m.reg().sp = 0xD000;
    enable_ime(m);
    m.bus.request_interrupt(Interrupt::Timer);

    EXPECT_EQ(m.step(), 20U);
    EXPECT_EQ(m.reg().pc, 0x0050);
    EXPECT_EQ(m.reg().sp, 0xCFFE);
    EXPECT_EQ(m.bus.read8(0xCFFF), 0x01);
    EXPECT_EQ(m.bus.read8(0xCFFE), 0x02);
    EXPECT_FALSE(m.cpu.interrupts_enabled());
    EXPECT_FALSE(m.bus.interrupt_requested(Interrupt::Timer));
}

struct Source {
    const char* name;
    Interrupt interrupt;
    std::uint16_t vector;
};

// Tells GoogleTest how to print a case in the test listing.
void PrintTo(const Source& source, std::ostream* os) {
    *os << source.name;
}

class CpuInterruptSource : public testing::TestWithParam<Source> {};

TEST_P(CpuInterruptSource, JumpsToItsOwnHandler) {
    const Source source = GetParam();
    Machine m({kEi, kNop});
    set_interrupts(m, 0x1F);
    m.reg().sp = 0xD000;
    enable_ime(m);
    m.bus.request_interrupt(source.interrupt);

    m.step();
    EXPECT_EQ(m.reg().pc, source.vector);
}

INSTANTIATE_TEST_SUITE_P(AllSources, CpuInterruptSource,
                         testing::Values(Source{"VBlank", Interrupt::VBlank, 0x0040},
                                         Source{"LcdStat", Interrupt::LcdStat, 0x0048},
                                         Source{"Timer", Interrupt::Timer, 0x0050},
                                         Source{"Serial", Interrupt::Serial, 0x0058},
                                         Source{"Joypad", Interrupt::Joypad, 0x0060}),
                         [](const testing::TestParamInfo<Source>& info) {
                             return std::string(info.param.name);
                         });

TEST(CpuInterrupt, LowestBitWinsAndTheOtherStaysRequested) {
    Machine m({kEi, kNop});
    set_interrupts(m, 0x1F);
    m.reg().sp = 0xD000;
    enable_ime(m);
    m.bus.request_interrupt(Interrupt::Joypad);
    m.bus.request_interrupt(Interrupt::LcdStat);

    m.step();
    EXPECT_EQ(m.reg().pc, 0x0048);
    EXPECT_FALSE(m.bus.interrupt_requested(Interrupt::LcdStat));
    EXPECT_TRUE(m.bus.interrupt_requested(Interrupt::Joypad));
}

TEST(CpuInterrupt, HandlerReturnsWithRetiAndTheNextOneIsServiced) {
    // The timer handler at 0x0050 is a single RETI.
    Machine m({});
    auto rom = test::make_rom({kEi, kNop, kNop});
    rom[0x0050] = kReti;
    m.bus.load_rom(rom);
    set_interrupts(m, 0x1F);
    m.reg().sp = 0xD000;
    enable_ime(m);
    m.bus.request_interrupt(Interrupt::Timer);
    m.bus.request_interrupt(Interrupt::Joypad);

    m.step();  // service the timer
    EXPECT_EQ(m.reg().pc, 0x0050);
    m.step();  // RETI: back to 0x0102 with IME on again
    EXPECT_EQ(m.reg().pc, kEntry + 2);
    EXPECT_TRUE(m.cpu.interrupts_enabled());
    m.step();  // so the joypad interrupt is serviced straight away
    EXPECT_EQ(m.reg().pc, 0x0060);
}

TEST(CpuInterrupt, EiTakesEffectAfterTheFollowingInstruction) {
    Machine m({kEi, kIncA, kIncA});
    set_interrupts(m, 0x04, 0x04);
    m.reg().a = 0;
    m.reg().sp = 0xD000;

    m.step();  // EI
    EXPECT_FALSE(m.cpu.interrupts_enabled());
    m.step();  // the instruction after EI still runs
    EXPECT_EQ(m.reg().a, 1);
    EXPECT_TRUE(m.cpu.interrupts_enabled());
    m.step();  // now the interrupt is serviced instead of the second INC
    EXPECT_EQ(m.reg().a, 1);
    EXPECT_EQ(m.reg().pc, 0x0050);
}

TEST(CpuInterrupt, DiRightAfterEiCancelsIt) {
    Machine m({kEi, kDi, kNop, kNop});
    set_interrupts(m, 0x04, 0x04);

    m.run(4);
    EXPECT_FALSE(m.cpu.interrupts_enabled());
    EXPECT_EQ(m.reg().pc, kEntry + 4);
}

TEST(CpuInterrupt, IfPushingPcOverwritesIeTheCpuJumpsToZero) {
    // SP = 0x0000, so the high byte of PC is pushed to 0xFFFF, which is IE.
    // PC is 0x0102, so IE becomes 0x01 and the timer is no longer enabled.
    Machine m({kEi, kNop});
    set_interrupts(m, 0x04);
    enable_ime(m);
    m.reg().sp = 0x0000;
    m.bus.request_interrupt(Interrupt::Timer);

    EXPECT_EQ(m.step(), 20U);
    EXPECT_EQ(m.reg().pc, 0x0000);
    EXPECT_TRUE(m.bus.interrupt_requested(Interrupt::Timer));
}

TEST(CpuHalt, SleepsUntilAnInterruptIsRequested) {
    Machine m({kHalt, kIncA});
    set_interrupts(m, 0x04);
    m.reg().a = 0;

    m.step();
    EXPECT_TRUE(m.cpu.halted());
    EXPECT_EQ(m.run(10), 40U);
    EXPECT_TRUE(m.cpu.halted());
    EXPECT_EQ(m.reg().a, 0);
}

TEST(CpuHalt, WithImeOffWakesAndCarriesOnWithoutServicing) {
    Machine m({kHalt, kIncA});
    set_interrupts(m, 0x04);
    m.reg().a = 0;
    m.step();
    m.bus.request_interrupt(Interrupt::Timer);

    m.step();
    EXPECT_FALSE(m.cpu.halted());
    EXPECT_EQ(m.reg().a, 1);
    EXPECT_EQ(m.reg().pc, kEntry + 2);
    EXPECT_TRUE(m.bus.interrupt_requested(Interrupt::Timer));
}

TEST(CpuHalt, WithImeOnWakesIntoTheHandlerAndReturnsAfterTheHalt) {
    Machine m({kEi, kNop, kHalt, kIncA});
    set_interrupts(m, 0x04);
    m.reg().sp = 0xD000;
    enable_ime(m);
    m.step();
    ASSERT_TRUE(m.cpu.halted());
    m.bus.request_interrupt(Interrupt::Timer);

    m.step();
    EXPECT_FALSE(m.cpu.halted());
    EXPECT_EQ(m.reg().pc, 0x0050);
    // The return address on the stack is the instruction after HALT.
    EXPECT_EQ(m.bus.read8(0xCFFF), 0x01);
    EXPECT_EQ(m.bus.read8(0xCFFE), 0x03);
}

TEST(CpuHalt, DoesNotWakeForAnInterruptThatIsNotEnabled) {
    Machine m({kHalt});
    set_interrupts(m, 0x01);
    m.step();
    m.bus.request_interrupt(Interrupt::Timer);

    m.run(5);
    EXPECT_TRUE(m.cpu.halted());
}

TEST(CpuHalt, TheTimerWakesAHaltedCpu) {
    Machine m({kHalt, kIncA});
    set_interrupts(m, 0x04);
    m.bus.write8(core::Timer::kDiv, 0x00);
    m.bus.write8(core::Timer::kTima, 0xFF);
    m.bus.write8(core::Timer::kTac, 0x05);  // one count every 16 ticks
    m.reg().a = 0;

    // HALT (4 ticks) plus three sleeping cycles (12) reach the overflow at
    // tick 16; the interrupt is requested one machine cycle later, at tick 20.
    m.run(5);
    EXPECT_TRUE(m.cpu.halted());
    EXPECT_EQ(m.bus.cycles(), 20U);
    m.step();
    EXPECT_FALSE(m.cpu.halted());
    EXPECT_EQ(m.reg().a, 1);
}

TEST(CpuHalt, BugWithImeOffAndInterruptAlreadyPendingRunsTheNextByteTwice) {
    Machine m({kHalt, kIncA, kNop});
    set_interrupts(m, 0x04, 0x04);
    m.reg().a = 0;

    m.step();
    EXPECT_FALSE(m.cpu.halted());
    m.step();  // INC A, but PC does not move past it
    EXPECT_EQ(m.reg().a, 1);
    EXPECT_EQ(m.reg().pc, kEntry + 1);
    m.step();  // INC A again
    EXPECT_EQ(m.reg().a, 2);
    EXPECT_EQ(m.reg().pc, kEntry + 2);
}

TEST(CpuStop, SkipsItsSecondByteResetsDivAndSleeps) {
    Machine m({kStopOp, kIncA, kIncA});
    m.reg().a = 0;
    m.bus.tick(1024);
    ASSERT_NE(m.bus.read8(core::Timer::kDiv), 0x00);

    EXPECT_EQ(m.step(), 4U);
    EXPECT_TRUE(m.cpu.stopped());
    EXPECT_EQ(m.reg().pc, kEntry + 2);
    EXPECT_EQ(m.bus.read8(core::Timer::kDiv), 0x00);

    m.run(5);
    EXPECT_TRUE(m.cpu.stopped());
    EXPECT_EQ(m.reg().a, 0);
}

TEST(CpuStop, OnlyAButtonPressEndsIt) {
    Machine m({kStopOp, 0x00, kIncA});
    set_interrupts(m, 0x1F);
    m.reg().a = 0;
    m.step();

    m.bus.request_interrupt(Interrupt::Timer);
    m.run(3);
    EXPECT_TRUE(m.cpu.stopped());

    m.bus.write8(Bus::kInterruptFlag, 0x00);
    m.bus.request_interrupt(Interrupt::Joypad);
    m.step();
    EXPECT_FALSE(m.cpu.stopped());
    EXPECT_EQ(m.reg().a, 1);
}

}  // namespace
