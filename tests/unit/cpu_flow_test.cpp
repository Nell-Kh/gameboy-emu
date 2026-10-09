// Jumps, calls, returns and restarts.

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <string>

#include "machine.h"

namespace {

using test::kEntry;
using test::Machine;

constexpr std::uint8_t kZeroSet = 0x80;
constexpr std::uint8_t kCarrySet = 0x10;

TEST(CpuJump, AbsoluteJump) {
    Machine m({0xC3, 0x34, 0x12});  // JP 0x1234
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().pc, 0x1234);
}

TEST(CpuJump, JumpToHlIsOneCycle) {
    Machine m({0xE9});  // JP HL
    m.reg().set_hl(0x4567);
    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().pc, 0x4567);
}

struct ConditionCase {
    const char* name;
    std::uint8_t jp_opcode;
    std::uint8_t flags_when_true;
    std::uint8_t flags_when_false;
};

// Tells GoogleTest how to print a case in the test listing.
void PrintTo(const ConditionCase& c, std::ostream* os) {
    *os << c.name;
}

class CpuCondition : public testing::TestWithParam<ConditionCase> {};

TEST_P(CpuCondition, JpTakenAndNotTaken) {
    const ConditionCase c = GetParam();
    Machine taken({c.jp_opcode, 0x34, 0x12});
    taken.reg().f = c.flags_when_true;
    EXPECT_EQ(taken.step(), 16U);
    EXPECT_EQ(taken.reg().pc, 0x1234);

    Machine skipped({c.jp_opcode, 0x34, 0x12});
    skipped.reg().f = c.flags_when_false;
    EXPECT_EQ(skipped.step(), 12U);
    EXPECT_EQ(skipped.reg().pc, kEntry + 3);
}

INSTANTIATE_TEST_SUITE_P(AllConditions, CpuCondition,
                         testing::Values(ConditionCase{"NZ", 0xC2, 0x00, kZeroSet},
                                         ConditionCase{"Z", 0xCA, kZeroSet, 0x00},
                                         ConditionCase{"NC", 0xD2, 0x00, kCarrySet},
                                         ConditionCase{"C", 0xDA, kCarrySet, 0x00}),
                         [](const testing::TestParamInfo<ConditionCase>& info) {
                             return std::string(info.param.name);
                         });

TEST(CpuJump, RelativeJumpForward) {
    Machine m({0x18, 0x05});  // JR +5
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().pc, kEntry + 2 + 5);
}

TEST(CpuJump, RelativeJumpBackwardIsMeasuredFromTheNextInstruction) {
    Machine m({0x18, 0xFE});  // JR -2: jumps to itself
    m.step();
    EXPECT_EQ(m.reg().pc, kEntry);
}

TEST(CpuJump, ConditionalRelativeJump) {
    Machine taken({0x28, 0x10});  // JR Z, +16
    taken.reg().f = kZeroSet;
    EXPECT_EQ(taken.step(), 12U);
    EXPECT_EQ(taken.reg().pc, kEntry + 2 + 16);

    Machine skipped({0x28, 0x10});
    skipped.reg().f = 0x00;
    EXPECT_EQ(skipped.step(), 8U);
    EXPECT_EQ(skipped.reg().pc, kEntry + 2);
}

TEST(CpuCall, PushesTheAddressOfTheNextInstruction) {
    Machine m({0xCD, 0x34, 0x12});  // CALL 0x1234
    m.reg().sp = 0xD000;
    EXPECT_EQ(m.step(), 24U);
    EXPECT_EQ(m.reg().pc, 0x1234);
    EXPECT_EQ(m.reg().sp, 0xCFFE);
    EXPECT_EQ(m.bus.read8(0xCFFF), 0x01);
    EXPECT_EQ(m.bus.read8(0xCFFE), 0x03);
}

TEST(CpuCall, ConditionalCallNotTakenLeavesTheStackAlone) {
    Machine m({0xC4, 0x34, 0x12});  // CALL NZ, 0x1234
    m.reg().f = kZeroSet;
    m.reg().sp = 0xD000;
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().pc, kEntry + 3);
    EXPECT_EQ(m.reg().sp, 0xD000);
}

TEST(CpuCall, ConditionalCallTaken) {
    Machine m({0xDC, 0x34, 0x12});  // CALL C, 0x1234
    m.reg().f = kCarrySet;
    m.reg().sp = 0xD000;
    EXPECT_EQ(m.step(), 24U);
    EXPECT_EQ(m.reg().pc, 0x1234);
}

TEST(CpuCall, CallThenRetComesBack) {
    // 0x0100: CALL 0x0105 ; 0x0103: NOP ; 0x0104: NOP ; 0x0105: RET
    Machine m({0xCD, 0x05, 0x01, 0x00, 0x00, 0xC9});
    m.reg().sp = 0xD000;
    m.step();
    EXPECT_EQ(m.reg().pc, 0x0105);
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().pc, 0x0103);
    EXPECT_EQ(m.reg().sp, 0xD000);
}

TEST(CpuRet, ConditionalReturnTakenAndNotTaken) {
    Machine taken({0xC8});  // RET Z
    taken.bus.write8(0xCFFE, 0x34);
    taken.bus.write8(0xCFFF, 0x12);
    taken.reg().sp = 0xCFFE;
    taken.reg().f = kZeroSet;
    EXPECT_EQ(taken.step(), 20U);
    EXPECT_EQ(taken.reg().pc, 0x1234);
    EXPECT_EQ(taken.reg().sp, 0xD000);

    Machine skipped({0xC8});
    skipped.reg().sp = 0xCFFE;
    skipped.reg().f = 0x00;
    EXPECT_EQ(skipped.step(), 8U);
    EXPECT_EQ(skipped.reg().pc, kEntry + 1);
    EXPECT_EQ(skipped.reg().sp, 0xCFFE);
}

TEST(CpuRet, RetiReturnsAndEnablesInterrupts) {
    Machine m({0xD9});  // RETI
    m.bus.write8(0xCFFE, 0x34);
    m.bus.write8(0xCFFF, 0x12);
    m.reg().sp = 0xCFFE;
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().pc, 0x1234);
    EXPECT_TRUE(m.cpu.interrupts_enabled());
}

TEST(CpuRst, EachRestartJumpsToItsFixedAddress) {
    for (unsigned n = 0; n < 8; ++n) {
        const auto opcode = static_cast<std::uint8_t>(0xC7U + (n << 3U));  // RST n*8
        Machine m({opcode});
        m.reg().sp = 0xD000;
        EXPECT_EQ(m.step(), 16U);
        EXPECT_EQ(m.reg().pc, n * 8U);
        EXPECT_EQ(m.bus.read8(0xCFFF), 0x01);
        EXPECT_EQ(m.bus.read8(0xCFFE), 0x01);
    }
}

TEST(CpuFlow, JumpsDoNotTouchFlags) {
    Machine m({0xC3, 0x03, 0x01, 0x18, 0x00, 0xCD, 0x08, 0x01, 0xC9});
    // JP 0x0103 ; JR +0 ; CALL 0x0108 ; RET
    m.reg().f = 0xF0;
    m.reg().sp = 0xD000;
    m.run(4);
    EXPECT_EQ(m.reg().f, 0xF0);
}

}  // namespace
