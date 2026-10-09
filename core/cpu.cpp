#include "core/cpu.h"

namespace core {

Cpu::Cpu(Bus& bus) noexcept : bus_(bus) {}

std::uint32_t Cpu::step() {
    const std::uint64_t start = bus_.cycles();
    if (locked_) {
        internal_cycle();
    } else {
        execute(fetch8());
    }
    return static_cast<std::uint32_t>(bus_.cycles() - start);
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

// Hand-written for now: four opcodes, one of each timing shape, to prove the
// model. The full table (all 500) is generated in the next step and replaces
// this switch.
void Cpu::execute(std::uint8_t opcode) {
    switch (opcode) {
        case 0x00:  // NOP                1 cycle:  fetch
            break;
        case 0x3E:  // LD A, n8           2 cycles: fetch, read operand
            reg_.a = fetch8();
            break;
        case 0x77:  // LD (HL), A         2 cycles: fetch, write
            write8(reg_.hl(), reg_.a);
            break;
        case 0xC3: {  // JP a16           4 cycles: fetch, read lo, read hi, internal
            const std::uint16_t target = fetch16();
            internal_cycle();
            reg_.pc = target;
            break;
        }
        default:
            locked_ = true;
            break;
    }
}

}  // namespace core
