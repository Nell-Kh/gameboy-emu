#include "core/cpu.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "core/bus.h"
#include "machine.h"

namespace {

using core::Bus;
using core::Cpu;
using test::kEntry;
using test::Machine;

TEST(Cpu, StartsInPostBootState) {
    Bus bus;
    const Cpu cpu(bus);
    EXPECT_EQ(cpu.registers().pc, kEntry);
    EXPECT_EQ(cpu.registers().sp, 0xFFFE);
    EXPECT_EQ(cpu.registers().af(), 0x01B0);
    EXPECT_FALSE(cpu.locked());
    EXPECT_FALSE(cpu.halted());
    EXPECT_FALSE(cpu.stopped());
    EXPECT_FALSE(cpu.interrupts_enabled());
}

TEST(Cpu, NopTakesOneMachineCycle) {
    Machine m({0x00});
    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().pc, kEntry + 1);
}

TEST(Cpu, StepReturnsExactlyTheTimeItPutOnTheBus) {
    // LD A, 0x07 ; LD [HL], A ; JP 0x0100
    Machine m({0x3E, 0x07, 0x77, 0xC3, 0x00, 0x01});
    m.reg().set_hl(0xC000);

    const std::uint64_t total = m.run(3);

    EXPECT_EQ(total, 32U);
    EXPECT_EQ(m.bus.cycles(), total);
    EXPECT_EQ(m.bus.read8(0xC000), 0x07);
    EXPECT_EQ(m.reg().pc, kEntry);
}

TEST(Cpu, UndefinedOpcodeLocksTheCpu) {
    // 0xD3 is one of the 11 opcodes that do not exist on the SM83.
    Machine m({0xD3, 0x00});
    EXPECT_EQ(m.step(), 4U);
    EXPECT_TRUE(m.cpu.locked());
}

TEST(Cpu, AllElevenUndefinedOpcodesLock) {
    for (const std::uint8_t opcode :
         {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}) {
        Machine m({opcode});
        m.step();
        EXPECT_TRUE(m.cpu.locked()) << "opcode " << static_cast<int>(opcode);
    }
}

TEST(Cpu, LockedCpuBurnsTimeButDoesNothingElse) {
    Machine m({0xD3, 0x3E});
    m.step();
    const std::uint16_t pc_when_locked = m.reg().pc;

    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().pc, pc_when_locked);
    EXPECT_EQ(m.bus.cycles(), 12U);
}

TEST(Cpu, HaltStopsExecution) {
    // HALT ; LD A, 0x55
    Machine m({0x76, 0x3E, 0x55});
    m.reg().a = 0x00;

    EXPECT_EQ(m.step(), 4U);
    EXPECT_TRUE(m.cpu.halted());
    EXPECT_EQ(m.run(3), 12U);
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.reg().pc, kEntry + 1);
}

TEST(Cpu, DiAndEiSwitchTheInterruptMasterEnable) {
    // EI ; NOP ; DI
    Machine m({0xFB, 0x00, 0xF3});
    m.run(2);
    EXPECT_TRUE(m.cpu.interrupts_enabled());
    m.step();
    EXPECT_FALSE(m.cpu.interrupts_enabled());
}

}  // namespace
