#include "core/oam_dma.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace {

using core::OamDma;

TEST(OamDma, IdleControllerCopiesNothingAndDoesNotBlock) {
    OamDma dma;
    EXPECT_FALSE(dma.step().has_value());
    EXPECT_FALSE(dma.oam_blocked());
}

TEST(OamDma, RegisterReadsBackTheLastWrite) {
    OamDma dma;
    dma.write(0xC1);
    EXPECT_EQ(dma.read(), 0xC1);
}

TEST(OamDma, FirstCycleAfterTheWriteOnlyLatchesTheRequest) {
    OamDma dma;
    dma.write(0xC1);
    EXPECT_FALSE(dma.step().has_value());
    EXPECT_FALSE(dma.oam_blocked());
}

TEST(OamDma, CopiesOneByteASecondCycleOnwardsInOrder) {
    OamDma dma;
    dma.write(0xC1);
    static_cast<void>(dma.step());
    for (std::uint16_t i = 0; i < OamDma::kLength; ++i) {
        EXPECT_EQ(dma.step(), std::optional<std::uint16_t>(0xC100 + i)) << "byte " << i;
        EXPECT_TRUE(dma.oam_blocked());
    }
}

TEST(OamDma, BlocksOamForExactly160CyclesThenStops) {
    OamDma dma;
    dma.write(0xC1);
    int copies = 0;
    int blocked_cycles = 0;
    for (int cycle = 1; cycle <= 200; ++cycle) {
        copies += dma.step().has_value() ? 1 : 0;
        blocked_cycles += dma.oam_blocked() ? 1 : 0;
        if (cycle == 161) {
            EXPECT_TRUE(dma.oam_blocked());
        }
        if (cycle == 162) {
            EXPECT_FALSE(dma.oam_blocked());
        }
    }
    EXPECT_EQ(copies, 160);
    EXPECT_EQ(blocked_cycles, 160);
}

TEST(OamDma, RestartKeepsTheOldTransferRunningUntilTheNewOneTakesOver) {
    OamDma dma;
    dma.write(0xC1);
    for (int cycle = 0; cycle < 11; ++cycle) {
        static_cast<void>(dma.step());
    }
    // Ten bytes of 0xC1xx copied. Restart from 0xD2.
    dma.write(0xD2);
    EXPECT_EQ(dma.step(), std::optional<std::uint16_t>(0xC10A));
    EXPECT_EQ(dma.step(), std::optional<std::uint16_t>(0xD200));
    EXPECT_EQ(dma.step(), std::optional<std::uint16_t>(0xD201));
    EXPECT_TRUE(dma.oam_blocked());
}

}  // namespace
