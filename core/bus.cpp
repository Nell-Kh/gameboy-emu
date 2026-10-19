#include "core/bus.h"

#include <array>
#include <utility>

#include "core/cartridge/no_mbc.h"
#include "core/state_io.h"

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

constexpr std::uint16_t kCartridgeRamStart = 0xA000;
constexpr std::uint16_t kCartridgeRamEnd = 0xBFFF;

constexpr bool in_cartridge_ram(std::uint16_t address) noexcept {
    return address >= kCartridgeRamStart && address <= kCartridgeRamEnd;
}

constexpr std::uint16_t kOamStart = Ppu::kOamStart;
constexpr std::uint16_t kVramStart = Ppu::kVramStart;
constexpr std::uint16_t kVramEnd = Ppu::kVramStart + Ppu::kVramSize - 1;

constexpr bool in_vram(std::uint16_t address) noexcept {
    return address >= kVramStart && address <= kVramEnd;
}
constexpr std::uint16_t kTicksPerMachineCycle = 4;

constexpr bool in_oam(std::uint16_t address) noexcept {
    return address >= kOamStart && address < kUnusableStart;
}

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

}  // namespace

Cartridge* Bus::cartridge() noexcept {
    return cartridge_.get();
}

const Cartridge* Bus::cartridge() const noexcept {
    return cartridge_.get();
}

void Bus::insert_cartridge(std::unique_ptr<Cartridge> cartridge) noexcept {
    cartridge_ = std::move(cartridge);
}

void Bus::load_rom(std::span<const std::uint8_t> rom) {
    cartridge_ = std::make_unique<NoMbc>(rom, 0);
}

