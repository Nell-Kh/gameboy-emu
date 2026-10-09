#include "core/cpu.h"

#include <bit>

namespace core {

namespace {

constexpr std::uint8_t low_nibble(std::uint8_t value) noexcept {
    return value & 0x0FU;
}

}  // namespace

Cpu::Cpu(Bus& bus) noexcept : bus_(bus) {}

std::uint32_t Cpu::step() {
    const std::uint64_t start = bus_.cycles();
    run_one_step();
    return static_cast<std::uint32_t>(bus_.cycles() - start);
}

void Cpu::run_one_step() {
    if (locked_) {
        internal_cycle();
        return;
    }
    if (stopped_) {
        if (!bus_.interrupt_requested(Interrupt::Joypad)) {
            internal_cycle();
            return;
        }
        stopped_ = false;
    }
    if (halted_) {
        // HALT ends as soon as an enabled interrupt is requested, even if IME
        // is off. With IME off the CPU simply carries on after the HALT.
        if (bus_.pending_interrupts() == 0) {
            internal_cycle();
            return;
        }
        halted_ = false;
    }
    if (ime_ == Ime::Enabled && bus_.pending_interrupts() != 0) {
        service_interrupt();
        return;
    }

    // An EI from the previous instruction takes effect after this one.
    const bool enable_after = ime_ == Ime::Pending;

    const std::uint8_t opcode = read8(reg_.pc);
    if (repeat_next_byte_) {
        repeat_next_byte_ = false;
    } else {
        ++reg_.pc;
    }
    execute(opcode);

    // Unless that instruction was DI, which cancels it.
    if (enable_after && ime_ == Ime::Pending) {
        ime_ = Ime::Enabled;
    }
}

// Five machine cycles: two internal, two to push PC, one to load the new PC.
void Cpu::service_interrupt() {
    ime_ = Ime::Disabled;
    internal_cycle();
    internal_cycle();
    push16(reg_.pc);

    // The source is chosen only now. If pushing PC happened to overwrite IE
    // (the stack can point at 0xFFFF) and nothing is pending any more, the
    // hardware jumps to address 0 instead.
    const std::uint8_t pending = bus_.pending_interrupts();
    if (pending == 0) {
        reg_.pc = 0x0000;
    } else {
        // Lowest set bit = highest priority. Handlers are 8 bytes apart,
        // starting at 0x0040.
        const int index = std::countr_zero(pending);
        bus_.acknowledge_interrupt(static_cast<Interrupt>(1U << static_cast<unsigned>(index)));
        reg_.pc = static_cast<std::uint16_t>(0x0040 + 8 * index);
    }
    internal_cycle();
}

Registers& Cpu::registers() noexcept {
    return reg_;
}

const Registers& Cpu::registers() const noexcept {
    return reg_;
}

bool Cpu::locked() const noexcept {
    return locked_;
}

bool Cpu::halted() const noexcept {
    return halted_;
}

bool Cpu::stopped() const noexcept {
    return stopped_;
}

bool Cpu::interrupts_enabled() const noexcept {
    return ime_ == Ime::Enabled;
}

// --- Bus access ---

std::uint8_t Cpu::read8(std::uint16_t address) noexcept {
    bus_.tick(kTicksPerMachineCycle);
    return bus_.read8(address);
}

void Cpu::write8(std::uint16_t address, std::uint8_t value) {
    bus_.tick(kTicksPerMachineCycle);
    bus_.write8(address, value);
}

void Cpu::internal_cycle() noexcept {
    bus_.tick(kTicksPerMachineCycle);
}

std::uint8_t Cpu::fetch8() noexcept {
    return read8(reg_.pc++);
}

std::uint16_t Cpu::fetch16() noexcept {
    const std::uint8_t lo = fetch8();
    const std::uint8_t hi = fetch8();
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

// --- Helpers ---

void Cpu::set_flags(bool zero, bool subtract, bool half_carry, bool carry) noexcept {
    reg_.set_flag(Flag::Z, zero);
    reg_.set_flag(Flag::N, subtract);
    reg_.set_flag(Flag::H, half_carry);
    reg_.set_flag(Flag::C, carry);
}

std::uint16_t Cpu::hl_post_increment() noexcept {
    const std::uint16_t address = reg_.hl();
    reg_.set_hl(static_cast<std::uint16_t>(address + 1));
    return address;
}

std::uint16_t Cpu::hl_post_decrement() noexcept {
    const std::uint16_t address = reg_.hl();
    reg_.set_hl(static_cast<std::uint16_t>(address - 1));
    return address;
}

std::uint16_t Cpu::high_page(std::uint8_t offset) noexcept {
    return static_cast<std::uint16_t>(0xFF00U | offset);
}

// --- 8-bit arithmetic and logic ---
//
// Half carry (H) is the carry between the low and high 4-bit halves of a byte.
// It exists only so that DAA can fix up decimal arithmetic afterwards.

void Cpu::alu_add(std::uint8_t value) noexcept {
    const unsigned sum = reg_.a + value;
    const bool half = low_nibble(reg_.a) + low_nibble(value) > 0x0F;
    reg_.a = static_cast<std::uint8_t>(sum);
    set_flags(reg_.a == 0, false, half, sum > 0xFF);
}

void Cpu::alu_adc(std::uint8_t value) noexcept {
    const unsigned carry_in = reg_.flag(Flag::C) ? 1 : 0;
    const unsigned sum = reg_.a + value + carry_in;
    const bool half = low_nibble(reg_.a) + low_nibble(value) + carry_in > 0x0F;
    reg_.a = static_cast<std::uint8_t>(sum);
    set_flags(reg_.a == 0, false, half, sum > 0xFF);
}

std::uint8_t Cpu::subtract(std::uint8_t value, std::uint8_t carry_in) noexcept {
    const int difference = reg_.a - value - carry_in;
    const bool half = low_nibble(reg_.a) < low_nibble(value) + carry_in;
    const auto result = static_cast<std::uint8_t>(difference);
    set_flags(result == 0, true, half, difference < 0);
    return result;
}

void Cpu::alu_sub(std::uint8_t value) noexcept {
    reg_.a = subtract(value, 0);
}

void Cpu::alu_sbc(std::uint8_t value) noexcept {
    reg_.a = subtract(value, reg_.flag(Flag::C) ? 1 : 0);
}

// CP is SUB that throws the result away and keeps only the flags.
void Cpu::alu_cp(std::uint8_t value) noexcept {
    static_cast<void>(subtract(value, 0));
}

void Cpu::alu_and(std::uint8_t value) noexcept {
    reg_.a &= value;
    set_flags(reg_.a == 0, false, true, false);
}

void Cpu::alu_xor(std::uint8_t value) noexcept {
    reg_.a ^= value;
    set_flags(reg_.a == 0, false, false, false);
}

void Cpu::alu_or(std::uint8_t value) noexcept {
    reg_.a |= value;
    set_flags(reg_.a == 0, false, false, false);
}

// INC and DEC leave the carry flag alone.
std::uint8_t Cpu::alu_inc(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>(value + 1);
    set_flags(result == 0, false, low_nibble(value) == 0x0F, reg_.flag(Flag::C));
    return result;
}

std::uint8_t Cpu::alu_dec(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>(value - 1);
    set_flags(result == 0, true, low_nibble(value) == 0x00, reg_.flag(Flag::C));
    return result;
}

// Decimal adjust: after adding or subtracting two binary-coded-decimal bytes
// (0x15 + 0x27 = 0x3C), turns A back into valid BCD (0x42). It uses N to know
// which operation came before, and H and C to know which digits overflowed.
void Cpu::daa() noexcept {
    const bool subtracted = reg_.flag(Flag::N);
    bool carry = reg_.flag(Flag::C);
    std::uint8_t adjust = 0;
    if (reg_.flag(Flag::H) || (!subtracted && low_nibble(reg_.a) > 0x09)) {
        adjust |= 0x06U;
    }
    if (carry || (!subtracted && reg_.a > 0x99)) {
        adjust |= 0x60U;
        carry = true;
    }
    reg_.a = static_cast<std::uint8_t>(subtracted ? reg_.a - adjust : reg_.a + adjust);
    set_flags(reg_.a == 0, subtracted, false, carry);
}

void Cpu::cpl() noexcept {
    reg_.a = static_cast<std::uint8_t>(~reg_.a);
    reg_.set_flag(Flag::N, true);
    reg_.set_flag(Flag::H, true);
}

void Cpu::scf() noexcept {
    set_flags(reg_.flag(Flag::Z), false, false, true);
}

void Cpu::ccf() noexcept {
    set_flags(reg_.flag(Flag::Z), false, false, !reg_.flag(Flag::C));
}

// --- 16-bit arithmetic ---

// 16-bit INC and DEC take an extra cycle and touch no flags.
std::uint16_t Cpu::inc16(std::uint16_t value) noexcept {
    internal_cycle();
    return static_cast<std::uint16_t>(value + 1);
}

std::uint16_t Cpu::dec16(std::uint16_t value) noexcept {
    internal_cycle();
    return static_cast<std::uint16_t>(value - 1);
}

// Here H and C are the carries out of bit 11 and bit 15. Z is left alone.
void Cpu::alu_add_hl(std::uint16_t value) noexcept {
    const std::uint16_t hl = reg_.hl();
    const unsigned sum = hl + value;
    const bool half = (hl & 0x0FFFU) + (value & 0x0FFFU) > 0x0FFF;
    reg_.set_hl(static_cast<std::uint16_t>(sum));
    set_flags(reg_.flag(Flag::Z), false, half, sum > 0xFFFF);
    internal_cycle();
}

// The offset is signed, but H and C come from adding it as an unsigned byte
// to the low byte of SP.
std::uint16_t Cpu::sp_plus_offset() noexcept {
    const std::uint8_t raw = fetch8();
    const bool half = (reg_.sp & 0x0FU) + low_nibble(raw) > 0x0F;
    const bool carry = (reg_.sp & 0xFFU) + raw > 0xFF;
    set_flags(false, false, half, carry);
    return static_cast<std::uint16_t>(reg_.sp + static_cast<std::int8_t>(raw));
}

void Cpu::ld_a16_sp() {
    const std::uint16_t address = fetch16();
    write8(address, static_cast<std::uint8_t>(reg_.sp & 0xFFU));
    write8(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(reg_.sp >> 8U));
}

// --- Rotates and shifts ---
//
// "Through carry" (RL, RR) means the old carry flag is shifted in, making a
// 9-bit rotation. The "circular" forms (RLC, RRC) rotate the 8 bits alone.

void Cpu::rlca() noexcept {
    reg_.a = alu_rlc(reg_.a);
    reg_.set_flag(Flag::Z, false);
}

void Cpu::rrca() noexcept {
    reg_.a = alu_rrc(reg_.a);
    reg_.set_flag(Flag::Z, false);
}

void Cpu::rla() noexcept {
    reg_.a = alu_rl(reg_.a);
    reg_.set_flag(Flag::Z, false);
}

void Cpu::rra() noexcept {
    reg_.a = alu_rr(reg_.a);
    reg_.set_flag(Flag::Z, false);
}

std::uint8_t Cpu::alu_rlc(std::uint8_t value) noexcept {
    const unsigned out = value >> 7U;
    const auto result = static_cast<std::uint8_t>((value << 1U) | out);
    set_flags(result == 0, false, false, out != 0);
    return result;
}

std::uint8_t Cpu::alu_rrc(std::uint8_t value) noexcept {
    const unsigned out = value & 1U;
    const auto result = static_cast<std::uint8_t>((value >> 1U) | (out << 7U));
    set_flags(result == 0, false, false, out != 0);
    return result;
}

std::uint8_t Cpu::alu_rl(std::uint8_t value) noexcept {
    const unsigned in = reg_.flag(Flag::C) ? 1 : 0;
    const auto result = static_cast<std::uint8_t>((value << 1U) | in);
    set_flags(result == 0, false, false, (value & 0x80U) != 0);
    return result;
}

std::uint8_t Cpu::alu_rr(std::uint8_t value) noexcept {
    const unsigned in = reg_.flag(Flag::C) ? 1 : 0;
    const auto result = static_cast<std::uint8_t>((value >> 1U) | (in << 7U));
    set_flags(result == 0, false, false, (value & 1U) != 0);
    return result;
}

std::uint8_t Cpu::alu_sla(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>(value << 1U);
    set_flags(result == 0, false, false, (value & 0x80U) != 0);
    return result;
}

// Arithmetic shift right: bit 7 (the sign) stays where it is.
std::uint8_t Cpu::alu_sra(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>((value >> 1U) | (value & 0x80U));
    set_flags(result == 0, false, false, (value & 1U) != 0);
    return result;
}

std::uint8_t Cpu::alu_swap(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>((value << 4U) | (value >> 4U));
    set_flags(result == 0, false, false, false);
    return result;
}

std::uint8_t Cpu::alu_srl(std::uint8_t value) noexcept {
    const auto result = static_cast<std::uint8_t>(value >> 1U);
    set_flags(result == 0, false, false, (value & 1U) != 0);
    return result;
}

// --- Single-bit operations ---

// BIT sets Z when the tested bit is 0.
void Cpu::alu_bit(unsigned bit, std::uint8_t value) noexcept {
    set_flags(((value >> bit) & 1U) == 0, false, true, reg_.flag(Flag::C));
}

std::uint8_t Cpu::res_bit(unsigned bit, std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>(value & ~(1U << bit));
}

std::uint8_t Cpu::set_bit(unsigned bit, std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>(value | (1U << bit));
}

// --- Stack ---
//
// The stack grows downwards: push moves SP down, then writes. The high byte
// goes in first, so the low byte ends up at the lower address.

void Cpu::push16(std::uint16_t value) {
    write8(--reg_.sp, static_cast<std::uint8_t>(value >> 8U));
    write8(--reg_.sp, static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint16_t Cpu::pop16() noexcept {
    const std::uint8_t lo = read8(reg_.sp++);
    const std::uint8_t hi = read8(reg_.sp++);
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

void Cpu::push(std::uint16_t value) {
    internal_cycle();
    push16(value);
}

// --- Control flow ---
//
// A conditional jump that is not taken still reads its operand, but skips the
// rest. That is why these instructions have two durations.

void Cpu::jp(bool condition) noexcept {
    const std::uint16_t target = fetch16();
    if (condition) {
        internal_cycle();
        reg_.pc = target;
    }
}

void Cpu::jr(bool condition) noexcept {
    const auto offset = static_cast<std::int8_t>(fetch8());
    if (condition) {
        internal_cycle();
        reg_.pc = static_cast<std::uint16_t>(reg_.pc + offset);
    }
}

void Cpu::call(bool condition) {
    const std::uint16_t target = fetch16();
    if (condition) {
        internal_cycle();
        push16(reg_.pc);
        reg_.pc = target;
    }
}

void Cpu::ret() noexcept {
    reg_.pc = pop16();
    internal_cycle();
}

void Cpu::ret_if(bool condition) noexcept {
    internal_cycle();
    if (condition) {
        ret();
    }
}

// Unlike EI, RETI enables interrupts immediately.
void Cpu::reti() noexcept {
    ret();
    ime_ = Ime::Enabled;
}

void Cpu::rst(std::uint16_t vector) {
    internal_cycle();
    push16(reg_.pc);
    reg_.pc = vector;
}

// --- CPU control ---

void Cpu::halt() noexcept {
    if (ime_ == Ime::Disabled && bus_.pending_interrupts() != 0) {
        // The HALT bug: with IME off and an interrupt already pending, the CPU
        // does not halt, and it fails to advance PC on the next fetch. The byte
        // after HALT is therefore read twice.
        repeat_next_byte_ = true;
    } else {
        halted_ = true;
    }
}

// STOP is two bytes long; the second one is ignored. It also resets DIV.
// The CPU then sleeps until a button is pressed.
//
// Known simplification: on hardware the clock itself stops, so the timer
// freezes too. Here time keeps passing while stopped.
void Cpu::stop() {
    ++reg_.pc;
    bus_.write8(Timer::kDiv, 0x00);
    stopped_ = true;
}

void Cpu::di() noexcept {
    ime_ = Ime::Disabled;
}

void Cpu::ei() noexcept {
    if (ime_ == Ime::Disabled) {
        ime_ = Ime::Pending;
    }
}

}  // namespace core
