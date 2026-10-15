#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/bus.h"
#include "core/cartridge/rtc.h"
#include "core/cpu.h"

namespace core {

// The whole machine. Owns the bus (memory, timer, serial port) and the CPU,
// and is the one class a frontend or a test needs to talk to.
class GameBoy {
public:
    static constexpr int kScreenWidth = 160;
    static constexpr int kScreenHeight = 144;
    // The master clock: 4.194304 MHz, i.e. this many ticks per second.
    static constexpr std::uint64_t kTicksPerSecond = 4'194'304;
    // One picture: 154 lines of 456 ticks, about 59.73 frames per second.
    static constexpr std::uint64_t kTicksPerFrame =
        std::uint64_t{Ppu::kLinesPerFrame} * Ppu::kTicksPerLine;

    [[nodiscard]] static std::string_view version() noexcept;

    GameBoy() = default;
    // The CPU keeps a reference to the bus, so the pair cannot be copied or moved.
    GameBoy(const GameBoy&) = delete;
    GameBoy& operator=(const GameBoy&) = delete;
    GameBoy(GameBoy&&) = delete;
    GameBoy& operator=(GameBoy&&) = delete;
    ~GameBoy() = default;

    // Inserts the cartridge described by a ROM image. Execution starts at
    // 0x0100, as it does after the real boot ROM. Returns an empty string on
    // success, or why the ROM cannot be used; the machine is then unchanged.
    [[nodiscard]] std::string load_rom(std::span<const std::uint8_t> rom);

    // Runs one CPU instruction and returns how many clock ticks it took.
    std::uint32_t step();

    // Runs whole instructions until at least `t_cycles` ticks have passed.
    // Returns the ticks actually run, which can overshoot by one instruction.
    std::uint64_t run_for(std::uint64_t t_cycles);

    // Runs whole instructions until the PPU finishes a picture (line 144
    // starts), so frame() then holds one complete frame. With the LCD off no
    // picture is finished; it stops after kTicksPerFrame ticks instead.
    // Returns the ticks run.
    std::uint64_t run_frame();

    // Total clock ticks since power-on.
    [[nodiscard]] std::uint64_t cycles() const noexcept;

    // Presses or releases a button. A frontend calls this as input arrives.
    void set_button(Joypad::Button button, bool pressed) noexcept;

    // Whether the cartridge has a battery with something to keep (RAM, a
    // clock or both), so the frontend keeps a save file for it
    // (docs/cartridge-notes.md, section 6).
    [[nodiscard]] bool has_battery() const noexcept;
    // Battery-backed cartridge RAM, for the save file. Empty if the cartridge
    // has no battery or no RAM.
    [[nodiscard]] std::span<const std::uint8_t> battery_ram() const noexcept;
    // Loads a save file's RAM. Returns false, and changes nothing, if the
    // cartridge has no battery RAM or `data` is not exactly its size.
    [[nodiscard]] bool load_battery_ram(std::span<const std::uint8_t> data) noexcept;
    // Whether the program has changed battery RAM, or written a clock
    // register, since the last clear.
    [[nodiscard]] bool battery_ram_changed() const noexcept;
    void clear_battery_ram_changed() noexcept;
    // The cartridge's real-time clock (MBC3 + timer), for the save file's
    // clock footer and the catch-up at load; null for every other cartridge.
    [[nodiscard]] Rtc* clock() noexcept;
    [[nodiscard]] const Rtc* clock() const noexcept;

    // The screen: 160x144 shades, 0 (white) to 3 (black), row by row.
    [[nodiscard]] const Ppu::Frame& frame() const noexcept;

    // Everything the program has sent over the serial port so far.
    [[nodiscard]] const std::string& serial_output() const noexcept;

    [[nodiscard]] const Cpu& cpu() const noexcept;
    [[nodiscard]] const Bus& bus() const noexcept;

private:
    Bus bus_;
    Cpu cpu_{bus_};
};

}  // namespace core
