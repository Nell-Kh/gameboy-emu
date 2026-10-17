#include "app/ram_verdict.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using frontend::read_ram_verdict;

std::vector<std::uint8_t> ram_with(std::uint8_t status, const std::string& text,
                                   bool signature = true) {
    std::vector<std::uint8_t> ram(0x2000, 0x00);
    ram[0] = status;
    if (signature) {
        ram[1] = 0xDE;
        ram[2] = 0xB0;
        ram[3] = 0x61;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        ram[4 + i] = static_cast<std::uint8_t>(text[i]);
    }
    return ram;
}

TEST(RamVerdict, APassHasStatusZeroAndTheText) {
    const auto verdict = read_ram_verdict(ram_with(0x00, "01-registers\n\nPassed\n"));
    ASSERT_TRUE(verdict.has_value());
    const frontend::RamVerdict result =
        verdict.value_or(frontend::RamVerdict{.status = 0xFF, .text = {}});
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.text, "01-registers\n\nPassed\n");
}

TEST(RamVerdict, AFailureHasTheFailingTestNumber) {
    const auto verdict = read_ram_verdict(ram_with(0x03, "Failed #3\n"));
    ASSERT_TRUE(verdict.has_value());
    const frontend::RamVerdict result = verdict.value_or(frontend::RamVerdict{});
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.status, 3);
}

TEST(RamVerdict, NothingWhileRunning) {
    EXPECT_FALSE(read_ram_verdict(ram_with(0x80, "")).has_value());
}

// Without the signature the status byte is just a RAM byte: a game's save
// RAM full of zeros must not read as a pass.
TEST(RamVerdict, NothingWithoutTheSignature) {
    EXPECT_FALSE(read_ram_verdict(ram_with(0x00, "Passed", false)).has_value());
    auto almost = ram_with(0x00, "Passed");
    almost[3] = 0x62;
    EXPECT_FALSE(read_ram_verdict(almost).has_value());
}

TEST(RamVerdict, NothingWithNoOrTooLittleRam) {
    EXPECT_FALSE(read_ram_verdict({}).has_value());
    EXPECT_FALSE(read_ram_verdict(std::vector<std::uint8_t>{0x00, 0xDE, 0xB0, 0x61}).has_value());
}

}  // namespace
