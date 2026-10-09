// Rotates, shifts and single-bit operations.

#include <gtest/gtest.h>

#include <cstdint>

#include "machine.h"

namespace {

using test::Machine;

// Runs one CB-prefixed instruction on register B.
Machine run_cb_on_b(std::uint8_t cb_opcode, std::uint8_t b, bool carry_in = false) {
    Machine m({0xCB, cb_opcode});
    m.reg().b = b;
    m.reg().f = carry_in ? 0x10 : 0x00;
    m.step();
    return m;
}

TEST(CpuRotate, RlcMovesBit7ToBit0AndCarry) {
    Machine m = run_cb_on_b(0x00, 0x85);
    EXPECT_EQ(m.reg().b, 0x0B);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuRotate, RrcMovesBit0ToBit7AndCarry) {
    Machine m = run_cb_on_b(0x08, 0x01);
    EXPECT_EQ(m.reg().b, 0x80);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuRotate, RlShiftsTheOldCarryIn) {
    Machine m = run_cb_on_b(0x10, 0x80, true);
    EXPECT_EQ(m.reg().b, 0x01);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuRotate, RlWithoutCarryInCanReachZero) {
    Machine m = run_cb_on_b(0x10, 0x80, false);
    EXPECT_EQ(m.reg().b, 0x00);
    EXPECT_EQ(m.flags(), "Z--C");
}

TEST(CpuRotate, RrShiftsTheOldCarryIn) {
    Machine m = run_cb_on_b(0x18, 0x01, true);
    EXPECT_EQ(m.reg().b, 0x80);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuRotate, RotatingZeroSetsZero) {
    Machine m = run_cb_on_b(0x00, 0x00);
    EXPECT_EQ(m.flags(), "Z---");
}

TEST(CpuRotate, OneByteRotatesOnANeverSetZero) {
    for (const std::uint8_t opcode : {0x07, 0x0F, 0x17, 0x1F}) {  // RLCA RRCA RLA RRA
        Machine m({opcode});
        m.reg().a = 0x00;
        m.reg().f = 0x00;
        EXPECT_EQ(m.step(), 4U);
        EXPECT_EQ(m.reg().a, 0x00);
        EXPECT_EQ(m.flags(), "----") << "opcode " << static_cast<int>(opcode);
    }
}

TEST(CpuRotate, RlcaRotatesA) {
    Machine m({0x07});
    m.reg().a = 0x85;
    m.reg().f = 0xF0;
    m.step();
    EXPECT_EQ(m.reg().a, 0x0B);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuRotate, RraShiftsCarryIntoBit7) {
    Machine m({0x1F});
    m.reg().a = 0x02;
    m.reg().f = 0x10;
    m.step();
    EXPECT_EQ(m.reg().a, 0x81);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuShift, SlaShiftsLeftAndDropsBit7IntoCarry) {
    Machine m = run_cb_on_b(0x20, 0xC1);
    EXPECT_EQ(m.reg().b, 0x82);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuShift, SraKeepsTheSignBit) {
    Machine m = run_cb_on_b(0x28, 0x81);
    EXPECT_EQ(m.reg().b, 0xC0);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuShift, SrlClearsBit7) {
    Machine m = run_cb_on_b(0x38, 0x81);
    EXPECT_EQ(m.reg().b, 0x40);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuShift, SrlOfOneGivesZeroWithCarry) {
    Machine m = run_cb_on_b(0x38, 0x01);
    EXPECT_EQ(m.reg().b, 0x00);
    EXPECT_EQ(m.flags(), "Z--C");
}

TEST(CpuShift, SwapExchangesTheTwoNibbles) {
    Machine m = run_cb_on_b(0x30, 0xA5, true);
    EXPECT_EQ(m.reg().b, 0x5A);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuBit, BitSetsZeroWhenTheBitIsClear) {
    Machine m = run_cb_on_b(0x78, 0x7F);  // BIT 7, B
    EXPECT_EQ(m.flags(), "Z-H-");
    EXPECT_EQ(m.reg().b, 0x7F);
}

TEST(CpuBit, BitClearsZeroWhenTheBitIsSetAndKeepsCarry) {
    Machine m = run_cb_on_b(0x78, 0x80, true);
    EXPECT_EQ(m.flags(), "--HC");
}

TEST(CpuBit, EveryBitNumberTestsItsOwnBit) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto opcode = static_cast<std::uint8_t>(0x40U + (bit << 3U));  // BIT n, B
        Machine set = run_cb_on_b(opcode, static_cast<std::uint8_t>(1U << bit));
        EXPECT_EQ(set.flags(), "--H-") << "bit " << bit;
        Machine clear = run_cb_on_b(opcode, static_cast<std::uint8_t>(~(1U << bit)));
        EXPECT_EQ(clear.flags(), "Z-H-") << "bit " << bit;
    }
}

TEST(CpuBit, ResClearsOnlyItsBit) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto opcode = static_cast<std::uint8_t>(0x80U + (bit << 3U));  // RES n, B
        Machine m = run_cb_on_b(opcode, 0xFF);
        EXPECT_EQ(m.reg().b, static_cast<std::uint8_t>(~(1U << bit))) << "bit " << bit;
    }
}

TEST(CpuBit, SetSetsOnlyItsBit) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto opcode = static_cast<std::uint8_t>(0xC0U + (bit << 3U));  // SET n, B
        Machine m = run_cb_on_b(opcode, 0x00);
        EXPECT_EQ(m.reg().b, static_cast<std::uint8_t>(1U << bit)) << "bit " << bit;
    }
}

TEST(CpuBit, EachRegisterColumnTargetsItsOwnRegister) {
    // SET 0, r for r = B C D E H L (HL) A: opcodes 0xC0..0xC7.
    Machine m({0xCB, 0xC0, 0xCB, 0xC1, 0xCB, 0xC2, 0xCB, 0xC3, 0xCB, 0xC7});
    m.reg().a = 0;
    m.reg().set_bc(0);
    m.reg().set_de(0);
    m.run(5);
    EXPECT_EQ(m.reg().b, 0x01);
    EXPECT_EQ(m.reg().c, 0x01);
    EXPECT_EQ(m.reg().d, 0x01);
    EXPECT_EQ(m.reg().e, 0x01);
    EXPECT_EQ(m.reg().a, 0x01);
}

TEST(CpuBit, OperationsOnMemoryAtHl) {
    Machine m({0xCB, 0xFE, 0xCB, 0x46, 0xCB, 0x06});  // SET 7,[HL] ; BIT 0,[HL] ; RLC [HL]
    m.reg().set_hl(0xC000);
    m.reg().f = 0x00;

    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x80);
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.flags(), "Z-H-");
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x01);
    EXPECT_EQ(m.flags(), "---C");
}

}  // namespace
