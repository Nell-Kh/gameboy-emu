// 8-bit and 16-bit arithmetic and logic: results and flags.

#include <gtest/gtest.h>

#include <cstdint>

#include "core/registers.h"
#include "machine.h"

namespace {

using core::Flag;
using test::Machine;

// Runs `ADD A, n8` style instructions: opcode followed by one operand byte.
Machine run_on_a(std::uint8_t opcode, std::uint8_t a, std::uint8_t operand, bool carry_in = false) {
    Machine m({opcode, operand});
    m.reg().a = a;
    m.reg().f = carry_in ? 0x10 : 0x00;
    m.step();
    return m;
}

constexpr std::uint8_t kAddN = 0xC6;
constexpr std::uint8_t kAdcN = 0xCE;
constexpr std::uint8_t kSubN = 0xD6;
constexpr std::uint8_t kSbcN = 0xDE;
constexpr std::uint8_t kAndN = 0xE6;
constexpr std::uint8_t kXorN = 0xEE;
constexpr std::uint8_t kOrN = 0xF6;
constexpr std::uint8_t kCpN = 0xFE;

TEST(CpuAdd, PlainSumSetsNoFlags) {
    Machine m = run_on_a(kAddN, 0x12, 0x34);
    EXPECT_EQ(m.reg().a, 0x46);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuAdd, CarryOutOfBit3SetsHalfCarry) {
    Machine m = run_on_a(kAddN, 0x0F, 0x01);
    EXPECT_EQ(m.reg().a, 0x10);
    EXPECT_EQ(m.flags(), "--H-");
}

TEST(CpuAdd, CarryOutOfBit7SetsCarry) {
    Machine m = run_on_a(kAddN, 0xF0, 0x20);
    EXPECT_EQ(m.reg().a, 0x10);
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuAdd, WrappingToZeroSetsZeroHalfAndCarry) {
    Machine m = run_on_a(kAddN, 0xFF, 0x01);
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.flags(), "Z-HC");
}

TEST(CpuAdd, IgnoresTheIncomingCarry) {
    Machine m = run_on_a(kAddN, 0x01, 0x01, true);
    EXPECT_EQ(m.reg().a, 0x02);
}

TEST(CpuAdc, AddsTheIncomingCarry) {
    Machine m = run_on_a(kAdcN, 0x01, 0x01, true);
    EXPECT_EQ(m.reg().a, 0x03);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuAdc, CarryInAloneCanCauseHalfCarry) {
    Machine m = run_on_a(kAdcN, 0x0F, 0x00, true);
    EXPECT_EQ(m.reg().a, 0x10);
    EXPECT_EQ(m.flags(), "--H-");
}

TEST(CpuAdc, CarryInAloneCanWrapToZero) {
    Machine m = run_on_a(kAdcN, 0xFF, 0x00, true);
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.flags(), "Z-HC");
}

TEST(CpuSub, PlainDifferenceSetsOnlySubtract) {
    Machine m = run_on_a(kSubN, 0x46, 0x12);
    EXPECT_EQ(m.reg().a, 0x34);
    EXPECT_EQ(m.flags(), "-N--");
}

TEST(CpuSub, BorrowFromBit4SetsHalfCarry) {
    Machine m = run_on_a(kSubN, 0x10, 0x01);
    EXPECT_EQ(m.reg().a, 0x0F);
    EXPECT_EQ(m.flags(), "-NH-");
}

TEST(CpuSub, GoingBelowZeroSetsCarry) {
    Machine m = run_on_a(kSubN, 0x10, 0x20);
    EXPECT_EQ(m.reg().a, 0xF0);
    EXPECT_EQ(m.flags(), "-N-C");
}

TEST(CpuSub, EqualOperandsGiveZero) {
    Machine m = run_on_a(kSubN, 0x42, 0x42);
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.flags(), "ZN--");
}

TEST(CpuSbc, SubtractsTheIncomingCarry) {
    Machine m = run_on_a(kSbcN, 0x10, 0x01, true);
    EXPECT_EQ(m.reg().a, 0x0E);
    EXPECT_EQ(m.flags(), "-NH-");
}

TEST(CpuSbc, CarryInAloneCanBorrowAllTheWay) {
    Machine m = run_on_a(kSbcN, 0x00, 0x00, true);
    EXPECT_EQ(m.reg().a, 0xFF);
    EXPECT_EQ(m.flags(), "-NHC");
}

TEST(CpuSbc, OperandFFWithCarryInLeavesAUnchangedButBorrows) {
    Machine m = run_on_a(kSbcN, 0x05, 0xFF, true);
    EXPECT_EQ(m.reg().a, 0x05);
    EXPECT_EQ(m.flags(), "-NHC");
}

TEST(CpuCp, SetsFlagsLikeSubButKeepsA) {
    Machine m = run_on_a(kCpN, 0x10, 0x20);
    EXPECT_EQ(m.reg().a, 0x10);
    EXPECT_EQ(m.flags(), "-N-C");
}

