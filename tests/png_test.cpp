// The screenshot PNG writer (app/png.h), read back with stb_image, which the
// screenshot tests already use (ADR-015). It lives in rom_tests because stb
// is linked there.

#include "app/png.h"

#include <gtest/gtest.h>
#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// Decodes `png` and checks it is `width` x `height` RGB with exactly the
// colours of `argb`.
void expect_decodes_to(const std::vector<std::uint8_t>& png, int width, int height,
                       const std::vector<std::uint32_t>& argb) {
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 3);
    ASSERT_NE(pixels, nullptr) << stbi_failure_reason();
    EXPECT_EQ(w, width);
    EXPECT_EQ(h, height);
    EXPECT_EQ(channels, 3);
    for (std::size_t i = 0; i < argb.size(); ++i) {
        const std::uint32_t rgb = (std::uint32_t{pixels[3 * i]} << 16U) |
                                  (std::uint32_t{pixels[(3 * i) + 1]} << 8U) | pixels[(3 * i) + 2];
        ASSERT_EQ(rgb, argb[i] & 0xFFFFFFU) << "pixel " << i;
    }
    stbi_image_free(pixels);
}

TEST(Png, OnePixel) {
    const std::vector<std::uint32_t> argb = {0xFF123456};
    expect_decodes_to(frontend::encode_png(1, 1, argb), 1, 1, argb);
}

// 160x144 RGB is 69 264 bytes with the filter bytes: more than one stored
// deflate block (65 535 bytes at most).
TEST(Png, AScreenSizedImageSpansTwoDeflateBlocks) {
    std::vector<std::uint32_t> argb(std::size_t{160} * 144);
    for (std::size_t i = 0; i < argb.size(); ++i) {
        argb[i] = 0xFF000000U | static_cast<std::uint32_t>(i * 2654435761U >> 8U);
    }
    expect_decodes_to(frontend::encode_png(160, 144, argb), 160, 144, argb);
}

TEST(Png, ScreenshotsAreNumberedNextToTheRomWithoutReplacingOne) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "peasoup-png-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path rom = dir / "game.gb";
    EXPECT_EQ(frontend::screenshot_path_for(rom), dir / "game-1.png");
    std::ofstream(dir / "game-1.png") << "x";
    EXPECT_EQ(frontend::screenshot_path_for(rom), dir / "game-2.png");
    std::filesystem::remove_all(dir);
}

}  // namespace
