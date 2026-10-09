#include "core/serial.h"

namespace core {

namespace {

constexpr std::uint8_t kTransferStart = 0x80;
constexpr std::uint8_t kInternalClock = 0x01;
// Bits 1-6 of SC do not exist on the DMG and read as 1.
constexpr std::uint8_t kControlUnusedBits = 0x7E;

}  // namespace

std::uint8_t Serial::read(std::uint16_t address) const noexcept {
    if (address == kData) {
        return data_;
    }
    return control_ | kControlUnusedBits;
}

void Serial::write(std::uint16_t address, std::uint8_t value) {
    if (address == kData) {
        data_ = value;
        return;
    }
    control_ = value & (kTransferStart | kInternalClock);
    // A transfer only runs if we provide the clock. With an external clock it
    // would wait for the other Game Boy, and there is none.
    if (control_ == (kTransferStart | kInternalClock)) {
        output_.push_back(static_cast<char>(data_));
        ticks_left_ = kTicksPerTransfer;
    } else {
        ticks_left_ = 0;
    }
}

bool Serial::tick(std::uint32_t t_cycles) noexcept {
    if (ticks_left_ == 0) {
        return false;
    }
    if (t_cycles < ticks_left_) {
        ticks_left_ -= t_cycles;
        return false;
    }
    ticks_left_ = 0;
    data_ = 0xFF;
    control_ = static_cast<std::uint8_t>(control_ & ~kTransferStart);
    return true;
}

const std::string& Serial::output() const noexcept {
    return output_;
}

}  // namespace core
