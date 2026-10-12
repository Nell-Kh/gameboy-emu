#include "core/joypad.h"

#include "core/interrupts.h"

namespace core {

namespace {

constexpr std::uint8_t kUnusedBits = 0xC0;  // bits 6-7 do not exist and read 1
constexpr std::uint8_t kSelectBits = 0x30;
constexpr std::uint8_t kSelectDpad = 0x10;
constexpr std::uint8_t kSelectButtons = 0x20;
constexpr std::uint8_t kLineBits = 0x0F;
constexpr std::uint8_t kButtonsShift = 4;
constexpr auto kJoypadBit = static_cast<std::uint8_t>(Interrupt::Joypad);

}  // namespace

std::uint8_t Joypad::read() const noexcept {
    return static_cast<std::uint8_t>(kUnusedBits | select_ | lines());
}

std::uint8_t Joypad::write(std::uint8_t value) noexcept {
    const std::uint8_t before = lines();
    select_ = value & kSelectBits;
    return falling_edge(before);
}

std::uint8_t Joypad::set_pressed(Button button, bool pressed) noexcept {
    const std::uint8_t before = lines();
    const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(button));
    if (pressed) {
        pressed_ |= bit;
    } else {
        pressed_ &= static_cast<std::uint8_t>(~bit);
    }
    return falling_edge(before);
}

std::uint8_t Joypad::lines() const noexcept {
    std::uint32_t low = 0;
    if ((select_ & kSelectDpad) == 0) {
        low |= pressed_ & kLineBits;
    }
    if ((select_ & kSelectButtons) == 0) {
        low |= static_cast<std::uint32_t>(pressed_ >> kButtonsShift) & kLineBits;
    }
    return static_cast<std::uint8_t>(~low & kLineBits);
}

std::uint8_t Joypad::falling_edge(std::uint8_t before) const noexcept {
    const auto fell = static_cast<std::uint8_t>(before & ~lines() & kLineBits);
    return fell != 0 ? kJoypadBit : std::uint8_t{0};
}

}  // namespace core
