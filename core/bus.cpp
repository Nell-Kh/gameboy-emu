#include "core/bus.h"

#include <array>

namespace core {

namespace {

constexpr std::uint8_t kInterruptBits = 0x1F;
// With nothing driving the data lines they float high.
constexpr std::uint8_t kOpenBus = 0xFF;

// 0xE000-0xFDFF is a mirror of 0xC000-0xDDFF: the work RAM chip only decodes
// 13 address lines.
constexpr std::uint16_t kEchoStart = 0xE000;
constexpr std::uint16_t kEchoEnd = 0xFDFF;
constexpr std::uint16_t kEchoOffset = 0x2000;

// 0xFEA0-0xFEFF, between the sprite table and the I/O registers, is not
// connected to anything. On the DMG it reads 0x00 and ignores writes.
constexpr std::uint16_t kUnusableStart = 0xFEA0;
constexpr std::uint16_t kIoStart = 0xFF00;
constexpr std::uint8_t kUnusableValue = 0x00;

// Folds an echo RAM address onto the work RAM it mirrors.
constexpr std::uint16_t resolve_echo(std::uint16_t address) noexcept {
    if (address >= kEchoStart && address <= kEchoEnd) {
        return static_cast<std::uint16_t>(address - kEchoOffset);
    }
    return address;
}

constexpr bool in_unusable_region(std::uint16_t address) noexcept {
    return address >= kUnusableStart && address < kIoStart;
}

// The addresses in 0xFF00-0xFF7F where the DMG has no register at all. They
// read 0xFF and ignore writes. 0xFF50 (boot ROM switch) is included: once the
// boot ROM has run it behaves the same way.
constexpr bool is_unmapped_io(std::uint16_t address) noexcept {
    return address == 0xFF03 || (address >= 0xFF08 && address <= 0xFF0E) || address == 0xFF15 ||
           address == 0xFF1F || (address >= 0xFF27 && address <= 0xFF2F) ||
           (address >= 0xFF4C && address <= 0xFF7F);
}

// I/O registers that are not emulated yet are kept as plain memory, but bits
// that do not exist, or can only be written, read back as 1 on hardware. This
// table holds those bits for each address in 0xFF00-0xFF7F.
constexpr std::array<std::uint8_t, 0x80> make_io_read_mask() noexcept {
    std::array<std::uint8_t, 0x80> mask{};
    // Joypad: bits 6-7 do not exist. With no buttons pressed the four input
    // lines (bits 0-3) read 1.
    mask[0x00] = 0xCF;
    // Sound. Lengths and frequencies are write-only; several bits are unused.
    mask[0x10] = 0x80;  // NR10
    mask[0x11] = 0x3F;  // NR11
    mask[0x13] = 0xFF;  // NR13
    mask[0x14] = 0xBF;  // NR14
    mask[0x16] = 0x3F;  // NR21
    mask[0x18] = 0xFF;  // NR23
    mask[0x19] = 0xBF;  // NR24
    mask[0x1A] = 0x7F;  // NR30
    mask[0x1B] = 0xFF;  // NR31
    mask[0x1C] = 0x9F;  // NR32
    mask[0x1D] = 0xFF;  // NR33
    mask[0x1E] = 0xBF;  // NR34
    mask[0x20] = 0xFF;  // NR41
    mask[0x23] = 0xBF;  // NR44
    mask[0x26] = 0x70;  // NR52
    // STAT: bit 7 does not exist.
    mask[0x41] = 0x80;
    return mask;
}

constexpr std::array<std::uint8_t, 0x80> kIoReadMask = make_io_read_mask();

constexpr bool is_io(std::uint16_t address) noexcept {
    return address >= kIoStart && address < kIoStart + kIoReadMask.size();
}

}  // namespace

void Bus::load_rom(std::span<const std::uint8_t> rom) {
    rom_.assign(rom.begin(), rom.end());
}

std::uint8_t Bus::read8(std::uint16_t address) const noexcept {
    if (address < kRomRegionSize) {
        return address < rom_.size() ? rom_[address] : kOpenBus;
    }
    address = resolve_echo(address);
    if (in_unusable_region(address)) {
        return kUnusableValue;
    }
    if (is_unmapped_io(address)) {
        return kOpenBus;
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
        case Ppu::kLcdc:
        case Ppu::kLy:
            return ppu_.read(address);
        case kInterruptFlag:
            // The top three bits do not exist and read as 1.
            return static_cast<std::uint8_t>(interrupt_flag_ | ~kInterruptBits);
        case kInterruptEnable:
            return interrupt_enable_;
        default:
            if (is_io(address)) {
                return memory_[address] | kIoReadMask.at(address - kIoStart);
            }
            return memory_[address];
    }
}

void Bus::write8(std::uint16_t address, std::uint8_t value) {
    if (address < kRomRegionSize) {
        // ROM cannot be written. Cartridges with a mapper chip watch these
        // writes to switch banks; that is M2.
        return;
    }
    address = resolve_echo(address);
    if (in_unusable_region(address) || is_unmapped_io(address)) {
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
        case Ppu::kLcdc:
        case Ppu::kLy:
            ppu_.write(address, value);
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
    ppu_.tick(t_cycles);
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
