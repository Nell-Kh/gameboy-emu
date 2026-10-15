#pragma once

#include <cstddef>

namespace core {

// Mask for a bank number on a chip of `total_bytes`: banks wrap at the chip
// size, which is a power of two, because the unused address lines are not
// connected. A chip smaller than one bank still counts as one.
constexpr std::size_t bank_mask(std::size_t total_bytes, std::size_t bytes_per_bank) noexcept {
    const std::size_t banks = total_bytes / bytes_per_bank;
    return banks == 0 ? 0 : banks - 1;
}

}  // namespace core
