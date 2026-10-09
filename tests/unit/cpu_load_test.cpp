// Loads, the stack and the high-page instructions.

#include <gtest/gtest.h>

#include <cstdint>

#include "machine.h"

namespace {

using test::kEntry;
using test::Machine;

TEST(CpuLoad, RegisterToRegister) {
    Machine m({0x41});  // LD B, C
    m.reg().c = 0x99;
    EXPECT_EQ(m.step(), 4U);
    EXPECT_EQ(m.reg().b, 0x99);
}

TEST(CpuLoad, EveryRegisterCanBeLoadedWithAnImmediate) {
    // LD B/C/D/E/H/L/A, n8
    Machine m({0x06, 1, 0x0E, 2, 0x16, 3, 0x1E, 4, 0x26, 5, 0x2E, 6, 0x3E, 7});
    EXPECT_EQ(m.run(7), 7U * 8U);
    EXPECT_EQ(m.reg().bc(), 0x0102);
    EXPECT_EQ(m.reg().de(), 0x0304);
    EXPECT_EQ(m.reg().hl(), 0x0506);
    EXPECT_EQ(m.reg().a, 0x07);
}

TEST(CpuLoad, SixteenBitImmediateIsLowByteFirst) {
    Machine m({0x01, 0x34, 0x12, 0x31, 0xFE, 0xDF});  // LD BC, 0x1234 ; LD SP, 0xDFFE
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().bc(), 0x1234);
    m.step();
    EXPECT_EQ(m.reg().sp, 0xDFFE);
}

TEST(CpuLoad, ImmediateToMemoryAtHl) {
    Machine m({0x36, 0x7B});  // LD [HL], 0x7B
    m.reg().set_hl(0xC000);
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x7B);
}

TEST(CpuLoad, AThroughBcAndDe) {
    Machine m({0x02, 0x1A});  // LD [BC], A ; LD A, [DE]
    m.bus.write8(0xC100, 0x66);
    m.reg().a = 0x55;
    m.reg().set_bc(0xC000);
    m.reg().set_de(0xC100);
    m.step();
    EXPECT_EQ(m.bus.read8(0xC000), 0x55);
    m.step();
    EXPECT_EQ(m.reg().a, 0x66);
}

TEST(CpuLoad, StoreWithPostIncrementAndPostDecrement) {
    Machine m({0x22, 0x32});  // LD [HL+], A ; LD [HL-], A
    m.reg().a = 0x11;
    m.reg().set_hl(0xC000);
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x11);
    EXPECT_EQ(m.reg().hl(), 0xC001);
    m.step();
    EXPECT_EQ(m.bus.read8(0xC001), 0x11);
    EXPECT_EQ(m.reg().hl(), 0xC000);
}

TEST(CpuLoad, LoadWithPostIncrementAndPostDecrement) {
    Machine m({0x2A, 0x3A});  // LD A, [HL+] ; LD A, [HL-]
    m.bus.write8(0xC000, 0xAA);
    m.bus.write8(0xC001, 0xBB);
    m.reg().set_hl(0xC000);
    m.step();
    EXPECT_EQ(m.reg().a, 0xAA);
    EXPECT_EQ(m.reg().hl(), 0xC001);
    m.step();
    EXPECT_EQ(m.reg().a, 0xBB);
    EXPECT_EQ(m.reg().hl(), 0xC000);
}

TEST(CpuLoad, AToAndFromAnAbsoluteAddress) {
    Machine m({0xEA, 0x00, 0xC0, 0x3E, 0x00, 0xFA, 0x00, 0xC0});
    // LD [0xC000], A ; LD A, 0 ; LD A, [0xC000]
    m.reg().a = 0x42;
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.bus.read8(0xC000), 0x42);
    m.step();
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().a, 0x42);
}

TEST(CpuLoad, HighPageWithImmediateOffset) {
    Machine m({0xE0, 0x80, 0x3E, 0x00, 0xF0, 0x80});
    // LDH [0xFF80], A ; LD A, 0 ; LDH A, [0xFF80]
    m.reg().a = 0x42;
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.bus.read8(0xFF80), 0x42);
    m.step();
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().a, 0x42);
}

TEST(CpuLoad, HighPageWithOffsetInC) {
    Machine m({0xE2, 0x3E, 0x00, 0xF2});  // LDH [C], A ; LD A, 0 ; LDH A, [C]
    m.reg().a = 0x42;
    m.reg().c = 0x81;
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.bus.read8(0xFF81), 0x42);
    m.step();
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.reg().a, 0x42);
}

TEST(CpuLoad, SpToMemoryIsLowByteFirst) {
    Machine m({0x08, 0x00, 0xC0});  // LD [0xC000], SP
    m.reg().sp = 0xABCD;
    EXPECT_EQ(m.step(), 20U);
    EXPECT_EQ(m.bus.read8(0xC000), 0xCD);
    EXPECT_EQ(m.bus.read8(0xC001), 0xAB);
}

TEST(CpuLoad, SpFromHl) {
    Machine m({0xF9});  // LD SP, HL
    m.reg().set_hl(0xD123);
    EXPECT_EQ(m.step(), 8U);
    EXPECT_EQ(m.reg().sp, 0xD123);
}

TEST(CpuLoad, LoadsDoNotTouchFlags) {
    Machine m({0x06, 0x00, 0x78, 0x21, 0x00, 0x00});  // LD B, 0 ; LD A, B ; LD HL, 0
    m.reg().f = 0xF0;
    m.run(3);
    EXPECT_EQ(m.reg().f, 0xF0);
}

TEST(CpuStack, PushStoresHighByteAtTheHigherAddress) {
    Machine m({0xC5});  // PUSH BC
    m.reg().set_bc(0x1234);
    m.reg().sp = 0xD000;
    EXPECT_EQ(m.step(), 16U);
    EXPECT_EQ(m.reg().sp, 0xCFFE);
    EXPECT_EQ(m.bus.read8(0xCFFF), 0x12);
    EXPECT_EQ(m.bus.read8(0xCFFE), 0x34);
}

TEST(CpuStack, PopRestoresWhatPushSaved) {
    Machine m({0xD5, 0xE1});  // PUSH DE ; POP HL
    m.reg().set_de(0xBEEF);
    m.reg().sp = 0xD000;
    m.step();
    EXPECT_EQ(m.step(), 12U);
    EXPECT_EQ(m.reg().hl(), 0xBEEF);
    EXPECT_EQ(m.reg().sp, 0xD000);
}

TEST(CpuStack, PopAfDropsTheLowHalfOfF) {
    Machine m({0xC5, 0xF1});  // PUSH BC ; POP AF
    m.reg().set_bc(0x12FF);
    m.reg().sp = 0xD000;
    m.run(2);
    EXPECT_EQ(m.reg().af(), 0x12F0);
}

TEST(CpuStack, PushAfThenPopBcCopiesTheFlags) {
    Machine m({0xF5, 0xC1});  // PUSH AF ; POP BC
    m.reg().a = 0x9A;
    m.reg().f = 0x50;
    m.reg().sp = 0xD000;
    m.run(2);
    EXPECT_EQ(m.reg().bc(), 0x9A50);
}

TEST(CpuStack, StackPointerWrapsAround) {
    Machine m({0xC5});  // PUSH BC
    m.reg().set_bc(0x1234);
    m.reg().sp = 0x0001;
    m.step();
    EXPECT_EQ(m.reg().sp, 0xFFFF);
    EXPECT_EQ(m.reg().pc, kEntry + 1);
}

}  // namespace