TEST(CpuCp, EqualValuesSetZero) {
    Machine m = run_on_a(kCpN, 0x42, 0x42);
    EXPECT_EQ(m.reg().a, 0x42);
    EXPECT_EQ(m.flags(), "ZN--");
}

TEST(CpuLogic, AndAlwaysSetsHalfCarry) {
    Machine m = run_on_a(kAndN, 0xF0, 0x3C, true);
    EXPECT_EQ(m.reg().a, 0x30);
    EXPECT_EQ(m.flags(), "--H-");
}

TEST(CpuLogic, AndWithNoCommonBitsIsZero) {
    Machine m = run_on_a(kAndN, 0xF0, 0x0F);
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.flags(), "Z-H-");
}

TEST(CpuLogic, OrClearsEverythingButZero) {
    Machine m = run_on_a(kOrN, 0xF0, 0x0C, true);
    EXPECT_EQ(m.reg().a, 0xFC);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuLogic, OrOfZerosIsZero) {
    Machine m = run_on_a(kOrN, 0x00, 0x00);
    EXPECT_EQ(m.flags(), "Z---");
}

TEST(CpuLogic, XorTogglesBits) {
    Machine m = run_on_a(kXorN, 0xFF, 0x0F, true);
    EXPECT_EQ(m.reg().a, 0xF0);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuLogic, XorAWithItselfClearsA) {
    Machine m({0xAF});  // XOR A, A
    m.reg().a = 0x5A;
    m.step();
    EXPECT_EQ(m.reg().a, 0x00);
    EXPECT_EQ(m.flags(), "Z---");
}

TEST(CpuAluOperands, TakesTheOperandFromARegister) {
    Machine m({0x80});  // ADD A, B
    m.reg().a = 0x01;
    m.reg().b = 0x02;
    m.reg().f = 0x00;
    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().a, 0x03);
}

TEST(CpuAluOperands, TakesTheOperandFromMemoryAtHl) {
    Machine m({0x86});  // ADD A, [HL]
    m.bus.write8(0xC000, 0x02);
    m.reg().a = 0x01;
    m.reg().f = 0x00;
    m.reg().set_hl(0xC000);
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.reg().a, 0x03);
}

TEST(CpuIncDec, IncSetsHalfCarryWhenTheLowNibbleWraps) {
    Machine m({0x04});  // INC B
    m.reg().b = 0x0F;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().b, 0x10);
    EXPECT_EQ(m.flags(), "--H-");
}

TEST(CpuIncDec, IncWrapsToZeroWithoutTouchingCarry) {
    Machine m({0x04});
    m.reg().b = 0xFF;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().b, 0x00);
    EXPECT_EQ(m.flags(), "Z-H-");
}

TEST(CpuIncDec, IncKeepsAnExistingCarry) {
    Machine m({0x04});
    m.reg().b = 0x01;
    m.reg().f = 0x10;
    m.step();
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuIncDec, DecSetsHalfCarryWhenBorrowingFromTheHighNibble) {
    Machine m({0x05});  // DEC B
    m.reg().b = 0x10;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().b, 0x0F);
    EXPECT_EQ(m.flags(), "-NH-");
}

TEST(CpuIncDec, DecToZeroSetsZero) {
    Machine m({0x05});
    m.reg().b = 0x01;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().b, 0x00);
    EXPECT_EQ(m.flags(), "ZN--");
}

TEST(CpuIncDec, DecWrapsBelowZeroWithoutTouchingCarry) {
    Machine m({0x05});
    m.reg().b = 0x00;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().b, 0xFF);
    EXPECT_EQ(m.flags(), "-NH-");
}

TEST(CpuIncDec, IncMemoryAtHlReadsThenWrites) {
    Machine m({0x34});  // INC [HL]
    m.bus.write8(0xC000, 0x41);
    m.reg().set_hl(0xC000);
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x42);
}

TEST(CpuIncDec, SixteenBitIncAndDecTouchNoFlags) {
    Machine m({0x03, 0x0B, 0x0B});  // INC BC ; DEC BC ; DEC BC
    m.reg().set_bc(0xFFFF);
    m.reg().f = 0xF0;
    m.step();
    EXPECT_EQ(m.reg().bc(), 0x0000);
    m.run(2);
    EXPECT_EQ(m.reg().bc(), 0xFFFE);
    EXPECT_EQ(m.reg().f, 0xF0);
}

