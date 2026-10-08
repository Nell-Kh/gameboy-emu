#include "core/registers.h"

#include <gtest/gtest.h>

namespace {

using core::Flag;
using core::Registers;

TEST(Registers, StartZeroed) {
    const Registers r;
    EXPECT_EQ(r.af(), 0x0000);
    EXPECT_EQ(r.bc(), 0x0000);
    EXPECT_EQ(r.de(), 0x0000);
    EXPECT_EQ(r.hl(), 0x0000);
    EXPECT_EQ(r.sp, 0x0000);
    EXPECT_EQ(r.pc, 0x0000);
}

TEST(Registers, PairsReadHighByteFirst) {
    Registers r;
    r.b = 0x12;
    r.c = 0x34;
    r.d = 0x56;
    r.e = 0x78;
    r.h = 0x9A;
    r.l = 0xBC;
    EXPECT_EQ(r.bc(), 0x1234);
    EXPECT_EQ(r.de(), 0x5678);
    EXPECT_EQ(r.hl(), 0x9ABC);
}

TEST(Registers, SettingAPairSplitsIntoBothHalves) {
    Registers r;
    r.set_bc(0x1234);
    r.set_de(0x5678);
    r.set_hl(0x9ABC);
    EXPECT_EQ(r.b, 0x12);
    EXPECT_EQ(r.c, 0x34);
    EXPECT_EQ(r.d, 0x56);
    EXPECT_EQ(r.e, 0x78);
    EXPECT_EQ(r.h, 0x9A);
    EXPECT_EQ(r.l, 0xBC);
}

TEST(Registers, LowNibbleOfFIsAlwaysZero) {
    Registers r;
    r.set_af(0x12FF);
    EXPECT_EQ(r.a, 0x12);
    EXPECT_EQ(r.f, 0xF0);
    EXPECT_EQ(r.af(), 0x12F0);
}

TEST(Registers, FlagsMapToTheirBits) {
    Registers r;
    r.set_flag(Flag::Z, true);
    EXPECT_EQ(r.f, 0x80);
    r.set_flag(Flag::N, true);
    EXPECT_EQ(r.f, 0xC0);
    r.set_flag(Flag::H, true);
    EXPECT_EQ(r.f, 0xE0);
    r.set_flag(Flag::C, true);
    EXPECT_EQ(r.f, 0xF0);
}

TEST(Registers, ClearingOneFlagLeavesTheOthers) {
    Registers r;
    r.f = 0xF0;
    r.set_flag(Flag::H, false);
    EXPECT_TRUE(r.flag(Flag::Z));
    EXPECT_TRUE(r.flag(Flag::N));
    EXPECT_FALSE(r.flag(Flag::H));
    EXPECT_TRUE(r.flag(Flag::C));
    EXPECT_EQ(r.f, 0xD0);
}

TEST(Registers, SettingAFlagTwiceChangesNothing) {
    Registers r;
    r.set_flag(Flag::C, true);
    r.set_flag(Flag::C, true);
    EXPECT_EQ(r.f, 0x10);
    r.set_flag(Flag::C, false);
    r.set_flag(Flag::C, false);
    EXPECT_EQ(r.f, 0x00);
}

TEST(Registers, PostBootStateMatchesDmg) {
    const Registers r = Registers::post_boot_dmg();
    EXPECT_EQ(r.af(), 0x01B0);
    EXPECT_EQ(r.bc(), 0x0013);
    EXPECT_EQ(r.de(), 0x00D8);
    EXPECT_EQ(r.hl(), 0x014D);
    EXPECT_EQ(r.sp, 0xFFFE);
    EXPECT_EQ(r.pc, 0x0100);
    EXPECT_TRUE(r.flag(Flag::Z));
    EXPECT_FALSE(r.flag(Flag::N));
    EXPECT_TRUE(r.flag(Flag::H));
    EXPECT_TRUE(r.flag(Flag::C));
}

// The register file is usable at compile time.
static_assert(Registers::post_boot_dmg().hl() == 0x014D);

}  // namespace
