#pragma once

#include <cstdint>

#include "core/bus.h"
#include "core/registers.h"

namespace core {

// The Sharp SM83 CPU.
//
// Timing model (ADR-005): one machine cycle is 4 clock ticks (T-cycles), and
// the CPU does at most one memory access per machine cycle. So every memory
// access advances the rest of the machine by 4 ticks before it happens, and
// the other components always see the access at the right moment.
class Cpu {
public:
    static constexpr std::uint32_t kTicksPerMachineCycle = 4;

    explicit Cpu(Bus& bus) noexcept;

    // Executes one instruction and returns how many clock ticks it took.
    // The ticks have already been applied to the bus by the time this returns.
    std::uint32_t step() noexcept;

    [[nodiscard]] Registers& registers() noexcept;
    [[nodiscard]] const Registers& registers() const noexcept;

    // True once the CPU has hit an opcode it cannot execute. Real hardware
    // freezes on its 11 undefined opcodes; a locked CPU only burns time.
    [[nodiscard]] bool locked() const noexcept;

private:
    // One machine cycle with a memory access.
    [[nodiscard]] std::uint8_t read8(std::uint16_t address) noexcept;
    void write8(std::uint16_t address, std::uint8_t value) noexcept;

    // One machine cycle with no memory access (the CPU is busy internally).
    void internal_cycle() noexcept;

    // Read the byte(s) at PC and move PC past them. 16-bit values are stored
    // low byte first.
    [[nodiscard]] std::uint8_t fetch8() noexcept;
    [[nodiscard]] std::uint16_t fetch16() noexcept;

    void execute(std::uint8_t opcode) noexcept;

    Bus& bus_;
    Registers reg_ = Registers::post_boot_dmg();
    bool locked_ = false;
};

}  // namespace core
