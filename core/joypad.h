#pragma once

#include <cstdint>

namespace core {

class StateReader;
class StateWriter;

// The joypad register P1 (0xFF00).
//
// The eight buttons are wired as two groups of four on the same four input
// lines (bits 0-3). The program selects which group it wants to read by
// writing 0 to bit 4 (the d-pad) or bit 5 (the buttons); with both selected the
// two groups are combined. A line reads 0 while a button on it is pressed in a
// selected group:
//
//   bit 3: Down / Start   bit 2: Up / Select   bit 1: Left / B   bit 0: Right / A
//
// The joypad interrupt is requested when any input line goes from 1 to 0,
// whether because a button was pressed or because a group with a button held
// was selected.
class Joypad {
public:
    // Save states (core/state_io.h): this part's fields, in the order of
    // the section table in docs/cartridge-notes.md, section 7.
    void save_state(StateWriter& out) const;
    void load_state(StateReader& in);

    static constexpr std::uint16_t kRegister = 0xFF00;

    // The d-pad comes first, in the order of its input lines, then the buttons.
    enum class Button : std::uint8_t { Right, Left, Up, Down, A, B, Select, Start };

    [[nodiscard]] std::uint8_t read() const noexcept;

    // Only the two select bits can be written. Returns the joypad interrupt
    // bit if a line fell, otherwise 0.
    [[nodiscard]] std::uint8_t write(std::uint8_t value) noexcept;

    // Presses or releases a button. Returns the joypad interrupt bit if a line
    // fell, otherwise 0.
    [[nodiscard]] std::uint8_t set_pressed(Button button, bool pressed) noexcept;

private:
    // Bits 0-3 as the program sees them: 0 where a selected button is pressed.
    [[nodiscard]] std::uint8_t lines() const noexcept;
    // The interrupt bit if a line that was high in `before` is now low.
    [[nodiscard]] std::uint8_t falling_edge(std::uint8_t before) const noexcept;

    // Bits 4-5 as written; 0 selects a group. After boot both are selected.
    std::uint8_t select_ = 0x00;
    // One bit per button, bit n for Button n, set while pressed.
    std::uint8_t pressed_ = 0x00;
};

}  // namespace core
