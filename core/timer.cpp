#include "core/timer.h"

#include <array>

namespace core {

namespace {

constexpr std::uint8_t kTacEnable = 0x04;
constexpr std::uint8_t kTacRateMask = 0x03;
// The top five bits of TAC do not exist and read as 1.
constexpr std::uint8_t kTacUnusedBits = 0xF8;
constexpr std::uint8_t kTicksPerMachineCycle = 4;

// Which counter bit TIMA watches for each TAC rate setting.
// Rate 0 is the slowest (4096 Hz), rate 1 the fastest (262144 Hz).
constexpr std::array<std::uint16_t, 4> kWatchedBit = {
    1U << 9U,  // 00: every 1024 ticks
    1U << 3U,  // 01: every 16 ticks
    1U << 5U,  // 10: every 64 ticks
    1U << 7U,  // 11: every 256 ticks
};

}  // namespace

std::uint8_t Timer::read(std::uint16_t address) const noexcept {
    switch (address) {
        case kDiv:
            return static_cast<std::uint8_t>(counter_ >> 8U);
        case kTima:
            return tima_;
        case kTma:
            return tma_;
        default:
            return tac_ | kTacUnusedBits;
    }
}

void Timer::write(std::uint16_t address, std::uint8_t value) noexcept {
    switch (address) {
        case kDiv: {
            // Any write clears the whole counter. If the watched bit was 1,
            // TIMA sees it fall and counts.
            const bool before = signal();
            counter_ = 0;
            if (before) {
                increment_tima();
            }
            break;
        }
        case kTima:
            if (overflow_ == Overflow::Reloaded) {
                // The reload wins over a write in the same machine cycle.
                break;
            }
            // Writing during the delay cancels the pending reload and interrupt.
            overflow_ = Overflow::None;
            tima_ = value;
            break;
        case kTma:
            tma_ = value;
            if (overflow_ == Overflow::Reloaded) {
                // TIMA is still being loaded from TMA, so it gets the new value.
                tima_ = value;
            }
            break;
        default: {
            // Changing the rate or the enable bit can also make the watched
            // signal fall.
            const bool before = signal();
            tac_ = value & (kTacEnable | kTacRateMask);
            if (before && !signal()) {
                increment_tima();
            }
            break;
        }
    }
}

bool Timer::tick(std::uint32_t t_cycles) noexcept {
    bool interrupt = false;
    for (std::uint32_t i = 0; i < t_cycles; ++i) {
        interrupt = tick_once() || interrupt;
    }
    return interrupt;
}

bool Timer::signal() const noexcept {
    return (tac_ & kTacEnable) != 0 && (counter_ & kWatchedBit[tac_ & kTacRateMask]) != 0;
}

void Timer::increment_tima() noexcept {
    ++tima_;
    if (tima_ == 0) {
        overflow_ = Overflow::Delay;
        overflow_ticks_left_ = kTicksPerMachineCycle;
    }
}

bool Timer::tick_once() noexcept {
    bool interrupt = false;
    if (overflow_ != Overflow::None) {
        --overflow_ticks_left_;
        if (overflow_ticks_left_ == 0) {
            if (overflow_ == Overflow::Delay) {
                tima_ = tma_;
                interrupt = true;
                overflow_ = Overflow::Reloaded;
                overflow_ticks_left_ = kTicksPerMachineCycle;
            } else {
                overflow_ = Overflow::None;
            }
        }
    }

    const bool before = signal();
    ++counter_;
    if (before && !signal()) {
        increment_tima();
    }
    return interrupt;
}

}  // namespace core
