#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace test {

// Where cartridge code starts: PC points here after boot.
inline constexpr std::uint16_t kEntry = 0x0100;

// Builds a 32 KiB cartridge image that is all zeros (NOPs) except for
// `program`, placed at `address`.
inline std::vector<std::uint8_t> make_rom(std::initializer_list<std::uint8_t> program,
                                          std::uint16_t address = kEntry) {
    std::vector<std::uint8_t> rom(0x8000, 0x00);
    std::copy(program.begin(), program.end(), rom.begin() + address);
    return rom;
}

}  // namespace test
