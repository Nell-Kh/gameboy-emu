#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/registers.h"
#include "test_rom.h"

namespace test {

// A bus and a CPU with `program` loaded at the cartridge entry point (0x0100).
// The registers start in the post-boot state, so tests set what they need.
struct Machine {
    core::Bus bus;
    core::Cpu cpu{bus};

    Machine(std::initializer_list<std::uint8_t> program) {
        bus.load_rom(make_rom(program));
    }

    [[nodiscard]] core::Registers& reg() noexcept {
        return cpu.registers();
    }

    std::uint32_t step() {
        return cpu.step();
    }

    // Runs `count` instructions and returns the total ticks.
    std::uint32_t run(int count) {
        std::uint32_t ticks = 0;
        for (int i = 0; i < count; ++i) {
            ticks += cpu.step();
        }
        return ticks;
    }

    // The flags as a 4-character string in Z N H C order, e.g. "Z-HC".
    [[nodiscard]] std::string flags() {
        const core::Registers& r = cpu.registers();
        std::string text = "----";
        if (r.flag(core::Flag::Z)) {
            text[0] = 'Z';
        }
        if (r.flag(core::Flag::N)) {
            text[1] = 'N';
        }
        if (r.flag(core::Flag::H)) {
            text[2] = 'H';
        }
        if (r.flag(core::Flag::C)) {
            text[3] = 'C';
        }
        return text;
    }
};

}  // namespace test
