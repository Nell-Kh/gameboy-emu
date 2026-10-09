#include "core/oam_dma.h"

namespace core {

std::uint8_t OamDma::read() const noexcept {
    return register_;
}

void OamDma::write(std::uint8_t value) noexcept {
    register_ = value;
    requested_ = value;
}

std::optional<std::uint16_t> OamDma::step() noexcept {
    // The previous cycle copied the last byte: the transfer is over.
    if (active_ && (*active_ & 0xFFU) == kLength) {
        active_.reset();
    }
    // A transfer that was latched last cycle starts now, replacing any that
    // is still running.
    if (starting_) {
        active_ = static_cast<std::uint16_t>(*starting_ << 8U);
        starting_.reset();
    }
    std::optional<std::uint16_t> copy;
    if (active_) {
        copy = active_;
        ++*active_;
    }
    // A write to 0xFF46 is latched one cycle after it happens.
    if (requested_) {
        starting_ = requested_;
        requested_.reset();
    }
    return copy;
}

bool OamDma::oam_blocked() const noexcept {
    return active_.has_value();
}

}  // namespace core
