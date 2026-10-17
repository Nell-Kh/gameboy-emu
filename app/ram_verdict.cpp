#include "app/ram_verdict.h"

#include <array>
#include <cstddef>

namespace frontend {

namespace {

constexpr std::uint8_t kRunning = 0x80;
constexpr std::array<std::uint8_t, 3> kSignature = {0xDE, 0xB0, 0x61};
constexpr std::size_t kTextStart = 4;

}  // namespace

std::optional<RamVerdict> read_ram_verdict(std::span<const std::uint8_t> ram) {
    if (ram.size() <= kTextStart) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kSignature.size(); ++i) {
        if (ram[1 + i] != kSignature.at(i)) {
            return std::nullopt;
        }
    }
    if (ram[0] == kRunning) {
        return std::nullopt;
    }
    RamVerdict verdict{.status = ram[0], .text = {}};
    for (std::size_t i = kTextStart; i < ram.size() && ram[i] != 0; ++i) {
        verdict.text += static_cast<char>(ram[i]);
    }
    return verdict;
}

}  // namespace frontend
