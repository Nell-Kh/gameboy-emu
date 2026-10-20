#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace frontend {

// A PNG file of `width` x `height` pixels, given as 0xAARRGGBB row by row
// (alpha ignored), written as 8-bit RGB. The image data goes in stored
// (uncompressed) deflate blocks: a 160x144 screenshot is about 70 KB, and
// the frontend needs no compression library for it.
[[nodiscard]] std::vector<std::uint8_t> encode_png(std::size_t width, std::size_t height,
                                                   std::span<const std::uint32_t> argb);

// The first of `<stem>-1.png`, `<stem>-2.png`, ... next to `rom` that does
// not exist yet, so a screenshot never replaces an earlier one.
[[nodiscard]] std::filesystem::path screenshot_path_for(const std::filesystem::path& rom);

}  // namespace frontend