// DAA: decimal adjust. Add or subtract two two-digit decimal numbers stored as
// BCD (0x15 means fifteen), then DAA must give the BCD of the true result.
constexpr std::uint8_t to_bcd(int value) {
    return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

TEST(CpuDaa, FixesEveryTwoDigitDecimalAddition) {
    for (int x = 0; x < 100; ++x) {
        for (int y = 0; y < 100; ++y) {
            Machine m({kAddN, to_bcd(y), 0x27});  // ADD A, y ; DAA
            m.reg().a = to_bcd(x);
            m.run(2);
            ASSERT_EQ(m.reg().a, to_bcd((x + y) % 100)) << x << " + " << y;
            ASSERT_EQ(m.reg().flag(Flag::C), x + y > 99) << x << " + " << y;
            ASSERT_EQ(m.reg().flag(Flag::Z), (x + y) % 100 == 0) << x << " + " << y;
            ASSERT_FALSE(m.reg().flag(Flag::H));
        }
    }
}

TEST(CpuDaa, FixesEveryTwoDigitDecimalSubtraction) {
    for (int x = 0; x < 100; ++x) {
        for (int y = 0; y < 100; ++y) {
            Machine m({kSubN, to_bcd(y), 0x27});  // SUB A, y ; DAA
            m.reg().a = to_bcd(x);
            m.run(2);
            ASSERT_EQ(m.reg().a, to_bcd((x - y + 100) % 100)) << x << " - " << y;
            ASSERT_EQ(m.reg().flag(Flag::C), x < y) << x << " - " << y;
            ASSERT_TRUE(m.reg().flag(Flag::N));
            ASSERT_FALSE(m.reg().flag(Flag::H));
        }
    }
}

TEST(CpuDaa, KnownExample) {
    Machine m({kAddN, 0x27, 0x27});  // 0x15 + 0x27 = 0x3C, DAA -> 0x42
    m.reg().a = 0x15;
    m.step();
    EXPECT_EQ(m.reg().a, 0x3C);
    m.step();
    EXPECT_EQ(m.reg().a, 0x42);
}

TEST(CpuMisc, CplInvertsAAndSetsNAndH) {
    Machine m({0x2F});
    m.reg().a = 0x35;
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().a, 0xCA);
    EXPECT_EQ(m.flags(), "-NH-");
}

TEST(CpuMisc, ScfSetsCarryAndKeepsZero) {
    Machine m({0x37});
    m.reg().f = 0xE0;
    m.step();
    EXPECT_EQ(m.flags(), "Z--C");
}

TEST(CpuMisc, CcfFlipsCarry) {
    Machine m({0x3F, 0x3F});
    m.reg().f = 0x70;
    m.step();
    EXPECT_EQ(m.flags(), "----");
    m.step();
    EXPECT_EQ(m.flags(), "---C");
}

TEST(CpuAdd16, AddHlSetsHalfCarryFromBit11) {
    Machine m({0x09});  // ADD HL, BC
    m.reg().set_hl(0x0FFF);
    m.reg().set_bc(0x0001);
    m.reg().f = 0x00;
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.reg().hl(), 0x1000);
    EXPECT_EQ(m.flags(), "--H-");
}

TEST(CpuAdd16, AddHlCarryOutOfTheLowByteIsNotHalfCarry) {
    Machine m({0x09});
    m.reg().set_hl(0x00FF);
    m.reg().set_bc(0x0001);
    m.reg().f = 0x00;
    m.step();
    EXPECT_EQ(m.reg().hl(), 0x0100);
    EXPECT_EQ(m.flags(), "----");
}

TEST(CpuAdd16, AddHlSetsCarryFromBit15AndKeepsZero) {
    Machine m({0x09});
    m.reg().set_hl(0x8000);
    m.reg().set_bc(0x8000);
    m.reg().f = 0x80;
    m.step();
    EXPECT_EQ(m.reg().hl(), 0x0000);
    EXPECT_EQ(m.flags(), "Z--C");
}

TEST(CpuAdd16, AddHlToItselfDoubles) {
    Machine m({0x29});  // ADD HL, HL
    m.reg().set_hl(0x1234);
    m.step();
    EXPECT_EQ(m.reg().hl(), 0x2468);
}

TEST(CpuAdd16, AddSpTakesASignedOffset) {
    Machine m({0xE8, 0xFE});  // ADD SP, -2
    m.reg().sp = 0xD000;
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().sp, 0xCFFE);
}

TEST(CpuAdd16, AddSpFlagsComeFromTheLowByte) {
    Machine m({0xE8, 0x01});  // ADD SP, +1
    m.reg().sp = 0x00FF;
    m.reg().f = 0xF0;
    m.step();
    EXPECT_EQ(m.reg().sp, 0x0100);
    EXPECT_EQ(m.flags(), "--HC");
}

TEST(CpuAdd16, AddSpNegativeOffsetCanStillSetCarry) {
    Machine m({0xE8, 0xFF});  // ADD SP, -1
    m.reg().sp = 0x0001;
    m.step();
    EXPECT_EQ(m.reg().sp, 0x0000);
    EXPECT_EQ(m.flags(), "--HC");
}

TEST(CpuAdd16, LdHlSpPlusOffsetLeavesSpAlone) {
    Machine m({0xF8, 0x05});  // LD HL, SP+5
    m.reg().sp = 0xD00E;
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().hl(), 0xD013);
    EXPECT_EQ(m.reg().sp, 0xD00E);
    EXPECT_EQ(m.flags(), "--H-");
}

}  // namespace
