#pragma once

#include <cstdint>

namespace core {

// The five interrupt sources. The value is the source's bit in the IF and IE
// registers; a lower bit means a higher priority.
enum class Interrupt : std::uint8_t {
    VBlank = 0x01,
    LcdStat = 0x02,
    Timer = 0x04,
    Serial = 0x08,
    Joypad = 0x10,
};

}  // namespace core
