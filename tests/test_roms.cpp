// Integration tests: run Blargg's CPU test ROMs on the whole machine.
//
// Each ROM exercises one group of instructions on real-hardware-verified
// expectations and prints "Passed" or "Failed" over the serial port. Nothing
// here knows what the right answers are; the ROM does.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

#include "core/gameboy.h"

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

}  // namespace
