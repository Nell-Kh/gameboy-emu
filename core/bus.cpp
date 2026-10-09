#include "core/bus.h"

namespace core {

namespace {

constexpr std::uint8_t kInterruptBits = 0x1F;
// With no cartridge the data lines float high.
constexpr std::uint8_t kOpenBus = 0xFF;

}  // namespace

void Bus::load_rom(std::span<const std::uint8_t> rom) {
    rom_.assign(rom.begin(), rom.end());
}

std::uint8_t Bus::read8(std::uint16_t address) const noexcept {
    if (address < kRomRegionSize) {
        return address < rom_.size() ? rom_[address] : kOpenBus;
    }
    switch (address) {
        case Serial::kData:
        case Serial::kControl:
            return serial_.read(address);
        case Timer::kDiv:
        case Timer::kTima:
        case Timer::kTma:
        case Timer::kTac:
            return timer_.read(address);
        case kInterruptFlag:
            // The top three bits do not exist and read as 1.
            return static_cast<std::uint8_t>(interrupt_flag_ | ~kInterruptBits);
        case kInterruptEnable:
            return interrupt_enable_;
        default:
            return memory_[address];
    }
}

void Bus::write8(std::uint16_t address, std::uint8_t value) {
    if (address < kRomRegionSize) {
        // ROM cannot be written. Cartridges with a mapper chip watch these
        // writes to switch banks; that is M2.
        return;
    }
    switch (address) {
        case Serial::kData:
        case Serial::kControl:
            serial_.write(address, value);
            break;
        case Timer::kDiv:
        case Timer::kTima:
        case Timer::kTma:
        case Timer::kTac:
            timer_.write(address, value);
            break;
        case kInterruptFlag:
            interrupt_flag_ = value & kInterruptBits;
            break;
        case kInterruptEnable:
            interrupt_enable_ = value;
            break;
        default:
            memory_[address] = value;
            break;
    }
}

void Bus::tick(std::uint32_t t_cycles) noexcept {
    cycles_ += t_cycles;
    if (timer_.tick(t_cycles)) {
        request_interrupt(Interrupt::Timer);
    }
    if (serial_.tick(t_cycles)) {
        request_interrupt(Interrupt::Serial);
    }
}

std::uint64_t Bus::cycles() const noexcept {
    return cycles_;
}

void Bus::request_interrupt(Interrupt source) noexcept {
    interrupt_flag_ |= static_cast<std::uint8_t>(source);
}

void Bus::acknowledge_interrupt(Interrupt source) noexcept {
    interrupt_flag_ =
        static_cast<std::uint8_t>(interrupt_flag_ & ~static_cast<std::uint8_t>(source));
}

bool Bus::interrupt_requested(Interrupt source) const noexcept {
    return (interrupt_flag_ & static_cast<std::uint8_t>(source)) != 0;
}

std::uint8_t Bus::pending_interrupts() const noexcept {
    return interrupt_flag_ & interrupt_enable_ & kInterruptBits;
}

const std::string& Bus::serial_output() const noexcept {
    return serial_.output();
}

}  // namespace core
