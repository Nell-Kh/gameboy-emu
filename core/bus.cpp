#include "core/bus.h"

#include <algorithm>

namespace core {

std::uint8_t Bus::read8(std::uint16_t address) const noexcept {
    return memory_[address];
}

void Bus::write8(std::uint16_t address, std::uint8_t value) noexcept {
    memory_[address] = value;
}

bool Bus::load(std::span<const std::uint8_t> bytes, std::uint16_t address) noexcept {
    if (bytes.size() > kAddressSpace - address) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), memory_.begin() + address);
    return true;
}

}  // namespace core
