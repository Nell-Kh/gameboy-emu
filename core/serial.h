#pragma once

#include <cstdint>
#include <string>

namespace core {

// The link-cable port: SB (0xFF01) holds the byte to send, SC (0xFF02) starts
// and reports a transfer.
//
// Nothing is plugged in, so every transfer sends our byte out and shifts 0xFF
// in. The bytes sent are kept in output(), which is how the Blargg test ROMs
// report their result.
class Serial {
public:
    static constexpr std::uint16_t kData = 0xFF01;
    static constexpr std::uint16_t kControl = 0xFF02;

    // 8 bits at 8192 Hz, with a 4194304 Hz clock: 512 ticks per bit.
    static constexpr std::uint32_t kTicksPerTransfer = 8 * 512;

    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    void write(std::uint16_t address, std::uint8_t value);

    // Advances the port. Returns true if a transfer finished during these
    // ticks, which is when the serial interrupt is requested.
    [[nodiscard]] bool tick(std::uint32_t t_cycles) noexcept;

    // Every byte sent so far, in order.
    [[nodiscard]] const std::string& output() const noexcept;

private:
    std::uint8_t data_ = 0x00;
    std::uint8_t control_ = 0x00;
    std::uint32_t ticks_left_ = 0;
    std::string output_;
};

}  // namespace core
