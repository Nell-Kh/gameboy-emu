#include "core/joypad.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "core/interrupts.h"

namespace {

using core::Joypad;
using Button = Joypad::Button;

constexpr auto kJoypadBit = static_cast<std::uint8_t>(core::Interrupt::Joypad);
constexpr std::uint8_t kSelectDpad = 0x20;     // bit 4 low
constexpr std::uint8_t kSelectButtons = 0x10;  // bit 5 low
constexpr std::uint8_t kSelectBoth = 0x00;
constexpr std::uint8_t kSelectNone = 0x30;

// Test helpers that ignore the interrupt result.
void select(Joypad& joypad, std::uint8_t value) {
    static_cast<void>(joypad.write(value));
}

void press(Joypad& joypad, Button button) {
    static_cast<void>(joypad.set_pressed(button, true));
}

TEST(Joypad, StartsWithBothGroupsSelectedAndNothingPressed) {
    const Joypad joypad;
    EXPECT_EQ(joypad.read(), 0xCF);
}

TEST(Joypad, OnlyTheSelectBitsCanBeWritten) {
    Joypad joypad;
    select(joypad, 0xFF);
    EXPECT_EQ(joypad.read(), 0xFF);
    select(joypad, 0x0F);
    EXPECT_EQ(joypad.read(), 0xCF);
}

TEST(Joypad, DpadLinesAreRightLeftUpDown) {
    Joypad joypad;
    select(joypad, kSelectDpad);
    press(joypad, Button::Right);
    EXPECT_EQ(joypad.read() & 0x0F, 0x0E);
    press(joypad, Button::Down);
    EXPECT_EQ(joypad.read() & 0x0F, 0x06);
    static_cast<void>(joypad.set_pressed(Button::Right, false));
    press(joypad, Button::Left);
    press(joypad, Button::Up);
    EXPECT_EQ(joypad.read() & 0x0F, 0x01);
}

TEST(Joypad, ButtonLinesAreABSelectStart) {
    Joypad joypad;
    select(joypad, kSelectButtons);
    press(joypad, Button::A);
    EXPECT_EQ(joypad.read() & 0x0F, 0x0E);
    press(joypad, Button::B);
    EXPECT_EQ(joypad.read() & 0x0F, 0x0C);
    press(joypad, Button::Select);
    EXPECT_EQ(joypad.read() & 0x0F, 0x08);
    press(joypad, Button::Start);
    EXPECT_EQ(joypad.read() & 0x0F, 0x00);
}

TEST(Joypad, AnUnselectedGroupDoesNotShow) {
    Joypad joypad;
    press(joypad, Button::A);
    select(joypad, kSelectDpad);
    EXPECT_EQ(joypad.read(), 0xEF);
    select(joypad, kSelectNone);
    EXPECT_EQ(joypad.read(), 0xFF);
}

TEST(Joypad, BothGroupsSelectedCombineOnTheSameLines) {
    Joypad joypad;
    select(joypad, kSelectBoth);
    press(joypad, Button::Right);  // line 0
    press(joypad, Button::B);      // line 1
    EXPECT_EQ(joypad.read(), 0xCC);
}

TEST(Joypad, PressingASelectedButtonRequestsTheInterrupt) {
    Joypad joypad;
    select(joypad, kSelectDpad);
    EXPECT_EQ(joypad.set_pressed(Button::Up, true), kJoypadBit);
}

TEST(Joypad, OnlyAFallingLineRequestsTheInterrupt) {
    Joypad joypad;
    select(joypad, kSelectDpad);
    EXPECT_EQ(joypad.set_pressed(Button::A, true), 0);  // not selected
    EXPECT_EQ(joypad.set_pressed(Button::Up, true), kJoypadBit);
    EXPECT_EQ(joypad.set_pressed(Button::Up, true), 0);   // already pressed
    EXPECT_EQ(joypad.set_pressed(Button::Up, false), 0);  // a rising line
    EXPECT_EQ(joypad.set_pressed(Button::Right, true), kJoypadBit);
    EXPECT_EQ(joypad.set_pressed(Button::Left, true), kJoypadBit);
}

TEST(Joypad, SelectingAGroupWithAButtonHeldRequestsTheInterrupt) {
    Joypad joypad;
    select(joypad, kSelectNone);
    EXPECT_EQ(joypad.set_pressed(Button::Start, true), 0);
    EXPECT_EQ(joypad.write(kSelectDpad), 0);
    EXPECT_EQ(joypad.write(kSelectButtons), kJoypadBit);
    EXPECT_EQ(joypad.write(kSelectBoth), 0);  // line 3 is already low
}

}  // namespace
