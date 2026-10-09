#include "core/cpu.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "core/bus.h"
#include "test_rom.h"

namespace {

using core::Bus;
using core::Cpu;
using test::kEntry;

TEST(Cpu, StartsInPostBootState) {
    Bus bus;
    const Cpu cpu(bus);
    EXPECT_EQ(cpu.registers().pc, kEntry);
    EXPECT_EQ(cpu.registers().sp, 0xFFFE);
    EXPECT_EQ(cpu.registers().af(), 0x01B0);
    EXPECT_FALSE(cpu.locked());
}

TEST(Cpu, NopTakesOneMachineCycle) {
    Bus bus;
    Cpu cpu(bus);
    bus.load_rom(test::make_rom({0x00}));

    EXPECT_EQ(cpu.step(), 4U);
    EXPECT_EQ(cpu.registers().pc, kEntry + 1);
    EXPECT_FALSE(cpu.locked());
}

TEST(Cpu, LoadImmediateIntoA) {
    Bus bus;
    Cpu cpu(bus);
    bus.load_rom(test::make_rom({0x3E, 0x42}));

    EXPECT_EQ(cpu.step(), 8U);
    EXPECT_EQ(cpu.registers().a, 0x42);
    EXPECT_EQ(cpu.registers().pc, kEntry + 2);
}

TEST(Cpu, StoreAAtAddressInHl) {
    Bus bus;
    Cpu cpu(bus);
    bus.load_rom(test::make_rom({0x77}));
    cpu.registers().a = 0x5A;
    cpu.registers().set_hl(0xC123);

    EXPECT_EQ(cpu.step(), 8U);
    EXPECT_EQ(bus.read8(0xC123), 0x5A);
    EXPECT_EQ(cpu.registers().pc, kEntry + 1);
}

TEST(Cpu, JumpReadsItsTargetLowByteFirst) {
    Bus bus;
    Cpu cpu(bus);
    bus.load_rom(test::make_rom({0xC3, 0x34, 0x12}));

    EXPECT_EQ(cpu.step(), 16U);
    EXPECT_EQ(cpu.registers().pc, 0x1234);
}

TEST(Cpu, StepReturnsExactlyTheTimeItPutOnTheBus) {
    Bus bus;
    Cpu cpu(bus);
    // LD A, 0x07 ; LD (HL), A ; JP 0x0100
    bus.load_rom(test::make_rom({0x3E, 0x07, 0x77, 0xC3, 0x00, 0x01}));
    cpu.registers().set_hl(0xC000);

    std::uint64_t total = 0;
    total += cpu.step();
    total += cpu.step();
    total += cpu.step();

    EXPECT_EQ(total, 32U);
    EXPECT_EQ(bus.cycles(), total);
    EXPECT_EQ(bus.read8(0xC000), 0x07);
    EXPECT_EQ(cpu.registers().pc, kEntry);
}

TEST(Cpu, UndefinedOpcodeLocksTheCpu) {
    Bus bus;
    Cpu cpu(bus);
    // 0xD3 is one of the 11 opcodes that do not exist on the SM83.
    bus.load_rom(test::make_rom({0xD3, 0x00}));

    EXPECT_EQ(cpu.step(), 4U);
    EXPECT_TRUE(cpu.locked());
}

TEST(Cpu, LockedCpuBurnsTimeButDoesNothingElse) {
    Bus bus;
    Cpu cpu(bus);
    bus.load_rom(test::make_rom({0xD3, 0x3E}));
    static_cast<void>(cpu.step());
    const std::uint16_t pc_when_locked = cpu.registers().pc;

    EXPECT_EQ(cpu.step(), 4U);
    EXPECT_EQ(cpu.step(), 4U);
    EXPECT_EQ(cpu.registers().pc, pc_when_locked);
    EXPECT_EQ(bus.cycles(), 12U);
}

}  // namespace
