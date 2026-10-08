#pragma once

#include <string_view>

namespace core {

// The whole machine: CPU, bus, PPU, APU, timers. A stub until M1.
class GameBoy {
public:
    static constexpr int kScreenWidth = 160;
    static constexpr int kScreenHeight = 144;

    [[nodiscard]] static std::string_view version() noexcept;
};

}  // namespace core
