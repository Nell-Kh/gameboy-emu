#include "core/bus.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/interrupts.h"
#include "core/ppu.h"
#include "core/serial.h"
#include "core/timer.h"
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

TEST(Bus, EchoRamMirrorsWorkRamBothWays) {
    Bus bus;
    bus.write8(0xC123, 0x42);
    EXPECT_EQ(bus.read8(0xE123), 0x42);
    bus.write8(0xE456, 0x99);
    EXPECT_EQ(bus.read8(0xC456), 0x99);
}

TEST(Bus, EchoRamCoversExactlyE000ToFdff) {
    Bus bus;
    bus.write8(0xC000, 0x11);
    bus.write8(0xDDFF, 0x22);
    EXPECT_EQ(bus.read8(0xE000), 0x11);
    EXPECT_EQ(bus.read8(0xFDFF), 0x22);

    // 0xFE00 is the sprite table, not a mirror of 0xDE00.
    bus.write8(0xDE00, 0x33);
    EXPECT_EQ(bus.read8(0xFE00), 0x00);
    bus.write8(0xFE00, 0x44);
    EXPECT_EQ(bus.read8(0xDE00), 0x33);
}

TEST(Bus, UnusableRegionReadsZeroAndIgnoresWrites) {
    Bus bus;
    for (std::uint32_t address = 0xFEA0; address <= 0xFEFF; ++address) {
        const auto a = static_cast<std::uint16_t>(address);
        bus.write8(a, 0x5A);
        EXPECT_EQ(bus.read8(a), 0x00) << "address " << address;
    }
}

TEST(Bus, SpriteTableIsWritableUpToFe9f) {
    Bus bus;
    bus.write8(0xFE9F, 0x77);
    EXPECT_EQ(bus.read8(0xFE9F), 0x77);
}

TEST(Bus, IoAddressesWithNoRegisterReadFFAndIgnoreWrites) {
    Bus bus;
    for (const std::uint16_t address :
         {0xFF03, 0xFF08, 0xFF0E, 0xFF15, 0xFF1F, 0xFF27, 0xFF2F, 0xFF4C, 0xFF50, 0xFF7F}) {
        bus.write8(address, 0x00);
        EXPECT_EQ(bus.read8(address), 0xFF) << "address " << address;
    }
}

TEST(Bus, MappedNeighboursOfUnmappedIoStillWork) {
    Bus bus;
    // Wave RAM (0xFF30-0xFF3F) is real memory between two unmapped ranges.
    bus.write8(0xFF30, 0x12);
    bus.write8(0xFF3F, 0x34);
    EXPECT_EQ(bus.read8(0xFF30), 0x12);
    EXPECT_EQ(bus.read8(0xFF3F), 0x34);
    // High RAM starts right after the last unmapped I/O address.
    bus.write8(0xFF80, 0x56);
    EXPECT_EQ(bus.read8(0xFF80), 0x56);
}

TEST(Bus, UnemulatedRegistersReadTheirMissingAndWriteOnlyBitsAsOne) {
    Bus bus;
    struct Case {
        std::uint16_t address;
        std::uint8_t ones;
    };
    for (const Case c :
         {Case{0xFF10, 0x80}, Case{0xFF11, 0x3F}, Case{0xFF13, 0xFF}, Case{0xFF14, 0xBF},
          Case{0xFF1A, 0x7F}, Case{0xFF1C, 0x9F}, Case{0xFF20, 0xFF}, Case{0xFF23, 0xBF},
          Case{0xFF26, 0x70}, Case{0xFF41, 0x80}}) {
        bus.write8(c.address, 0x00);
        EXPECT_EQ(bus.read8(c.address), c.ones) << "address " << c.address;
        bus.write8(c.address, 0xFF);
        EXPECT_EQ(bus.read8(c.address), 0xFF) << "address " << c.address;
    }
}

TEST(Bus, FullyReadableUnemulatedRegistersReadBackAsWritten) {
    Bus bus;
    for (const std::uint16_t address : {0xFF12, 0xFF24, 0xFF42, 0xFF47}) {
        bus.write8(address, 0x00);
        EXPECT_EQ(bus.read8(address), 0x00) << "address " << address;
        bus.write8(address, 0xA5);
        EXPECT_EQ(bus.read8(address), 0xA5) << "address " << address;
    }
}

TEST(Bus, JoypadReadsNoButtonsPressedAndKeepsTheSelectBits) {
    Bus bus;
    bus.write8(0xFF00, 0x00);
    EXPECT_EQ(bus.read8(0xFF00), 0xCF);
    bus.write8(0xFF00, 0x20);
    EXPECT_EQ(bus.read8(0xFF00), 0xEF);
    bus.write8(0xFF00, 0x10);
    EXPECT_EQ(bus.read8(0xFF00), 0xDF);
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

TEST(Bus, SerialTransferIsCapturedAndRaisesItsInterrupt) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    bus.write8(core::Serial::kData, 'P');
    bus.write8(core::Serial::kControl, 0x81);
    EXPECT_EQ(bus.serial_output(), "P");

    bus.tick(core::Serial::kTicksPerTransfer - 4);
    EXPECT_FALSE(bus.interrupt_requested(Interrupt::Serial));
    bus.tick(4);
    EXPECT_TRUE(bus.interrupt_requested(Interrupt::Serial));
}

TEST(Bus, TimerOverflowRaisesTheTimerInterrupt) {
    Bus bus;
    bus.write8(Bus::kInterruptFlag, 0x00);
    bus.write8(core::Timer::kDiv, 0x00);
    bus.write8(core::Timer::kTima, 0xFF);
    bus.write8(core::Timer::kTac, 0x05);  // enabled, one count every 16 ticks

    bus.tick(16);
    EXPECT_FALSE(bus.interrupt_requested(Interrupt::Timer));
    bus.tick(4);
    EXPECT_TRUE(bus.interrupt_requested(Interrupt::Timer));
}

TEST(Bus, LyIsRoutedToThePpuAndAdvancesWithTime) {
    Bus bus;
    EXPECT_EQ(bus.read8(core::Ppu::kLy), 0x00);
    bus.tick(core::Ppu::kTicksPerLine * 3);
    EXPECT_EQ(bus.read8(core::Ppu::kLy), 3);
    bus.write8(core::Ppu::kLy, 0x55);
    EXPECT_EQ(bus.read8(core::Ppu::kLy), 3);
}

}  // namespace
