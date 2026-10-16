#include "core/oam_dma.h"

#include "core/state_io.h"

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

void OamDma::save_state(StateWriter& out) const {
    out.u8(register_);
    out.optional_u8(requested_);
    out.optional_u8(starting_);
    out.optional_u16(active_);
}

void OamDma::load_state(StateReader& in) {
    constexpr std::uint16_t kLowByte = 0xFF;
    register_ = in.u8();
    requested_ = in.optional_u8();
    starting_ = in.optional_u8();
    active_ = in.optional_u16();
    // A running transfer is between its first byte and one past its last.
    if (active_ && (*active_ & kLowByte) > kLength) {
        in.fail("a value in section DMA  is out of range");
        active_.reset();
    }
}

}  // namespace core
