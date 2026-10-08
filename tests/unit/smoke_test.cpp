#include <gtest/gtest.h>

#include "core/gameboy.h"

TEST(Smoke, CoreLinksAndReportsVersion) {
    EXPECT_FALSE(core::GameBoy::version().empty());
    EXPECT_EQ(core::GameBoy::kScreenWidth, 160);
    EXPECT_EQ(core::GameBoy::kScreenHeight, 144);
}
