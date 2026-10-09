// Integration tests: run hardware test ROMs on the whole machine.
//
// Each ROM checks one area against expectations verified on real hardware and
// reports the verdict itself: Blargg's over the serial port, Mooneye's in the
// CPU registers. Nothing here knows what the right answers are; the ROM does.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "core/registers.h"

namespace {

// The slowest of these ROMs needs about 18 emulated seconds.
constexpr std::uint64_t kMaxTicks = 60 * core::GameBoy::kTicksPerSecond;
// Check for a verdict about 60 times per emulated second.
constexpr std::uint64_t kTicksPerCheck = core::GameBoy::kTicksPerSecond / 60;

struct Rom {
    const char* test_name;  // letters, digits and underscores only
    const char* path;       // relative to the gb-test-roms checkout
};

// Tells GoogleTest how to print a case in the test listing.
void PrintTo(const Rom& rom, std::ostream* os) {
    *os << rom.path;
}

std::vector<std::uint8_t> read_rom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class BlarggRom : public testing::TestWithParam<Rom> {};

TEST_P(BlarggRom, PrintsPassed) {
    const std::string path = std::string(GB_TEST_ROM_DIR) + "/" + GetParam().path;
    const std::vector<std::uint8_t> rom = read_rom(path);
    ASSERT_FALSE(rom.empty()) << "cannot read " << path;

    core::GameBoy gb;
    gb.load_rom(rom);

    const std::string& serial = gb.serial_output();
    bool passed = false;
    bool failed = false;
    while (!passed && !failed && gb.cycles() < kMaxTicks && !gb.cpu().locked()) {
        gb.run_for(kTicksPerCheck);
        passed = serial.find("Passed") != std::string::npos;
        failed = serial.find("Failed") != std::string::npos;
    }

    EXPECT_FALSE(gb.cpu().locked()) << "the CPU hit an undefined opcode";
    EXPECT_TRUE(passed) << "serial output after " << gb.cycles() << " ticks:\n" << serial;
}

INSTANTIATE_TEST_SUITE_P(
    CpuInstrs, BlarggRom,
    testing::Values(Rom{"01_special", "cpu_instrs/individual/01-special.gb"},
                    Rom{"02_interrupts", "cpu_instrs/individual/02-interrupts.gb"},
                    Rom{"03_op_sp_hl", "cpu_instrs/individual/03-op sp,hl.gb"},
                    Rom{"04_op_r_imm", "cpu_instrs/individual/04-op r,imm.gb"},
                    Rom{"05_op_rp", "cpu_instrs/individual/05-op rp.gb"},
                    Rom{"06_ld_r_r", "cpu_instrs/individual/06-ld r,r.gb"},
                    Rom{"07_jr_jp_call_ret_rst", "cpu_instrs/individual/07-jr,jp,call,ret,rst.gb"},
                    Rom{"08_misc_instrs", "cpu_instrs/individual/08-misc instrs.gb"},
                    Rom{"09_op_r_r", "cpu_instrs/individual/09-op r,r.gb"},
                    Rom{"10_bit_ops", "cpu_instrs/individual/10-bit ops.gb"},
                    Rom{"11_op_a_hl", "cpu_instrs/individual/11-op a,(hl).gb"}),
    [](const testing::TestParamInfo<Rom>& info) { return std::string(info.param.test_name); });

// instr_timing measures how long every instruction takes, using the timer.
// mem_timing checks in which machine cycle of an instruction each memory read
// and write happens. Both depend on the CPU ticking the bus per access (ADR-005).
INSTANTIATE_TEST_SUITE_P(
    Timing, BlarggRom,
    testing::Values(Rom{"instr_timing", "instr_timing/instr_timing.gb"},
                    Rom{"mem_timing_01_read", "mem_timing/individual/01-read_timing.gb"},
                    Rom{"mem_timing_02_write", "mem_timing/individual/02-write_timing.gb"},
                    Rom{"mem_timing_03_modify", "mem_timing/individual/03-modify_timing.gb"}),
    [](const testing::TestParamInfo<Rom>& info) { return std::string(info.param.test_name); });

// --- Mooneye Test Suite -------------------------------------------------
//
// A Mooneye test ends by executing LD B, B (opcode 0x40), used as a breakpoint.
// It passed if the registers then hold the Fibonacci numbers 3, 5, 8, 13, 21,
// 34 in B, C, D, E, H, L. Mooneye allows each test 120 emulated seconds.

constexpr std::uint8_t kMooneyeBreakpoint = 0x40;
constexpr std::uint64_t kMooneyeMaxTicks = 120 * core::GameBoy::kTicksPerSecond;

class MooneyeRom : public testing::TestWithParam<Rom> {};

TEST_P(MooneyeRom, EndsWithTheFibonacciRegisters) {
    const std::string path = std::string(GB_MOONEYE_DIR) + "/" + GetParam().path;
    const std::vector<std::uint8_t> rom = read_rom(path);
    ASSERT_FALSE(rom.empty()) << "cannot read " << path;

    core::GameBoy gb;
    gb.load_rom(rom);

    bool finished = false;
    while (!finished && gb.cycles() < kMooneyeMaxTicks && !gb.cpu().locked()) {
        const core::Registers& r = gb.cpu().registers();
        finished = !gb.cpu().halted() && gb.bus().read8(r.pc) == kMooneyeBreakpoint;
        if (!finished) {
            gb.step();
        }
    }

    const core::Registers& r = gb.cpu().registers();
    ASSERT_TRUE(finished) << "no breakpoint after " << gb.cycles() << " ticks"
                          << (gb.cpu().locked() ? " (CPU locked)" : "");
    EXPECT_EQ(r.b, 3);
    EXPECT_EQ(r.c, 5);
    EXPECT_EQ(r.d, 8);
    EXPECT_EQ(r.e, 13);
    EXPECT_EQ(r.h, 21);
    EXPECT_EQ(r.l, 34);
}

INSTANTIATE_TEST_SUITE_P(
    Timer, MooneyeRom,
    testing::Values(Rom{"div_write", "acceptance/timer/div_write.gb"},
                    Rom{"rapid_toggle", "acceptance/timer/rapid_toggle.gb"},
                    Rom{"tim00", "acceptance/timer/tim00.gb"},
                    Rom{"tim00_div_trigger", "acceptance/timer/tim00_div_trigger.gb"},
                    Rom{"tim01", "acceptance/timer/tim01.gb"},
                    Rom{"tim01_div_trigger", "acceptance/timer/tim01_div_trigger.gb"},
                    Rom{"tim10", "acceptance/timer/tim10.gb"},
                    Rom{"tim10_div_trigger", "acceptance/timer/tim10_div_trigger.gb"},
                    Rom{"tim11", "acceptance/timer/tim11.gb"},
                    Rom{"tim11_div_trigger", "acceptance/timer/tim11_div_trigger.gb"},
                    Rom{"tima_reload", "acceptance/timer/tima_reload.gb"},
                    Rom{"tima_write_reloading", "acceptance/timer/tima_write_reloading.gb"},
                    Rom{"tma_write_reloading", "acceptance/timer/tma_write_reloading.gb"}),
    [](const testing::TestParamInfo<Rom>& info) { return std::string(info.param.test_name); });

INSTANTIATE_TEST_SUITE_P(Interrupts, MooneyeRom,
                         testing::Values(Rom{"ei_sequence", "acceptance/ei_sequence.gb"},
                                         Rom{"ei_timing", "acceptance/ei_timing.gb"},
                                         Rom{"rapid_di_ei", "acceptance/rapid_di_ei.gb"},
                                         Rom{"intr_timing", "acceptance/intr_timing.gb"},
                                         Rom{"if_ie_registers", "acceptance/if_ie_registers.gb"},
                                         Rom{"ie_push", "acceptance/interrupts/ie_push.gb"}),
                         [](const testing::TestParamInfo<Rom>& info) {
                             return std::string(info.param.test_name);
                         });

INSTANTIATE_TEST_SUITE_P(OamDma, MooneyeRom,
                         testing::Values(Rom{"basic", "acceptance/oam_dma/basic.gb"},
                                         Rom{"reg_read", "acceptance/oam_dma/reg_read.gb"},
                                         Rom{"sources", "acceptance/oam_dma/sources-GS.gb"},
                                         Rom{"oam_dma_restart", "acceptance/oam_dma_restart.gb"},
                                         Rom{"oam_dma_start", "acceptance/oam_dma_start.gb"},
                                         Rom{"oam_dma_timing", "acceptance/oam_dma_timing.gb"}),
                         [](const testing::TestParamInfo<Rom>& info) {
                             return std::string(info.param.test_name);
                         });

// These measure when each memory access of an instruction happens, using an
// OAM DMA as the stopwatch.
INSTANTIATE_TEST_SUITE_P(
    InstructionTiming, MooneyeRom,
    testing::Values(Rom{"add_sp_e_timing", "acceptance/add_sp_e_timing.gb"},
                    Rom{"call_timing", "acceptance/call_timing.gb"},
                    Rom{"call_timing2", "acceptance/call_timing2.gb"},
                    Rom{"call_cc_timing", "acceptance/call_cc_timing.gb"},
                    Rom{"call_cc_timing2", "acceptance/call_cc_timing2.gb"},
                    Rom{"div_timing", "acceptance/div_timing.gb"},
                    Rom{"halt_ime1_timing", "acceptance/halt_ime1_timing.gb"},
                    Rom{"jp_timing", "acceptance/jp_timing.gb"},
                    Rom{"jp_cc_timing", "acceptance/jp_cc_timing.gb"},
                    Rom{"ld_hl_sp_e_timing", "acceptance/ld_hl_sp_e_timing.gb"},
                    Rom{"pop_timing", "acceptance/pop_timing.gb"},
                    Rom{"push_timing", "acceptance/push_timing.gb"},
                    Rom{"ret_timing", "acceptance/ret_timing.gb"},
                    Rom{"ret_cc_timing", "acceptance/ret_cc_timing.gb"},
                    Rom{"reti_timing", "acceptance/reti_timing.gb"},
                    Rom{"reti_intr_timing", "acceptance/reti_intr_timing.gb"},
                    Rom{"rst_timing", "acceptance/rst_timing.gb"}),
    [](const testing::TestParamInfo<Rom>& info) { return std::string(info.param.test_name); });

INSTANTIATE_TEST_SUITE_P(Cpu, MooneyeRom,
                         testing::Values(Rom{"boot_regs_dmgABC", "acceptance/boot_regs-dmgABC.gb"},
                                         Rom{"daa", "acceptance/instr/daa.gb"}),
                         [](const testing::TestParamInfo<Rom>& info) {
                             return std::string(info.param.test_name);
                         });

INSTANTIATE_TEST_SUITE_P(Bits, MooneyeRom,
                         testing::Values(Rom{"unused_hwio", "acceptance/bits/unused_hwio-GS.gb"},
                                         Rom{"mem_oam", "acceptance/bits/mem_oam.gb"},
                                         Rom{"reg_f", "acceptance/bits/reg_f.gb"}),
                         [](const testing::TestParamInfo<Rom>& info) {
                             return std::string(info.param.test_name);
                         });

}  // namespace
