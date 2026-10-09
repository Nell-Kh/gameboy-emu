#pragma once

#include <cstdint>

namespace core {

// The hardware timer: four registers at 0xFF04-0xFF07.
//
//   DIV   the upper 8 bits of a 16-bit counter that goes up by one on every
//         clock tick. Writing any value resets the whole counter to 0.
//   TIMA  the programmable counter. When it overflows past 0xFF it is reloaded
//         from TMA and the timer interrupt is requested.
//   TMA   the reload value.
//   TAC   bit 2 enables TIMA; bits 0-1 choose how fast it counts.
//
// TIMA is not driven by its own clock. It watches one bit of the DIV counter
// (which bit depends on TAC) and goes up each time that bit falls from 1 to 0.
// Modelling it that way gives the hardware's odd side effects for free: for
// example, resetting DIV can make TIMA count one extra step.
class Timer {
public:
    static constexpr std::uint16_t kDiv = 0xFF04;
    static constexpr std::uint16_t kTima = 0xFF05;
    static constexpr std::uint16_t kTma = 0xFF06;
    static constexpr std::uint16_t kTac = 0xFF07;

    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    void write(std::uint16_t address, std::uint8_t value) noexcept;

    // Advances the timer. Returns true if the timer interrupt was requested
    // during these ticks.
    [[nodiscard]] bool tick(std::uint32_t t_cycles) noexcept;

private:
    enum class Overflow : std::uint8_t {
        None,
        // TIMA has wrapped to 0x00 and stays there for one machine cycle.
        Delay,
        // TIMA has just been reloaded from TMA; writes to TIMA are ignored
        // for this machine cycle.
        Reloaded,
    };

    // The bit of the counter that TIMA watches, masked by the enable bit.
    [[nodiscard]] bool signal() const noexcept;
    void increment_tima() noexcept;
    // Runs one tick. Returns true if the interrupt was requested.
    [[nodiscard]] bool tick_once() noexcept;

    // The boot ROM takes 0xABCC ticks, so DIV reads 0xAB when a game starts.
    std::uint16_t counter_ = 0xABCC;
    std::uint8_t tima_ = 0x00;
    std::uint8_t tma_ = 0x00;
    std::uint8_t tac_ = 0x00;
    Overflow overflow_ = Overflow::None;
    std::uint8_t overflow_ticks_left_ = 0;
};

}  // namespace core
