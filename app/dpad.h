#pragma once

#include <array>
#include <cstddef>

#include "core/joypad.h"

namespace frontend {

// Turns the arrow keys held on a keyboard into a d-pad state a real Game Boy
// could have. A real d-pad cannot press left and right (or up and down) at
// once, and some games misbehave if they see it, so when both keys of an axis
// are held the one pressed last wins. Releasing it hands the axis back to the
// other key if that is still held.
class Dpad {
public:
    using Button = core::Joypad::Button;

    // Records that the key for `direction` went down or up.
    void set_held(Button direction, bool held) noexcept {
        held_.at(index(direction)) = held;
        if (held) {
            last_.at(axis(direction)) = direction;
        }
    }

    // Whether the Game Boy should see `direction` as pressed.
    [[nodiscard]] bool pressed(Button direction) const noexcept {
        if (!held_.at(index(direction))) {
            return false;
        }
        return !held_.at(index(opposite(direction))) || last_.at(axis(direction)) == direction;
    }

    static constexpr std::array<Button, 4> kDirections = {Button::Right, Button::Left, Button::Up,
                                                          Button::Down};

private:
    [[nodiscard]] static constexpr std::size_t index(Button direction) noexcept {
        return static_cast<std::size_t>(direction);
    }
    // 0: left/right, 1: up/down.
    [[nodiscard]] static constexpr std::size_t axis(Button direction) noexcept {
        return index(direction) / 2;
    }
    [[nodiscard]] static constexpr Button opposite(Button direction) noexcept {
        return static_cast<Button>(index(direction) ^ 1U);
    }

    std::array<bool, 4> held_{};
    std::array<Button, 2> last_ = {Button::Right, Button::Up};
};

}  // namespace frontend
