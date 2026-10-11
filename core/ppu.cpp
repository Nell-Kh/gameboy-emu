#include "core/ppu.h"

#include <cstddef>

#include "core/interrupts.h"

namespace core {

namespace {

constexpr std::uint8_t kLcdEnable = 0x80;
constexpr std::uint8_t kWindowEnable = 0x20;
constexpr std::uint8_t kTallObjects = 0x04;
constexpr std::uint8_t kBgWindowEnable = 0x01;

constexpr std::uint8_t kStatUnused = 0x80;
constexpr std::uint8_t kStatSelectMask = 0x78;
constexpr std::uint8_t kSelectHBlank = 0x08;
constexpr std::uint8_t kSelectVBlank = 0x10;
constexpr std::uint8_t kSelectOamScan = 0x20;
constexpr std::uint8_t kSelectLyc = 0x40;
constexpr std::uint8_t kLycEqualBit = 0x04;

constexpr auto kVBlankBit = static_cast<std::uint8_t>(Interrupt::VBlank);
constexpr auto kStatBit = static_cast<std::uint8_t>(Interrupt::LcdStat);

constexpr std::uint8_t kOpenBus = 0xFF;
constexpr std::uint8_t kMaxObjectsPerLine = 10;
constexpr std::uint8_t kObjectBytes = 4;
constexpr std::uint8_t kObjectYOffset = 16;
constexpr std::uint8_t kMaxWindowX = 166;

}  // namespace

bool Ppu::is_register(std::uint16_t address) noexcept {
    return (address >= kLcdc && address <= kLyc) || (address >= kBgp && address <= kWx);
}

std::uint8_t Ppu::read(std::uint16_t address) const noexcept {
    switch (address) {
        case kLcdc:
            return lcdc_;
        case kStat: {
            const auto mode = lcd_enabled() ? static_cast<std::uint8_t>(mode_) : std::uint8_t{0};
            return static_cast<std::uint8_t>(kStatUnused | stat_select_ |
                                             (lyc_equal_ ? kLycEqualBit : 0) | mode);
        }
        case kScy:
            return scy_;
        case kScx:
            return scx_;
        case kLy:
            return ly();
        case kLyc:
            return lyc_;
        case kBgp:
            return bgp_;
        case kObp0:
            return obp0_;
        case kObp1:
            return obp1_;
        case kWy:
            return wy_;
        default:
            return wx_;
    }
}

std::uint8_t Ppu::write(std::uint16_t address, std::uint8_t value) noexcept {
    switch (address) {
        case kLcdc: {
            const bool was_enabled = lcd_enabled();
            lcdc_ = value;
            if (was_enabled && !lcd_enabled()) {
                // Off: back to the start of line 0. LYC = LY keeps its value.
                line_ = 0;
                dot_ = 0;
                mode_ = Mode::HBlank;
                return 0;
            }
            if (!was_enabled && lcd_enabled()) {
                line_ = 0;
                dot_ = 0;
                first_line_ = true;
                mode_ = Mode::HBlank;
                window_y_reached_ = false;
                return update_stat();
            }
            return 0;
        }
        case kStat:
            stat_select_ = value & kStatSelectMask;
            return lcd_enabled() ? update_stat() : std::uint8_t{0};
        case kScy:
            scy_ = value;
            return 0;
        case kScx:
            scx_ = value;
            return 0;
        case kLy:
            return 0;  // read-only
        case kLyc:
            lyc_ = value;
            // While the LCD is off the comparison does not run.
            return lcd_enabled() ? update_stat() : std::uint8_t{0};
        case kBgp:
            bgp_ = value;
            return 0;
        case kObp0:
            obp0_ = value;
            return 0;
        case kObp1:
            obp1_ = value;
            return 0;
        case kWy:
            wy_ = value;
            return 0;
        default:
            wx_ = value;
            return 0;
    }
}

std::uint8_t Ppu::tick(std::uint32_t t_cycles) noexcept {
    if (!lcd_enabled()) {
        return 0;
    }
    std::uint8_t requested = 0;
    for (std::uint32_t i = 0; i < t_cycles; ++i) {
        const Mode before = mode_;
        step_dot();
        if (before != Mode::VBlank && mode_ == Mode::VBlank) {
            requested |= kVBlankBit;
        }
        requested |= update_stat();
    }
    return requested;
}

void Ppu::step_dot() noexcept {
    ++dot_;
    if (dot_ == kTicksPerLine) {
        dot_ = 0;
        line_ = (line_ + 1) % kLinesPerFrame;
        first_line_ = false;
        start_line();
        return;
    }
    if (line_ >= kVisibleLines) {
        return;
    }
    if (dot_ == kOamScanDots) {
        start_drawing();
    } else if (mode_ == Mode::Drawing && dot_ == kOamScanDots + drawing_dots_) {
        mode_ = Mode::HBlank;
    }
}

void Ppu::start_line() noexcept {
    if (line_ == kVisibleLines) {
        mode_ = Mode::VBlank;
        window_y_reached_ = false;
        return;
    }
    if (line_ > kVisibleLines) {
        return;
    }
    if (line_ == wy_) {
        window_y_reached_ = true;
    }
    mode_ = Mode::OamScan;

    // OAM scan: the first ten objects in OAM order whose rows cover this line.
    const std::uint32_t height = (lcdc_ & kTallObjects) != 0 ? 16 : 8;
    line_object_count_ = 0;
    for (std::uint8_t index = 0; index < kOamSize / kObjectBytes; ++index) {
        const std::uint32_t top = oam_.at(std::size_t{index} * kObjectBytes);
        const std::uint32_t y = line_ + kObjectYOffset;
        if (y >= top && y < top + height) {
            line_objects_.at(line_object_count_++) = index;
            if (line_object_count_ == kMaxObjectsPerLine) {
                break;
            }
        }
    }
}

void Ppu::start_drawing() noexcept {
    mode_ = Mode::Drawing;
    std::uint32_t dots = kMinDrawingDots + (scx_ & 7U);
    const bool window_on = (lcdc_ & kWindowEnable) != 0 && (lcdc_ & kBgWindowEnable) != 0;
    if (window_on && window_y_reached_ && wx_ <= kMaxWindowX) {
        dots += 6;
    }
    dots += object_penalty();
    drawing_dots_ = dots;
}

// Pan Docs, Rendering, "Mode 3 length". Objects are only fetched while
// objects are enabled.
std::uint32_t Ppu::object_penalty() const noexcept {
    constexpr std::uint32_t kFlat = 6;
    constexpr std::uint32_t kAtXZero = 11;
    if ((lcdc_ & 0x02U) == 0) {
        return 0;
    }
    std::uint32_t penalty = 0;
    std::array<bool, 32> tile_seen{};
    for (std::uint8_t i = 0; i < line_object_count_; ++i) {
        const std::uint32_t x = oam_.at(std::size_t{line_objects_.at(i)} * kObjectBytes + 1);
        if (x == 0) {
            penalty += kAtXZero;
            continue;
        }
        if (x >= 168) {
            continue;
        }
        const std::uint32_t bg_x = x + scx_;
        const std::uint32_t tile = (bg_x / 8) % 32;
        if (!tile_seen.at(tile)) {
            tile_seen.at(tile) = true;
            const std::uint32_t right_pixels = 7 - (bg_x % 8);
            penalty += right_pixels > 2 ? right_pixels - 2 : 0;
        }
        penalty += kFlat;
    }
    return penalty;
}

bool Ppu::stat_line() const noexcept {
    const bool hblank = mode_ == Mode::HBlank && (stat_select_ & kSelectHBlank) != 0;
    const bool vblank = mode_ == Mode::VBlank && (stat_select_ & kSelectVBlank) != 0;
    // The OAM scan select also fires when line 144 starts (Mooneye
    // vblank_stat_intr-GS).
    const bool oam = (stat_select_ & kSelectOamScan) != 0 &&
                     (mode_ == Mode::OamScan || (line_ == kVisibleLines && dot_ == 0));
    const bool lyc = lyc_equal_ && (stat_select_ & kSelectLyc) != 0;
    return hblank || vblank || oam || lyc;
}

std::uint8_t Ppu::update_stat() noexcept {
    lyc_equal_ = line_ == lyc_;
    const bool high = stat_line();
    const bool rising = high && !stat_line_high_;
    stat_line_high_ = high;
    return rising ? kStatBit : std::uint8_t{0};
}

std::uint8_t Ppu::read_vram(std::uint16_t address) const noexcept {
    return vram_accessible() ? vram_.at(address - kVramStart) : kOpenBus;
}

void Ppu::write_vram(std::uint16_t address, std::uint8_t value) noexcept {
    if (vram_accessible()) {
        vram_.at(address - kVramStart) = value;
    }
}

std::uint8_t Ppu::read_oam(std::uint16_t address) const noexcept {
    return oam_accessible() ? oam_.at(address - kOamStart) : kOpenBus;
}

void Ppu::write_oam(std::uint16_t address, std::uint8_t value) noexcept {
    if (oam_accessible()) {
        oam_.at(address - kOamStart) = value;
    }
}

std::uint8_t Ppu::dma_read_vram(std::uint16_t address) const noexcept {
    return vram_.at(address - kVramStart);
}

void Ppu::dma_write_oam(std::uint8_t index, std::uint8_t value) noexcept {
    oam_.at(index) = value;
}

Ppu::Mode Ppu::mode() const noexcept {
    return lcd_enabled() ? mode_ : Mode::HBlank;
}

bool Ppu::vram_accessible() const noexcept {
    return mode() != Mode::Drawing;
}

bool Ppu::oam_accessible() const noexcept {
    const Mode m = mode();
    return m != Mode::Drawing && m != Mode::OamScan;
}

std::uint32_t Ppu::drawing_dots() const noexcept {
    return drawing_dots_;
}

// LY already shows the next line during the last machine cycle of a line
// (Mooneye hblank_ly_scx_timing-GS; docs/ppu-notes.md, section 2).
std::uint8_t Ppu::ly() const noexcept {
    if (!lcd_enabled()) {
        return 0;
    }
    constexpr std::uint32_t kEarlyDots = 4;
    if (dot_ >= kTicksPerLine - kEarlyDots) {
        return static_cast<std::uint8_t>((line_ + 1) % kLinesPerFrame);
    }
    return static_cast<std::uint8_t>(line_);
}

bool Ppu::lcd_enabled() const noexcept {
    return (lcdc_ & kLcdEnable) != 0;
}

}  // namespace core
