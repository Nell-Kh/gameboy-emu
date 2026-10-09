#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/bus.h"
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

    // Total clock ticks since power-on.
    [[nodiscard]] std::uint64_t cycles() const noexcept;

    // Everything the program has sent over the serial port so far.
    [[nodiscard]] const std::string& serial_output() const noexcept;

    [[nodiscard]] const Cpu& cpu() const noexcept;
    [[nodiscard]] const Bus& bus() const noexcept;

private:
    Bus bus_;
    Cpu cpu_{bus_};
};

}  // namespace core
