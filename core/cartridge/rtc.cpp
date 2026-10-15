#include "core/cartridge/rtc.h"

#include <cstddef>

namespace core {

namespace {

constexpr Rtc::Registers kValidBits = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};
constexpr std::uint8_t kDayBit8 = 0x01;

constexpr std::uint8_t kLastSecond = 59;
constexpr std::uint8_t kLargestSecond = 63;
constexpr std::uint8_t kLastHour = 23;
constexpr std::uint8_t kLargestHour = 31;
constexpr std::uint64_t kSecondsPerMinute = 60;
constexpr std::uint64_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr std::uint64_t kSecondsPerDay = 24 * kSecondsPerHour;
constexpr std::uint64_t kDays = 512;  // the 9-bit day counter

constexpr std::size_t index(Rtc::Register reg) noexcept {
    return static_cast<std::size_t>(reg);
}

// One step of S, M or H: `last` (59 or 23) rolls over to 0 and carries into
// the next register; `largest` (the largest value that fits, 63 or 31) rolls
// over to 0 without a carry; anything else counts up, so the invalid values
// between them count on to `largest`.
bool step(std::uint8_t& value, std::uint8_t last, std::uint8_t largest) noexcept {
    if (value == last) {
        value = 0;
        return true;
    }
    value = value == largest ? 0 : static_cast<std::uint8_t>(value + 1);
    return false;
}

}  // namespace

bool Rtc::halted() const noexcept {
    return (live_[index(Register::DayHigh)] & kHaltBit) != 0;
}

void Rtc::tick(std::uint32_t t_cycles) noexcept {
    if (halted()) {
        return;
    }
    sub_second_ += t_cycles;
    while (sub_second_ >= kTicksPerSecond) {
        sub_second_ -= kTicksPerSecond;
        step_second();
    }
}

void Rtc::step_second() noexcept {
    if (!step(live_[index(Register::Seconds)], kLastSecond, kLargestSecond) ||
        !step(live_[index(Register::Minutes)], kLastSecond, kLargestSecond) ||
        !step(live_[index(Register::Hours)], kLastHour, kLargestHour)) {
        return;
    }
    std::uint8_t& low = live_[index(Register::DayLow)];
    std::uint8_t& high = live_[index(Register::DayHigh)];
    ++low;
    if (low != 0) {
        return;
    }
    if ((high & kDayBit8) == 0) {
        high |= kDayBit8;
    } else {
        high = static_cast<std::uint8_t>((high & ~kDayBit8) | kCarryBit);
    }
}

void Rtc::latch() noexcept {
    latched_ = live_;
}

std::uint8_t Rtc::read(Register reg) const noexcept {
    return latched_[index(reg)];
}

void Rtc::write(Register reg, std::uint8_t value) noexcept {
    const std::size_t i = index(reg);
    live_[i] = value & kValidBits[i];
    latched_[i] = live_[i];
    if (reg == Register::Seconds) {
        sub_second_ = 0;
    }
}

void Rtc::restore(const Registers& live, const Registers& latched) noexcept {
    for (std::size_t i = 0; i < live_.size(); ++i) {
        live_[i] = live[i] & kValidBits[i];
        latched_[i] = latched[i] & kValidBits[i];
    }
    sub_second_ = 0;
}

bool Rtc::time_is_valid() const noexcept {
    return live_[index(Register::Seconds)] <= kLastSecond &&
           live_[index(Register::Minutes)] <= kLastSecond &&
           live_[index(Register::Hours)] <= kLastHour;
}

void Rtc::advance_seconds(std::uint64_t seconds) noexcept {
    if (halted()) {
        return;
    }
    // Invalid values (seconds or minutes 60-63, hours 24-31) do not follow
    // the arithmetic below, so tick one second at a time until the time is a
    // real time of day. That takes at most eight hours' worth of ticks.
    while (seconds > 0 && !time_is_valid()) {
        step_second();
        --seconds;
    }
    if (seconds == 0) {
        return;
    }
    std::uint8_t& s = live_[index(Register::Seconds)];
    std::uint8_t& m = live_[index(Register::Minutes)];
    std::uint8_t& h = live_[index(Register::Hours)];
    std::uint8_t& low = live_[index(Register::DayLow)];
    std::uint8_t& high = live_[index(Register::DayHigh)];

    const std::uint64_t time = (h * kSecondsPerHour) + (m * kSecondsPerMinute) + s + seconds;
    const std::uint64_t day =
        (static_cast<std::uint64_t>(high & kDayBit8) << 8U) + low + (time / kSecondsPerDay);
    const std::uint64_t time_of_day = time % kSecondsPerDay;
    h = static_cast<std::uint8_t>(time_of_day / kSecondsPerHour);
    m = static_cast<std::uint8_t>((time_of_day % kSecondsPerHour) / kSecondsPerMinute);
    s = static_cast<std::uint8_t>(time_of_day % kSecondsPerMinute);
    low = static_cast<std::uint8_t>(day % kDays);
    high = static_cast<std::uint8_t>((high & ~kDayBit8) | (((day % kDays) >> 8U) & kDayBit8));
    if (day >= kDays) {
        high |= kCarryBit;
    }
}

}  // namespace core
