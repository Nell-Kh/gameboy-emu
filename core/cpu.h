#pragma once

#include <cstdint>

#include "core/bus.h"
#include "core/registers.h"

namespace core {

// The Sharp SM83 CPU.
//
// Timing model (ADR-005): one machine cycle is 4 clock ticks (T-cycles), and
// the CPU does at most one memory access per machine cycle. So every memory
// access advances the rest of the machine by 4 ticks before it happens, and
// the other components always see the access at the right moment.
//
// The instruction decoder (execute and execute_cb) is generated from the
// opcode table by tools/gen_opcodes.py. Everything else is hand-written.
class Cpu {
public:
    static constexpr std::uint32_t kTicksPerMachineCycle = 4;

    explicit Cpu(Bus& bus) noexcept;

    // Executes one instruction and returns how many clock ticks it took.
    // The ticks have already been applied to the bus by the time this returns.
    std::uint32_t step();

    [[nodiscard]] Registers& registers() noexcept;
    [[nodiscard]] const Registers& registers() const noexcept;

    // True once the CPU has hit one of the 11 undefined opcodes. Real hardware
    // freezes on them; a locked CPU only burns time.
    [[nodiscard]] bool locked() const noexcept;

    // True while the CPU sleeps after HALT.
    [[nodiscard]] bool halted() const noexcept;

    // The interrupt master enable flag (IME), set by EI and cleared by DI.
    [[nodiscard]] bool interrupts_enabled() const noexcept;

private:
    // --- Bus access. Each of these is one machine cycle. ---
    [[nodiscard]] std::uint8_t read8(std::uint16_t address) noexcept;
    void write8(std::uint16_t address, std::uint8_t value);
    // A machine cycle with no memory access (the CPU is busy internally).
    void internal_cycle() noexcept;

    // Read the byte(s) at PC and move PC past them. 16-bit values are stored
    // low byte first.
    [[nodiscard]] std::uint8_t fetch8() noexcept;
    [[nodiscard]] std::uint16_t fetch16() noexcept;

    // --- The decoder (generated, in opcodes.gen.cpp). ---
    void execute(std::uint8_t opcode);
    void execute_cb(std::uint8_t opcode);

    // --- Helpers the generated decoder calls. ---
    void set_flags(bool zero, bool subtract, bool half_carry, bool carry) noexcept;

    // Addressing.
    [[nodiscard]] std::uint16_t hl_post_increment() noexcept;
    [[nodiscard]] std::uint16_t hl_post_decrement() noexcept;
    // 0xFF00 + offset: the page that holds the I/O registers and high RAM.
    [[nodiscard]] static std::uint16_t high_page(std::uint8_t offset) noexcept;

    // 8-bit arithmetic and logic on A.
    void alu_add(std::uint8_t value) noexcept;
    void alu_adc(std::uint8_t value) noexcept;
    void alu_sub(std::uint8_t value) noexcept;
    void alu_sbc(std::uint8_t value) noexcept;
    void alu_and(std::uint8_t value) noexcept;
    void alu_xor(std::uint8_t value) noexcept;
    void alu_or(std::uint8_t value) noexcept;
    void alu_cp(std::uint8_t value) noexcept;
    // A - value - carry_in, setting the flags. Shared by SUB, SBC and CP.
    [[nodiscard]] std::uint8_t subtract(std::uint8_t value, std::uint8_t carry_in) noexcept;
    [[nodiscard]] std::uint8_t alu_inc(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_dec(std::uint8_t value) noexcept;
    void daa() noexcept;
    void cpl() noexcept;
    void scf() noexcept;
    void ccf() noexcept;

    // 16-bit arithmetic.
    [[nodiscard]] std::uint16_t inc16(std::uint16_t value) noexcept;
    [[nodiscard]] std::uint16_t dec16(std::uint16_t value) noexcept;
    void alu_add_hl(std::uint16_t value) noexcept;
    // Fetches a signed offset and returns SP + offset (ADD SP, e8 / LD HL, SP+e8).
    [[nodiscard]] std::uint16_t sp_plus_offset() noexcept;
    void ld_a16_sp();

    // Rotates and shifts. The four one-byte forms work on A and always clear Z.
    void rlca() noexcept;
    void rrca() noexcept;
    void rla() noexcept;
    void rra() noexcept;
    [[nodiscard]] std::uint8_t alu_rlc(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_rrc(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_rl(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_rr(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_sla(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_sra(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_swap(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t alu_srl(std::uint8_t value) noexcept;

    // Single-bit operations.
    void alu_bit(unsigned bit, std::uint8_t value) noexcept;
    [[nodiscard]] static std::uint8_t res_bit(unsigned bit, std::uint8_t value) noexcept;
    [[nodiscard]] static std::uint8_t set_bit(unsigned bit, std::uint8_t value) noexcept;

    // Stack. push16/pop16 are the two memory accesses only; `push` adds the
    // extra internal cycle the PUSH instruction has.
    void push16(std::uint16_t value);
    [[nodiscard]] std::uint16_t pop16() noexcept;
    void push(std::uint16_t value);

    // Control flow. `condition` is true for the unconditional forms.
    void jp(bool condition) noexcept;
    void jr(bool condition) noexcept;
    void call(bool condition);
    void ret() noexcept;
    void ret_if(bool condition) noexcept;
    void reti() noexcept;
    void rst(std::uint16_t vector);

    // CPU control.
    void halt() noexcept;
    void stop() noexcept;
    void di() noexcept;
    void ei() noexcept;

    Bus& bus_;
    Registers reg_ = Registers::post_boot_dmg();
    bool ime_ = false;
    bool halted_ = false;
    bool locked_ = false;
};

}  // namespace core
