#include "screenshot.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <span>
#include <system_error>

namespace test {

namespace {

using Frame = core::Ppu::Frame;
constexpr int kWidth = core::Ppu::kScreenWidth;
constexpr int kHeight = core::Ppu::kScreenHeight;

// The grey levels the reference screenshots use for shades 0-3 (c-sp's
// game-boy-test-roms how-to files).
constexpr std::array<std::uint8_t, 4> kGrey = {0xFF, 0xAA, 0x55, 0x00};

// Reads a 160x144 PNG that only uses the four grey levels into shades.
// Returns an error message, or an empty string on success.
std::string read_reference(const std::string& path, Frame& shades) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.c_str(), &width, &height, &channels, 1), &stbi_image_free);
    if (!pixels) {
        return "cannot read " + path + ": " + stbi_failure_reason();
    }
    if (width != kWidth || height != kHeight) {
        return path + " is " + std::to_string(width) + "x" + std::to_string(height);
    }
    const std::span<const stbi_uc> grey(pixels.get(), shades.size());
    for (std::size_t i = 0; i < shades.size(); ++i) {
        const auto* found = std::find(kGrey.begin(), kGrey.end(), grey[i]);
        if (found == kGrey.end()) {
            return path + ": pixel " + std::to_string(i) + " has grey level " +
                   std::to_string(grey[i]) + ", not one of the four shades";
        }
        shades.at(i) = static_cast<std::uint8_t>(found - kGrey.begin());
    }
    return "";
}

bool write_png(const std::string& path, const Frame& shades) {
    std::vector<std::uint8_t> grey(shades.size());
    std::transform(shades.begin(), shades.end(), grey.begin(),
                   [](std::uint8_t shade) { return kGrey.at(shade & 3U); });
    return stbi_write_png(path.c_str(), kWidth, kHeight, 1, grey.data(), kWidth) != 0;
}

}  // namespace

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

testing::AssertionResult matches_screenshot(const Frame& frame, const std::string& reference_png) {
    Frame expected{};
    if (const std::string error = read_reference(reference_png, expected); !error.empty()) {
        return testing::AssertionFailure() << error;
    }
    std::filesystem::path actual_path = reference_png;
    actual_path.replace_filename(actual_path.stem().string() + "-actual.png");

    const auto [first_actual, first_expected] =
        std::mismatch(frame.begin(), frame.end(), expected.begin());
    if (first_actual == frame.end()) {
        std::error_code ignored;
        std::filesystem::remove(actual_path, ignored);
        return testing::AssertionSuccess();
    }
    const auto index = static_cast<std::size_t>(first_actual - frame.begin());
    const auto differing = std::inner_product(
        frame.begin(), frame.end(), expected.begin(), std::size_t{0}, std::plus<>(),
        [](std::uint8_t a, std::uint8_t b) { return std::size_t{a != b ? 1U : 0U}; });
    const bool written = write_png(actual_path.string(), frame);
    return testing::AssertionFailure()
           << differing << " pixels differ. First at (x " << index % kWidth << ", y "
           << index / kWidth << "): expected shade " << int{*first_expected} << ", got "
           << int{*first_actual} << "\n"
           << (written ? "Frame written to " : "Could not write ") << actual_path.string();
}

}  // namespace test
