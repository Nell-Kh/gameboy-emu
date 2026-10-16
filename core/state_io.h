#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core {

// Building blocks of the save-state format (ADR-020; docs/cartridge-notes.md,
// section 7): little-endian fields written one by one, never a memcpy of a
// struct, grouped into sections of a 4-byte ASCII tag, a 32-bit length and the
// payload.

// The CRC-32 of zlib and PNG (reflected polynomial 0xEDB88320).
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept;

class StateWriter {
public:
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void boolean(bool value);
    void bytes(std::span<const std::uint8_t> values);
    // A presence byte (0 or 1), then the value or zeros of the same size.
    void optional_u8(const std::optional<std::uint8_t>& value);
    void optional_u16(const std::optional<std::uint16_t>& value);

    // A section: `tag` is exactly four characters. end_section() fills in
    // the length.
    void begin_section(std::string_view tag);
    void end_section();

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept {
        return data_;
    }

private:
    std::vector<std::uint8_t> data_;
    std::size_t length_at_ = 0;
};

// Reads what StateWriter wrote. The first problem (running out of bytes, a
// wrong tag or length, a value out of range) is recorded; after it, every
// read returns 0 and ok() is false, so loaders can read straight through and
// check once at the end.
class StateReader {
public:
    explicit StateReader(std::span<const std::uint8_t> data) noexcept;

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint16_t u16();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    // Fails unless the byte is 0 or 1.
    [[nodiscard]] bool boolean();
    void bytes(std::span<std::uint8_t> out);
    [[nodiscard]] std::optional<std::uint8_t> optional_u8();
    [[nodiscard]] std::optional<std::uint16_t> optional_u16();

    // Fails unless `value <= max`, for enums and counters a crafted file
    // could push out of range. Returns `value` or 0.
    [[nodiscard]] std::uint8_t at_most(std::uint8_t value, std::uint8_t max);
    [[nodiscard]] std::uint32_t at_most(std::uint32_t value, std::uint32_t max);

    // Reads a section's tag and length; reads after it may not go past the
    // section. end_section() fails unless exactly the whole section was read.
    void begin_section(std::string_view tag);
    void end_section();

    void fail(std::string reason);
    [[nodiscard]] bool ok() const noexcept {
        return error_.empty();
    }
    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }
    [[nodiscard]] bool at_end() const noexcept {
        return position_ == data_.size();
    }

private:
    // The next `count` bytes, or an empty span after failing.
    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t count);
    [[nodiscard]] std::uint64_t little_endian(std::size_t count);

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    std::size_t limit_;  // the end of the current section, or of the data
    std::string section_;
    std::string error_;
};

}  // namespace core
