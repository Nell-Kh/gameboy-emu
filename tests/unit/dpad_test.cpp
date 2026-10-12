#include "app/dpad.h"

#include <gtest/gtest.h>

namespace {

using frontend::Dpad;
using Button = Dpad::Button;

TEST(Dpad, NothingIsPressedAtFirst) {
    const Dpad dpad;
    for (const Button direction : Dpad::kDirections) {
        EXPECT_FALSE(dpad.pressed(direction));
    }
}

TEST(Dpad, AHeldKeyIsPressedUntilReleased) {
    Dpad dpad;
    dpad.set_held(Button::Up, true);
    EXPECT_TRUE(dpad.pressed(Button::Up));
    dpad.set_held(Button::Up, false);
    EXPECT_FALSE(dpad.pressed(Button::Up));
}

TEST(Dpad, DifferentAxesCombine) {
    Dpad dpad;
    dpad.set_held(Button::Right, true);
    dpad.set_held(Button::Down, true);
    EXPECT_TRUE(dpad.pressed(Button::Right));
    EXPECT_TRUE(dpad.pressed(Button::Down));
}

TEST(Dpad, OfTwoOppositeKeysTheLastPressedWins) {
    Dpad dpad;
    dpad.set_held(Button::Left, true);
    dpad.set_held(Button::Right, true);
    EXPECT_TRUE(dpad.pressed(Button::Right));
    EXPECT_FALSE(dpad.pressed(Button::Left));
    dpad.set_held(Button::Left, true);  // pressed again: now it is the last
    EXPECT_TRUE(dpad.pressed(Button::Left));
    EXPECT_FALSE(dpad.pressed(Button::Right));
}

TEST(Dpad, ReleasingTheWinnerHandsTheAxisBack) {
    Dpad dpad;
    dpad.set_held(Button::Up, true);
    dpad.set_held(Button::Down, true);
    dpad.set_held(Button::Down, false);
    EXPECT_TRUE(dpad.pressed(Button::Up));
    EXPECT_FALSE(dpad.pressed(Button::Down));
}

}  // namespace
