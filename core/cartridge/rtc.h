#pragma once

#include <array>
#include <cstdint>

namespace core {

// The MBC3 real-time clock (Pan Docs, MBC3; docs/cartridge-notes.md, sections
// 4 and 5). It counts emulated time, 4,194,304 clock ticks per second, never
// the wall clock (ADR-018).
//
// Five registers, selected on the MBC3 with 0x08-0x0C:
//
//   S   seconds, bits 0-5
//   M   minutes, bits 0-5
//   H   hours, bits 0-4
//   DL  day counter, bits 0-7
//   DH  bit 0 = day counter bit 8, bit 6 = halt, bit 7 = day carry
//
// Reads return a latched copy, refreshed by latch(). A write changes both the
// live register and the latched copy. Ticking follows rtc3test: S and M go
// from 59 to 0 with a carry and from 63 to 0 without one, H from 23 to 0 with
// a carry and from 31 to 0 without, the 9-bit day counter from 511 to 0 and
// sets the carry bit, which stays set until written to 0.
class Rtc {
public:
    enum class Register : std::uint8_t { Seconds, Minutes, Hours, DayLow, DayHigh };
    static constexpr int kRegisterCount = 5;
    using Registers = std::array<std::uint8_t, kRegisterCount>;

    static constexpr std::uint32_t kTicksPerSecond = 4'194'304;
    static constexpr std::uint8_t kHaltBit = 0x40;
    static constexpr std::uint8_t kCarryBit = 0x80;

    // Advances the sub-second counter; whole seconds tick the registers.
    // Nothing moves while the halt bit is set.
    void tick(std::uint32_t t_cycles) noexcept;

    // Copies the live registers into the readable copy.
    void latch() noexcept;

    [[nodiscard]] std::uint8_t read(Register reg) const noexcept;
    // Keeps only the bits that exist. Writing S restarts the second in
    // progress; the other registers leave it alone.
    void write(Register reg, std::uint8_t value) noexcept;

    // Both register sets, for the save file and tests, and the clock ticks
    // into the current second.
    [[nodiscard]] const Registers& live() const noexcept {
        return live_;
    }
    [[nodiscard]] const Registers& latched() const noexcept {
        return latched_;
    }
    [[nodiscard]] std::uint32_t sub_second() const noexcept {
        return sub_second_;
    }
    [[nodiscard]] bool halted() const noexcept;

    // Sets both register sets, as when a save file is loaded. Keeps only the
    // bits that exist; the second in progress starts again from zero.
    void restore(const Registers& live, const Registers& latched) noexcept;

    // Adds `seconds` to the live registers in one go, giving exactly what
    // that many ticks would (the catch-up at load, ADR-018): a halted clock
    // does not move, invalid values count on and wrap as when ticking, and
    // passing day 511 sets the carry bit. The second in progress is kept.
    void advance_seconds(std::uint64_t seconds) noexcept;

private:
    void step_second() noexcept;
    [[nodiscard]] bool time_is_valid() const noexcept;

    Registers live_{};
    Registers latched_{};
    std::uint32_t sub_second_ = 0;
};

}  // namespace core
