// Checks every opcode against the opcode table (tools/opcodes.json).
//
// The CPU's timing comes from the memory accesses each instruction makes, and
// its flag behaviour from the hand-written helpers. The table is an independent
// source for both, so comparing the two catches a wrong duration, a wrong
// length or a flag that should not have been touched, on all 500 instructions.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/opcode_info.h"
#include "machine.h"

namespace {

using core::OpcodeInfo;
using test::kEntry;
using test::Machine;

constexpr std::uint8_t kPrefix = 0xCB;
constexpr std::uint8_t kHalt = 0x76;
constexpr std::uint8_t kStop = 0x10;

// The two bytes after the opcode. As an address they point into work RAM
// (0xC010); as a relative jump the first is +16.
constexpr std::uint8_t kOperandLo = 0x10;
constexpr std::uint8_t kOperandHi = 0xC0;

bool is_undefined(const OpcodeInfo& info) {
    return info.mnemonic == "ILLEGAL";
}

bool changes_pc(const OpcodeInfo& info) {
    constexpr std::array<std::string_view, 5> kFlowMnemonics = {"JP", "JR", "CALL", "RET", "RST"};
    return std::ranges::any_of(kFlowMnemonics, [&info](std::string_view prefix) {
        return info.mnemonic.starts_with(prefix);
    });
}

// For the conditional jumps, calls and returns, bits 3-4 of the opcode pick
// the condition: NZ, Z, NC, C.
bool condition_holds(std::uint8_t opcode, std::uint8_t flags) {
    const bool zero = (flags & 0x80U) != 0;
    const bool carry = (flags & 0x10U) != 0;
    switch ((opcode >> 3U) & 0x03U) {
        case 0:
            return !zero;
        case 1:
            return zero;
        case 2:
            return !carry;
        default:
            return carry;
    }
}

// Points every register pair at work RAM so that any instruction can run.
void prepare(Machine& m, std::uint8_t flags) {
    m.reg().a = 0x12;
    m.reg().f = flags;
    m.reg().set_bc(0xC200);
    m.reg().set_de(0xC300);
    m.reg().set_hl(0xC400);
    m.reg().sp = 0xDFF0;
}

void expect_flags_follow_table(const OpcodeInfo& info, std::uint8_t before, std::uint8_t after) {
    constexpr std::array<std::uint8_t, 4> kMasks = {0x80, 0x40, 0x20, 0x10};
    for (std::size_t i = 0; i < kMasks.size(); ++i) {
        const std::uint8_t mask = kMasks.at(i);
        const char rule = info.flags.at(i);
        const bool was_set = (before & mask) != 0;
        const bool is_set = (after & mask) != 0;
        if (rule == '-') {
            EXPECT_EQ(is_set, was_set) << "flag " << "ZNHC"[i] << " must not change";
        } else if (rule == '0') {
            EXPECT_FALSE(is_set) << "flag " << "ZNHC"[i] << " must be cleared";
        } else if (rule == '1') {
            EXPECT_TRUE(is_set) << "flag " << "ZNHC"[i] << " must be set";
        }
    }
    EXPECT_EQ(after & 0x0FU, 0U) << "the low half of F must stay zero";
}

std::string describe(std::string_view table, std::uint8_t opcode, const OpcodeInfo& info,
                     std::uint8_t flags) {
    return std::string(table) + " opcode " + std::to_string(opcode) + " (" +
           std::string(info.mnemonic) + ") with F=" + std::to_string(flags);
}

class OpcodeTable : public testing::TestWithParam<std::uint8_t> {};

TEST_P(OpcodeTable, UnprefixedOpcodesMatch) {
    const std::uint8_t flags = GetParam();
    for (int code = 0; code < 256; ++code) {
        const auto opcode = static_cast<std::uint8_t>(code);
        const OpcodeInfo& info = core::kOpcodeInfo.at(opcode);
        if (is_undefined(info) || opcode == kPrefix || opcode == kHalt || opcode == kStop) {
            continue;
        }
        SCOPED_TRACE(describe("plain", opcode, info, flags));

        Machine m({opcode, kOperandLo, kOperandHi});
        prepare(m, flags);
        const std::uint32_t ticks = m.step();

        const bool conditional = info.ticks != info.ticks_not_taken;
        const bool taken = !conditional || condition_holds(opcode, flags);
        EXPECT_EQ(ticks, taken ? info.ticks : info.ticks_not_taken);
        if (!changes_pc(info)) {
            EXPECT_EQ(m.reg().pc, kEntry + info.bytes);
        }
        expect_flags_follow_table(info, flags, m.reg().f);
        EXPECT_FALSE(m.cpu.locked());
    }
}

TEST_P(OpcodeTable, PrefixedOpcodesMatch) {
    const std::uint8_t flags = GetParam();
    for (int code = 0; code < 256; ++code) {
        const auto opcode = static_cast<std::uint8_t>(code);
        const OpcodeInfo& info = core::kCbOpcodeInfo.at(opcode);
        SCOPED_TRACE(describe("CB", opcode, info, flags));

        Machine m({kPrefix, opcode});
        prepare(m, flags);
        const std::uint32_t ticks = m.step();

        EXPECT_EQ(ticks, info.ticks);
        EXPECT_EQ(m.reg().pc, kEntry + info.bytes);
        expect_flags_follow_table(info, flags, m.reg().f);
    }
}

// Every combination of the four flags going in.
INSTANTIATE_TEST_SUITE_P(AllFlagStates, OpcodeTable,
                         testing::Values(0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90,
                                         0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0),
                         [](const testing::TestParamInfo<std::uint8_t>& info) {
                             return "F" + std::to_string(info.param);
                         });

TEST(OpcodeTableShape, ElevenOpcodesAreUndefined) {
    int undefined = 0;
    for (const OpcodeInfo& info : core::kOpcodeInfo) {
        undefined += is_undefined(info) ? 1 : 0;
    }
    EXPECT_EQ(undefined, 11);
}

TEST(OpcodeTableShape, TakenBranchesAreNeverShorterThanSkippedOnes) {
    for (const OpcodeInfo& info : core::kOpcodeInfo) {
        EXPECT_GE(info.ticks, info.ticks_not_taken) << info.mnemonic;
    }
}

}  // namespace
