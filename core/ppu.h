#pragma once

#include <cstdint>

namespace core {

// The picture processing unit. So far only its line clock exists: LCDC turns
// the LCD on and off, and LY reports which scanline is being drawn. Rendering,
// STAT, the PPU interrupts and video-memory access rules arrive in M3.
//
// LY is here early because test ROMs wait on it: with LY stuck at 0 they never
// get past switching the screen off (ADR-010).
class Ppu {
public:
    static constexpr std::uint16_t kLcdc = 0xFF40;
    static constexpr std::uint16_t kLy = 0xFF44;

    // One scanline takes 456 clock ticks; 144 visible lines plus 10 lines of
    // vertical blank make one frame.
    static constexpr std::uint32_t kTicksPerLine = 456;
    static constexpr std::uint32_t kLinesPerFrame = 154;

    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    void write(std::uint16_t address, std::uint8_t value) noexcept;
    void tick(std::uint32_t t_cycles) noexcept;

private:
    [[nodiscard]] bool lcd_enabled() const noexcept;

    // The boot ROM leaves the LCD on with the background enabled.
    std::uint8_t lcdc_ = 0x91;
    // Ticks into the current frame. Stays 0 while the LCD is off.
    std::uint32_t frame_ticks_ = 0;
};

}  // namespace core
