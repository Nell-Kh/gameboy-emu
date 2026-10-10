#include "core/ppu.h"

namespace core {

namespace {

constexpr std::uint8_t kLcdEnable = 0x80;
constexpr std::uint32_t kTicksPerFrame = Ppu::kTicksPerLine * Ppu::kLinesPerFrame;

}  // namespace

std::uint8_t Ppu::read(std::uint16_t address) const noexcept {
    if (address == kLcdc) {
        return lcdc_;
    }
    return static_cast<std::uint8_t>(frame_ticks_ / kTicksPerLine);
}

void Ppu::write(std::uint16_t address, std::uint8_t value) noexcept {
    // LY is read-only.
    if (address != kLcdc) {
        return;
    }
    lcdc_ = value;
    // Switching the LCD off resets it to the start of line 0; switching it on
    // starts counting from there.
    if (!lcd_enabled()) {
        frame_ticks_ = 0;
    }
}

bool Ppu::tick(std::uint32_t t_cycles) noexcept {
    if (!lcd_enabled()) {
        return false;
    }
    constexpr std::uint32_t kVBlankStart = kTicksPerLine * kVisibleLines;
    const std::uint32_t before = frame_ticks_;
    frame_ticks_ = (frame_ticks_ + t_cycles) % kTicksPerFrame;
    // The blank starts at the first tick of line 144. Steps are at most a few
    // ticks, so a wrap past the end of the frame cannot also cross it.
    return before < kVBlankStart && (frame_ticks_ >= kVBlankStart || frame_ticks_ < before);
}

bool Ppu::lcd_enabled() const noexcept {
    return (lcdc_ & kLcdEnable) != 0;
}

}  // namespace core
