#include "app/png.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>

#include "core/state_io.h"

namespace frontend {

namespace {

constexpr unsigned kBitsPerByte = 8;
constexpr std::size_t kMaxStoredBlock = 0xFFFF;  // bytes in one stored deflate block
constexpr std::uint32_t kAdlerModulus = 65521;

void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= static_cast<int>(kBitsPerByte)) {
        out.push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
    }
}

// A chunk: length, type, data, and the CRC-32 of type and data.
void put_chunk(std::vector<std::uint8_t>& out, std::string_view type,
               std::span<const std::uint8_t> data) {
    put_u32_be(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed(type.begin(), type.end());
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put_u32_be(out, core::crc32(typed));
}

std::uint32_t adler32(std::span<const std::uint8_t> bytes) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : bytes) {
        a = (a + byte) % kAdlerModulus;
        b = (b + a) % kAdlerModulus;
    }
    return (b << 16U) | a;
}

// A zlib stream of stored blocks (RFC 1950 and 1951).
std::vector<std::uint8_t> zlib_stored(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> out = {0x78, 0x01};  // deflate, 32 KB window, no dictionary
    std::size_t at = 0;
    do {
        const std::size_t size = std::min(kMaxStoredBlock, raw.size() - at);
        const bool last = at + size == raw.size();
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE 00 (stored)
        const auto len = static_cast<std::uint16_t>(size);
        const auto nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(len));
        out.push_back(static_cast<std::uint8_t>(len >> kBitsPerByte));
        out.push_back(static_cast<std::uint8_t>(nlen));
        out.push_back(static_cast<std::uint8_t>(nlen >> kBitsPerByte));
        const auto first = raw.begin() + static_cast<std::ptrdiff_t>(at);
        out.insert(out.end(), first, first + static_cast<std::ptrdiff_t>(size));
        at += size;
    } while (at < raw.size());
    put_u32_be(out, adler32(raw));
    return out;
}

}  // namespace

std::vector<std::uint8_t> encode_png(std::size_t width, std::size_t height,
                                     std::span<const std::uint32_t> argb) {
    constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P',  'N',  'G',
                                                        '\r', '\n', 0x1A, '\n'};
    constexpr std::uint8_t kBitDepth = 8;
    constexpr std::uint8_t kColourTypeRgb = 2;
    constexpr unsigned kRedShift = 16;
    constexpr unsigned kGreenShift = 8;

    std::vector<std::uint8_t> out(kSignature.begin(), kSignature.end());
    std::vector<std::uint8_t> header;
    put_u32_be(header, static_cast<std::uint32_t>(width));
    put_u32_be(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(),
                  {kBitDepth, kColourTypeRgb, 0, 0, 0});  // deflate, no filter set, no interlace
    put_chunk(out, "IHDR", header);

    std::vector<std::uint8_t> raw;
    raw.reserve(height * (1 + (3 * width)));
    for (std::size_t y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type None
        for (std::size_t x = 0; x < width; ++x) {
            const std::uint32_t pixel = argb[(y * width) + x];
            raw.push_back(static_cast<std::uint8_t>(pixel >> kRedShift));
            raw.push_back(static_cast<std::uint8_t>(pixel >> kGreenShift));
            raw.push_back(static_cast<std::uint8_t>(pixel));
        }
    }
    put_chunk(out, "IDAT", zlib_stored(raw));
    put_chunk(out, "IEND", {});
    return out;
}

std::filesystem::path screenshot_path_for(const std::filesystem::path& rom) {
    const std::filesystem::path base = rom.parent_path() / rom.stem();
    for (int n = 1;; ++n) {
        std::filesystem::path path = base;
        path += "-" + std::to_string(n) + ".png";
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return path;
        }
    }
}

}  // namespace frontend
