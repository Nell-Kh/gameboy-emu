#pragma once

#include <cstdint>

namespace core {

// The four CPU flags live in the upper half of the F register.
enum class Flag : std::uint8_t {
    Z = 0x80,  // zero: the result was 0
    N = 0x40,  // subtract: the last operation was a subtraction
    H = 0x20,  // half carry: carry out of bit 3
    C = 0x10,  // carry: carry out of bit 7
};

// The SM83 register file: eight 8-bit registers that pair up into four
// 16-bit ones (AF, BC, DE, HL), plus the stack pointer and program counter.
struct Registers {
    std::uint8_t a = 0;
    std::uint8_t f = 0;
    std::uint8_t b = 0;
    std::uint8_t c = 0;
    std::uint8_t d = 0;
    std::uint8_t e = 0;
    std::uint8_t h = 0;
    std::uint8_t l = 0;
    std::uint16_t sp = 0;
    std::uint16_t pc = 0;

    [[nodiscard]] constexpr std::uint16_t af() const noexcept {
        return pair(a, f);
    }
    [[nodiscard]] constexpr std::uint16_t bc() const noexcept {
        return pair(b, c);
    }
    [[nodiscard]] constexpr std::uint16_t de() const noexcept {
        return pair(d, e);
    }
    [[nodiscard]] constexpr std::uint16_t hl() const noexcept {
        return pair(h, l);
    }

    // The low four bits of F do not exist in hardware and always read as 0.
    constexpr void set_af(std::uint16_t value) noexcept {
        a = high(value);
        f = static_cast<std::uint8_t>(low(value) & 0xF0U);
    }
    constexpr void set_bc(std::uint16_t value) noexcept {
        b = high(value);
        c = low(value);
    }
    constexpr void set_de(std::uint16_t value) noexcept {
        d = high(value);
        e = low(value);
    }
    constexpr void set_hl(std::uint16_t value) noexcept {
        h = high(value);
        l = low(value);
    }

    [[nodiscard]] constexpr bool flag(Flag which) const noexcept {
        return (f & static_cast<std::uint8_t>(which)) != 0;
    }
    constexpr void set_flag(Flag which, bool on) noexcept {
        const auto mask = static_cast<std::uint8_t>(which);
        f = static_cast<std::uint8_t>(on ? (f | mask) : (f & ~mask));
    }

    // Register values right after the DMG boot ROM hands over to the cartridge.
    // We do not ship or run the boot ROM, so the CPU starts from this state.
    // F is 0xB0 for any cartridge with a non-zero header checksum, which is
    // every real one.
    [[nodiscard]] static constexpr Registers post_boot_dmg() noexcept {
        Registers r;
        r.a = 0x01;
        r.f = 0xB0;
        r.b = 0x00;
        r.c = 0x13;
        r.d = 0x00;
        r.e = 0xD8;
        r.h = 0x01;
        r.l = 0x4D;
        r.sp = 0xFFFE;
        r.pc = 0x0100;
        return r;
    }

private:
    [[nodiscard]] static constexpr std::uint16_t pair(std::uint8_t hi, std::uint8_t lo) noexcept {
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }
    [[nodiscard]] static constexpr std::uint8_t high(std::uint16_t value) noexcept {
        return static_cast<std::uint8_t>(value >> 8);
    }
    [[nodiscard]] static constexpr std::uint8_t low(std::uint16_t value) noexcept {
        return static_cast<std::uint8_t>(value & 0xFFU);
    }
};

}  // namespace core
