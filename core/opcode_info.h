#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace core {

// What the opcode table says about one instruction. Used by the trace output
// and by the tests that check the CPU against the table.
struct OpcodeInfo {
    std::string_view mnemonic;  // e.g. "LD [HL+], A"
    std::uint8_t bytes;         // length including the opcode (and 0xCB prefix)
    std::uint8_t ticks;         // duration; for conditional jumps, when taken
    std::uint8_t ticks_not_taken;
    // One character per flag in Z N H C order:
    // '-' unchanged, '0' cleared, '1' set, a letter = depends on the result.
    std::string_view flags;
};

// Indexed by opcode. Defined in opcodes.gen.cpp.
extern const std::array<OpcodeInfo, 256> kOpcodeInfo;
// Indexed by the byte that follows a 0xCB prefix.
extern const std::array<OpcodeInfo, 256> kCbOpcodeInfo;

}  // namespace core
