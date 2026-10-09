#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/interrupts.h"
#include "core/oam_dma.h"
#include "core/ppu.h"
#include "core/serial.h"
#include "core/timer.h"

namespace core {

// Everything the CPU can address: 16-bit addresses, so 64 KiB.
// The CPU never touches memory directly; every read and write goes through here
// and is routed to whatever lives at that address.
//
//   0x0000-0x7FFF  cartridge ROM (read-only)
//   0x8000-0xDFFF  RAM: video, cartridge and work RAM (plain memory for now)
//   0xE000-0xFDFF  echo RAM, a mirror of 0xC000-0xDDFF
//   0xFE00-0xFE9F  sprite table (OAM); reads 0xFF while an OAM DMA runs
//   0xFEA0-0xFEFF  not connected: reads 0x00, ignores writes
//   0xFF00-0xFF7F  I/O registers; addresses with no register read 0xFF
//   0xFF80-0xFFFE  high RAM
//   0xFFFF         interrupt enable register (IE)
class Bus {
public:
    static constexpr std::size_t kAddressSpace = 0x10000;
    static constexpr std::size_t kRomRegionSize = 0x8000;

    static constexpr std::uint16_t kInterruptFlag = 0xFF0F;
    static constexpr std::uint16_t kInterruptEnable = 0xFFFF;

    // Inserts a cartridge. Only the first 32 KiB are visible until bank
    // switching (MBC) arrives in M2.
    void load_rom(std::span<const std::uint8_t> rom);

    [[nodiscard]] std::uint8_t read8(std::uint16_t address) const noexcept;
    void write8(std::uint16_t address, std::uint8_t value);

    // Advances the rest of the machine by `t_cycles` clock ticks.
    // The CPU calls this on every memory access (see ADR-005).
    void tick(std::uint32_t t_cycles) noexcept;

    // Total clock ticks since power-on.
    [[nodiscard]] std::uint64_t cycles() const noexcept;

    // Interrupts: a component requests one by setting its bit in IF. The CPU
    // services a request only if the same bit is set in IE.
    void request_interrupt(Interrupt source) noexcept;
    void acknowledge_interrupt(Interrupt source) noexcept;
    // True if `source` is requested, whether or not it is enabled.
    [[nodiscard]] bool interrupt_requested(Interrupt source) const noexcept;
    // The requested-and-enabled sources, as a bit mask (IF & IE).
    [[nodiscard]] std::uint8_t pending_interrupts() const noexcept;

    // Every byte the program has sent over the serial port.
    [[nodiscard]] const std::string& serial_output() const noexcept;

private:
    // Reads as the DMA controller sees memory: no OAM blocking, and sources
    // from 0xE000 up reach work RAM, as echo RAM does for the CPU.
    [[nodiscard]] std::uint8_t dma_read(std::uint16_t address) const noexcept;
    void step_dma() noexcept;

    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, kAddressSpace> memory_{};
    Serial serial_;
    Timer timer_;
    Ppu ppu_;
    OamDma dma_;
    // Ticks not yet making up a whole machine cycle, for the DMA.
    std::uint32_t dma_ticks_ = 0;
    // The boot ROM leaves the VBlank request set.
    std::uint8_t interrupt_flag_ = 0x01;
    std::uint8_t interrupt_enable_ = 0x00;
    std::uint64_t cycles_ = 0;
};

}  // namespace core
