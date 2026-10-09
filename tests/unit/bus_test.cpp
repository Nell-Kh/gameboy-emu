#include "core/bus.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/interrupts.h"
#include "test_rom.h"

namespace {

using core::Bus;
using core::Interrupt;

TEST(Bus, RamStartsZeroed) {
    const Bus bus;
    EXPECT_EQ(bus.read8(0x8000), 0x00);
    EXPECT_EQ(bus.read8(0xC000), 0x00);
    EXPECT_EQ(bus.read8(0xFFFE), 0x00);
}

TEST(Bus, RamReadsBackWhatWasWritten) {
    Bus bus;
    bus.write8(0xC000, 0x42);
    EXPECT_EQ(bus.read8(0xC000), 0x42);
    bus.write8(0xC000, 0x99);
    EXPECT_EQ(bus.read8(0xC000), 0x99);
}

TEST(Bus, WriteDoesNotTouchNeighbours) {
    Bus bus;
    bus.write8(0xC234, 0xAB);
    EXPECT_EQ(bus.read8(0xC233), 0x00);
    EXPECT_EQ(bus.read8(0xC235), 0x00);
}

TEST(Bus, FirstAndLastRamAddressesAreUsable) {
    Bus bus;
    bus.write8(0x8000, 0x11);
    bus.write8(0xFFFE, 0x22);
    EXPECT_EQ(bus.read8(0x8000), 0x11);
    EXPECT_EQ(bus.read8(0xFFFE), 0x22);
}

TEST(Bus, WithoutACartridgeTheRomRegionReadsFF) {
    const Bus bus;
    EXPECT_EQ(bus.read8(0x0000), 0xFF);
    EXPECT_EQ(bus.read8(0x0100), 0xFF);
    EXPECT_EQ(bus.read8(0x7FFF), 0xFF);
}

TEST(Bus, CartridgeRomIsVisibleFromAddressZero) {
    Bus bus;
    bus.load_rom(test::make_rom({0x3E, 0x05, 0x76}));
    EXPECT_EQ(bus.read8(0x00FF), 0x00);
    EXPECT_EQ(bus.read8(0x0100), 0x3E);
    EXPECT_EQ(bus.read8(0x0101), 0x05);
    EXPECT_EQ(bus.read8(0x0102), 0x76);
}

TEST(Bus, RomCannotBeOverwritten) {
    Bus bus;
    bus.load_rom(test::make_rom({0x3E}));
    bus.write8(0x0100, 0x00);
    bus.write8(0x7FFF, 0x55);
    EXPECT_EQ(bus.read8(0x0100), 0x3E);
    EXPECT_EQ(bus.read8(0x7FFF), 0x00);
}

TEST(Bus, RomShorterThanTheRegionReadsFFPastItsEnd) {
    Bus bus;
    const std::vector<std::uint8_t> tiny = {0x01, 0x02};
    bus.load_rom(tiny);
    EXPECT_EQ(bus.read8(0x0001), 0x02);
    EXPECT_EQ(bus.read8(0x0002), 0xFF);
}

TEST(Bus, RomLongerThanTheRegionDoesNotLeakIntoRam) {
    Bus bus;
    const std::vector<std::uint8_t> big(0x10000, 0x5A);
    bus.load_rom(big);
    EXPECT_EQ(bus.read8(0x7FFF), 0x5A);
    EXPECT_EQ(bus.read8(0x8000), 0x00);
}

TEST(Bus, TickAccumulatesCycles) {
    Bus bus;
    EXPECT_EQ(bus.cycles(), 0U);
    bus.tick(4);
    bus.tick(4);
    EXPECT_EQ(bus.cycles(), 8U);
}

TEST(Bus, ReadingAndWritingDoNotAdvanceTime) {
    Bus bus;
    bus.write8(0xC000, 0x01);
    EXPECT_EQ(bus.read8(0xC000), 0x01);
    EXPECT_EQ(bus.cycles(), 0U);
}

TEST(Bus, InterruptFlagStartsWithVBlankRequested) {
    const Bus bus;
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xE1);
}

TEST(Bus, UnusedInterruptFlagBitsAlwaysReadAsOne) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xE0);
    bus.write8(Bus::kInterruptFlag, 0xFF);
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xFF);
}

TEST(Bus, InterruptEnableKeepsAllEightBits) {
    Bus bus;
    EXPECT_EQ(bus.read8(Bus::kInterruptEnable), 0x00);
    bus.write8(Bus::kInterruptEnable, 0xA5);
    EXPECT_EQ(bus.read8(Bus::kInterruptEnable), 0xA5);
}

TEST(Bus, RequestingAnInterruptSetsItsFlagBit) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    bus.request_interrupt(Interrupt::Timer);
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xE4);
    bus.request_interrupt(Interrupt::Joypad);
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xF4);
}

TEST(Bus, InterruptRequestedReportsASingleSource) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    EXPECT_FALSE(bus.interrupt_requested(Interrupt::Timer));
    bus.request_interrupt(Interrupt::Timer);
    EXPECT_TRUE(bus.interrupt_requested(Interrupt::Timer));
    EXPECT_FALSE(bus.interrupt_requested(Interrupt::Serial));
}

TEST(Bus, AcknowledgingClearsOnlyThatSource) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x1F);
    bus.acknowledge_interrupt(Interrupt::Timer);
    EXPECT_EQ(bus.read8(Bus::kInterruptFlag), 0xFB);
}

TEST(Bus, AnInterruptIsPendingOnlyWhenRequestedAndEnabled) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    bus.request_interrupt(Interrupt::Timer);
    EXPECT_EQ(bus.pending_interrupts(), 0x00);

    bus.write8(Bus::kInterruptEnable, 0x01);
    EXPECT_EQ(bus.pending_interrupts(), 0x00);

    bus.write8(Bus::kInterruptEnable, 0x05);
    EXPECT_EQ(bus.pending_interrupts(), 0x04);
}

TEST(Bus, UnusedEnableBitsNeverMakeAnInterruptPending) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    bus.write8(Bus::kInterruptEnable, 0xE0);
    EXPECT_EQ(bus.pending_interrupts(), 0x00);
}

}  // namespace