std::uint8_t Bus::read8(std::uint16_t address) const noexcept {
    if (address < kRomRegionSize) {
        return cartridge_ ? cartridge_->read_rom(address) : kOpenBus;
    }
    if (in_cartridge_ram(address)) {
        return cartridge_ ? cartridge_->read_ram(address) : kOpenBus;
    }
    if (in_vram(address)) {
        return ppu_.read_vram(address);
    }
    address = resolve_echo(address);
    if (in_oam(address)) {
        return dma_.oam_blocked() ? kOpenBus : ppu_.read_oam(address);
    }
    if (in_unusable_region(address)) {
        return kUnusableValue;
    }
    if (is_unmapped_io(address)) {
        return kOpenBus;
    }
    if (Ppu::is_register(address)) {
        return ppu_.read(address);
    }
    if (Apu::is_register(address)) {
        return apu_.read(address);
    }
    switch (address) {
        case Joypad::kRegister:
            return joypad_.read();
        case Serial::kData:
        case Serial::kControl:
            return serial_.read(address);
        case Timer::kDiv:
        case Timer::kTima:
        case Timer::kTma:
        case Timer::kTac:
            return timer_.read(address);
        case OamDma::kRegister:
            return dma_.read();
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
        // ROM cannot be written; a mapper chip uses these writes to switch banks.
        if (cartridge_) {
            cartridge_->write_rom(address, value);
        }
        return;
    }
    if (in_cartridge_ram(address)) {
        if (cartridge_) {
            cartridge_->write_ram(address, value);
        }
        return;
    }
    if (in_vram(address)) {
        ppu_.write_vram(address, value);
        return;
    }
    address = resolve_echo(address);
    if (in_oam(address)) {
        if (!dma_.oam_blocked()) {
            ppu_.write_oam(address, value);
        }
        return;
    }
    if (in_unusable_region(address) || is_unmapped_io(address)) {
        return;
    }
    if (Ppu::is_register(address)) {
        interrupt_flag_ |= ppu_.write(address, value);
        return;
    }
    if (Apu::is_register(address)) {
        apu_.write(address, value);
        return;
    }
    switch (address) {
        case Joypad::kRegister:
            interrupt_flag_ |= joypad_.write(value);
            break;
        case Serial::kData:
        case Serial::kControl:
            serial_.write(address, value);
            break;
        case Timer::kDiv: {
            // Resetting DIV can make bit 4 fall, which clocks the frame sequencer.
            const bool before = timer_.frame_sequencer_bit();
            timer_.write(address, value);
            if (before && !timer_.frame_sequencer_bit()) {
                apu_.step_frame_sequencer();
            }
            break;
        }
        case Timer::kTima:
        case Timer::kTma:
        case Timer::kTac:
            timer_.write(address, value);
            break;
        case OamDma::kRegister:
            dma_.write(value);
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
    dma_ticks_ += t_cycles;
    while (dma_ticks_ >= kTicksPerMachineCycle) {
        dma_ticks_ -= kTicksPerMachineCycle;
        step_dma();
    }
    interrupt_flag_ |= ppu_.tick(t_cycles);
    const bool frame_sequencer_bit = timer_.frame_sequencer_bit();
    if (timer_.tick(t_cycles)) {
        request_interrupt(Interrupt::Timer);
    }
    if (frame_sequencer_bit && !timer_.frame_sequencer_bit()) {
        apu_.step_frame_sequencer();
    }
    apu_.tick(t_cycles);
    if (serial_.tick(t_cycles)) {
        request_interrupt(Interrupt::Serial);
    }
    if (cartridge_) {
        cartridge_->tick(t_cycles);
    }
}

void Bus::set_button(Joypad::Button button, bool pressed) noexcept {
    interrupt_flag_ |= joypad_.set_pressed(button, pressed);
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

std::uint8_t Bus::dma_read(std::uint16_t address) const noexcept {
    if (address >= kEchoStart) {
        address = static_cast<std::uint16_t>(address - kEchoOffset);
    }
    if (address < kRomRegionSize) {
        return cartridge_ ? cartridge_->read_rom(address) : kOpenBus;
    }
    if (in_cartridge_ram(address)) {
        return cartridge_ ? cartridge_->read_ram(address) : kOpenBus;
    }
    if (in_vram(address)) {
        return ppu_.dma_read_vram(address);
    }
    return memory_[address];
}

void Bus::step_dma() noexcept {
    if (const auto source = dma_.step()) {
        ppu_.dma_write_oam(static_cast<std::uint8_t>(*source & 0xFFU), dma_read(*source));
    }
}

bool Bus::interrupt_requested(Interrupt source) const noexcept {
    return (interrupt_flag_ & static_cast<std::uint8_t>(source)) != 0;
}

std::uint8_t Bus::pending_interrupts() const noexcept {
    return interrupt_flag_ & interrupt_enable_ & kInterruptBits;
}

const Ppu::Frame& Bus::frame() const noexcept {
    return ppu_.frame();
}

std::uint64_t Bus::frames_completed() const noexcept {
    return ppu_.frames_completed();
}

const std::string& Bus::serial_output() const noexcept {
    return serial_.output();
}

namespace {

constexpr std::uint16_t kWorkRamStart = 0xC000;
constexpr std::size_t kWorkRamSize = 0x2000;
constexpr std::size_t kIoSize = 0x80;
constexpr std::uint16_t kHighRamStart = 0xFF80;
constexpr std::size_t kHighRamSize = 0x7F;

}  // namespace

void Bus::save_state(StateWriter& out) const {
    const std::span<const std::uint8_t> memory(memory_);
    out.begin_section("BUS ");
    out.bytes(memory.subspan(kWorkRamStart, kWorkRamSize));
    out.bytes(memory.subspan(kIoStart, kIoSize));
    out.bytes(memory.subspan(kHighRamStart, kHighRamSize));
    out.u8(interrupt_flag_);
    out.u8(interrupt_enable_);
    out.u64(cycles_);
    out.u32(dma_ticks_);
    out.end_section();

    out.begin_section("TIMR");
    timer_.save_state(out);
    out.end_section();
    out.begin_section("SERL");
    serial_.save_state(out);
    out.end_section();
    out.begin_section("DMA ");
    dma_.save_state(out);
    out.end_section();
    out.begin_section("JOYP");
    joypad_.save_state(out);
    out.end_section();
    out.begin_section("PPU ");
    ppu_.save_state(out);
    out.end_section();
    out.begin_section("CART");
    if (cartridge_) {
        cartridge_->save_state(out);
    }
    out.end_section();
}

void Bus::load_state(StateReader& in) {
    const std::span<std::uint8_t> memory(memory_);
    in.begin_section("BUS ");
    in.bytes(memory.subspan(kWorkRamStart, kWorkRamSize));
    in.bytes(memory.subspan(kIoStart, kIoSize));
    in.bytes(memory.subspan(kHighRamStart, kHighRamSize));
    interrupt_flag_ = in.u8() & kInterruptBits;
    interrupt_enable_ = in.u8();
    cycles_ = in.u64();
    dma_ticks_ = in.at_most(in.u32(), kTicksPerMachineCycle - 1);
    in.end_section();

    in.begin_section("TIMR");
    timer_.load_state(in);
    in.end_section();
    in.begin_section("SERL");
    serial_.load_state(in);
    in.end_section();
    in.begin_section("DMA ");
    dma_.load_state(in);
    in.end_section();
    in.begin_section("JOYP");
    joypad_.load_state(in);
    in.end_section();
    in.begin_section("PPU ");
    ppu_.load_state(in);
    in.end_section();
    in.begin_section("CART");
    if (cartridge_) {
        cartridge_->load_state(in);
    }
    in.end_section();
}

}  // namespace core
