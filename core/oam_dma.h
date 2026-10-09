#pragma once

#include <cstdint>
#include <optional>

namespace core {

// OAM DMA: a hardware copy of 160 bytes from XX00-XX9F into the sprite table
// (OAM, 0xFE00-0xFE9F), started by writing XX to 0xFF46.
//
// The controller only keeps track of where the copy is. The bus does the
// actual reads and writes, one byte per machine cycle, and asks
// oam_blocked() whether the CPU may touch OAM.
//
// Timeline, counting the machine cycle that writes 0xFF46 as cycle 0:
//   cycle 1        the request is latched; OAM is still accessible
//   cycles 2-161   one byte per cycle, 160 in all; OAM is blocked
//   cycle 162      OAM is free again
// Writing 0xFF46 during a transfer restarts it with the new source; the old
// one keeps running, and OAM stays blocked, until the new one takes over.
class OamDma {
public:
    static constexpr std::uint16_t kRegister = 0xFF46;
    static constexpr std::uint16_t kLength = 0xA0;

    // The value last written to 0xFF46 reads back unchanged.
    [[nodiscard]] std::uint8_t read() const noexcept;
    void write(std::uint8_t value) noexcept;

    // Advances one machine cycle. Returns the source address of the byte to
    // copy in this cycle, if any; it goes to OAM at (source & 0xFF).
    [[nodiscard]] std::optional<std::uint16_t> step() noexcept;

    [[nodiscard]] bool oam_blocked() const noexcept;

private:
    std::uint8_t register_ = 0xFF;
    std::optional<std::uint8_t> requested_;
    std::optional<std::uint8_t> starting_;
    // The next source address to copy while a transfer runs.
    std::optional<std::uint16_t> active_;
};

}  // namespace core
