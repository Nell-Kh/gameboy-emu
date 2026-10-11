#pragma once

#include <array>
#include <cstdint>

namespace core {

// The picture processing unit: video RAM, the sprite table (OAM), the LCD
// registers, the per-dot mode state machine and the PPU interrupts.
// docs/ppu-notes.md is the specification this follows.
//
// One dot is one clock tick. Every line is 456 dots: mode 2 (OAM scan, 80
// dots), mode 3 (drawing, 172-289 dots), then mode 0 (HBlank) for the rest.
// Lines 144-153 are mode 1 (VBlank). Pixels are not produced yet; mode 3's
// length is still computed, because the STAT timing depends on it.
class Ppu {
public:
    enum class Mode : std::uint8_t { HBlank = 0, VBlank = 1, OamScan = 2, Drawing = 3 };

    static constexpr std::uint16_t kLcdc = 0xFF40;
    static constexpr std::uint16_t kStat = 0xFF41;
    static constexpr std::uint16_t kScy = 0xFF42;
    static constexpr std::uint16_t kScx = 0xFF43;
    static constexpr std::uint16_t kLy = 0xFF44;
    static constexpr std::uint16_t kLyc = 0xFF45;
    static constexpr std::uint16_t kBgp = 0xFF47;
    static constexpr std::uint16_t kObp0 = 0xFF48;
    static constexpr std::uint16_t kObp1 = 0xFF49;
    static constexpr std::uint16_t kWy = 0xFF4A;
    static constexpr std::uint16_t kWx = 0xFF4B;

    static constexpr std::uint16_t kVramStart = 0x8000;
    static constexpr std::uint16_t kVramSize = 0x2000;
    static constexpr std::uint16_t kOamStart = 0xFE00;
    static constexpr std::uint16_t kOamSize = 0xA0;

    static constexpr std::uint32_t kTicksPerLine = 456;
    static constexpr std::uint32_t kVisibleLines = 144;
    static constexpr std::uint32_t kLinesPerFrame = 154;
    static constexpr std::uint32_t kOamScanDots = 80;
    static constexpr std::uint32_t kMinDrawingDots = 172;

    // True for the LCD register addresses this class owns (not 0xFF46).
    [[nodiscard]] static bool is_register(std::uint16_t address) noexcept;

    // Registers. Writes return the interrupts they caused, as Interrupt bits:
    // selecting a STAT condition that already holds raises the STAT interrupt.
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    [[nodiscard]] std::uint8_t write(std::uint16_t address, std::uint8_t value) noexcept;

    // Advances the PPU. Returns the interrupts requested during these ticks.
    [[nodiscard]] std::uint8_t tick(std::uint32_t t_cycles) noexcept;

    // CPU access to video RAM and OAM. Blocked reads return 0xFF and blocked
    // writes are ignored: VRAM during mode 3, OAM during modes 2 and 3.
    [[nodiscard]] std::uint8_t read_vram(std::uint16_t address) const noexcept;
    void write_vram(std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t read_oam(std::uint16_t address) const noexcept;
    void write_oam(std::uint16_t address, std::uint8_t value) noexcept;

    // Access that ignores the mode, for OAM DMA.
    [[nodiscard]] std::uint8_t dma_read_vram(std::uint16_t address) const noexcept;
    void dma_write_oam(std::uint8_t index, std::uint8_t value) noexcept;

    [[nodiscard]] Mode mode() const noexcept;
    [[nodiscard]] bool vram_accessible() const noexcept;
    [[nodiscard]] bool oam_accessible() const noexcept;
    // Length of mode 3 on the current (or last) drawn line, in dots.
    [[nodiscard]] std::uint32_t drawing_dots() const noexcept;

private:
    [[nodiscard]] bool lcd_enabled() const noexcept;
    [[nodiscard]] std::uint8_t ly() const noexcept;
    void step_dot() noexcept;
    void start_line() noexcept;
    void start_drawing() noexcept;
    [[nodiscard]] std::uint32_t object_penalty() const noexcept;
    // Recomputes LYC = LY and the STAT line; returns kStatBit on a rising edge.
    [[nodiscard]] std::uint8_t update_stat() noexcept;
    [[nodiscard]] bool stat_line() const noexcept;

    std::array<std::uint8_t, kVramSize> vram_{};
    std::array<std::uint8_t, kOamSize> oam_{};

    // Post-boot register values.
    std::uint8_t lcdc_ = 0x91;
    std::uint8_t stat_select_ = 0x00;  // STAT bits 3-6
    std::uint8_t scy_ = 0x00;
    std::uint8_t scx_ = 0x00;
    std::uint8_t lyc_ = 0x00;
    std::uint8_t bgp_ = 0xFC;
    std::uint8_t obp0_ = 0x00;
    std::uint8_t obp1_ = 0x00;
    std::uint8_t wy_ = 0x00;
    std::uint8_t wx_ = 0x00;

    std::uint32_t line_ = 0;
    std::uint32_t dot_ = 0;
    Mode mode_ = Mode::OamScan;
    std::uint32_t drawing_dots_ = kMinDrawingDots;
    // The first line after the LCD is switched on has no OAM scan.
    bool first_line_ = false;
    // Set from the first line on which LY = WY until the next VBlank.
    bool window_y_reached_ = false;
    bool lyc_equal_ = true;
    bool stat_line_high_ = false;
    // Indices into OAM of the objects found by the last OAM scan.
    std::array<std::uint8_t, 10> line_objects_{};
    std::uint8_t line_object_count_ = 0;
};

}  // namespace core
