#include "core/bus.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using core::Bus;

TEST(Bus, StartsZeroed) {
    const Bus bus;
    EXPECT_EQ(bus.read8(0x0000), 0x00);
    EXPECT_EQ(bus.read8(0x8000), 0x00);
    EXPECT_EQ(bus.read8(0xFFFF), 0x00);
}

TEST(Bus, ReadsBackWhatWasWritten) {
    Bus bus;
    bus.write8(0xC000, 0x42);
    EXPECT_EQ(bus.read8(0xC000), 0x42);
    bus.write8(0xC000, 0x99);
    EXPECT_EQ(bus.read8(0xC000), 0x99);
}

TEST(Bus, FirstAndLastAddressAreUsable) {
    Bus bus;
    bus.write8(0x0000, 0x11);
    bus.write8(0xFFFF, 0x22);
    EXPECT_EQ(bus.read8(0x0000), 0x11);
    EXPECT_EQ(bus.read8(0xFFFF), 0x22);
}

TEST(Bus, WriteDoesNotTouchNeighbours) {
    Bus bus;
    bus.write8(0x1234, 0xAB);
    EXPECT_EQ(bus.read8(0x1233), 0x00);
    EXPECT_EQ(bus.read8(0x1235), 0x00);
}

TEST(Bus, LoadCopiesBytesInOrder) {
    Bus bus;
    const std::array<std::uint8_t, 3> program = {0x3E, 0x05, 0x76};
    ASSERT_TRUE(bus.load(program, 0x0100));
    EXPECT_EQ(bus.read8(0x00FF), 0x00);
    EXPECT_EQ(bus.read8(0x0100), 0x3E);
    EXPECT_EQ(bus.read8(0x0101), 0x05);
    EXPECT_EQ(bus.read8(0x0102), 0x76);
    EXPECT_EQ(bus.read8(0x0103), 0x00);
}

TEST(Bus, LoadDefaultsToAddressZero) {
    Bus bus;
    const std::array<std::uint8_t, 2> bytes = {0xAA, 0xBB};
    ASSERT_TRUE(bus.load(bytes));
    EXPECT_EQ(bus.read8(0x0000), 0xAA);
    EXPECT_EQ(bus.read8(0x0001), 0xBB);
}

TEST(Bus, LoadCanFillExactlyToTheEnd) {
    Bus bus;
    const std::array<std::uint8_t, 2> bytes = {0xAA, 0xBB};
    ASSERT_TRUE(bus.load(bytes, 0xFFFE));
    EXPECT_EQ(bus.read8(0xFFFE), 0xAA);
    EXPECT_EQ(bus.read8(0xFFFF), 0xBB);
}

TEST(Bus, LoadCanFillTheWholeAddressSpace) {
    Bus bus;
    const std::vector<std::uint8_t> image(Bus::kAddressSpace, 0x5A);
    ASSERT_TRUE(bus.load(image));
    EXPECT_EQ(bus.read8(0x0000), 0x5A);
    EXPECT_EQ(bus.read8(0xFFFF), 0x5A);
}

TEST(Bus, LoadPastTheEndIsRejectedAndChangesNothing) {
    Bus bus;
    const std::array<std::uint8_t, 3> bytes = {0xAA, 0xBB, 0xCC};
    EXPECT_FALSE(bus.load(bytes, 0xFFFE));
    EXPECT_EQ(bus.read8(0xFFFE), 0x00);
    EXPECT_EQ(bus.read8(0xFFFF), 0x00);
}

TEST(Bus, LoadingNothingSucceeds) {
    Bus bus;
    EXPECT_TRUE(bus.load({}, 0xFFFF));
    EXPECT_EQ(bus.read8(0xFFFF), 0x00);
}

TEST(Bus, TickAccumulatesCycles) {
    Bus bus;
    EXPECT_EQ(bus.cycles(), 0U);
    bus.tick(4);
    bus.tick(4);
    EXPECT_EQ(bus.cycles(), 8U);
}

TEST(Bus, ReadingAndWritingDoNotAdvanceTime) {
    Bus bus;
    bus.write8(0xC000, 0x01);
    EXPECT_EQ(bus.read8(0xC000), 0x01);
    EXPECT_EQ(bus.cycles(), 0U);
}

}  // namespace
