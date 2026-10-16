#include "core/state_io.h"

#include <algorithm>
#include <array>
#include <utility>

namespace core {

namespace {

constexpr std::size_t kTagSize = 4;
constexpr unsigned kBitsPerByte = 8;

constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
    constexpr std::uint32_t kPolynomial = 0xEDB88320U;
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1U) != 0 ? kPolynomial ^ (c >> 1U) : c >> 1U;
        }
        table[n] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

std::string printable(std::span<const std::uint8_t> tag) {
    std::string out;
    for (const std::uint8_t c : tag) {
        out += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    return out;
}

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::uint32_t kAllOnes = 0xFFFFFFFFU;
    constexpr std::uint32_t kLowByte = 0xFFU;
    std::uint32_t c = kAllOnes;
    for (const std::uint8_t byte : bytes) {
        c = kCrcTable[(c ^ byte) & kLowByte] ^ (c >> kBitsPerByte);
    }
    return c ^ kAllOnes;
}

// --- StateWriter ---

void StateWriter::u8(std::uint8_t value) {
    data_.push_back(value);
}

void StateWriter::u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> kBitsPerByte));
}

void StateWriter::u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16U));
}

void StateWriter::u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32U));
}

void StateWriter::boolean(bool value) {
    u8(value ? 1 : 0);
}

void StateWriter::bytes(std::span<const std::uint8_t> values) {
    data_.insert(data_.end(), values.begin(), values.end());
}

void StateWriter::optional_u8(const std::optional<std::uint8_t>& value) {
    boolean(value.has_value());
    u8(value.value_or(0));
}

void StateWriter::optional_u16(const std::optional<std::uint16_t>& value) {
    boolean(value.has_value());
    u16(value.value_or(0));
}

void StateWriter::begin_section(std::string_view tag) {
    for (std::size_t i = 0; i < kTagSize; ++i) {
        u8(i < tag.size() ? static_cast<std::uint8_t>(tag[i]) : ' ');
    }
    length_at_ = data_.size();
    u32(0);  // filled in by end_section()
}

void StateWriter::end_section() {
    const std::size_t length = data_.size() - length_at_ - sizeof(std::uint32_t);
    for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
        data_[length_at_ + i] = static_cast<std::uint8_t>(length >> (kBitsPerByte * i));
    }
}

// --- StateReader ---

StateReader::StateReader(std::span<const std::uint8_t> data) noexcept
    : data_(data), limit_(data.size()) {}

void StateReader::fail(std::string reason) {
    if (ok()) {
        error_ = std::move(reason);
    }
}

std::span<const std::uint8_t> StateReader::take(std::size_t count) {
    if (!ok()) {
        return {};
    }
    if (limit_ - position_ < count) {
        fail(section_.empty() ? "the file ends too early"
                              : "section " + section_ + " is too short");
        return {};
    }
    const auto out = data_.subspan(position_, count);
    position_ += count;
    return out;
}

std::uint64_t StateReader::little_endian(std::size_t count) {
    const auto bytes = take(count);
    std::uint64_t value = 0;
    for (std::size_t i = bytes.size(); i > 0; --i) {
        value = (value << kBitsPerByte) | bytes[i - 1];
    }
    return value;
}

std::uint8_t StateReader::u8() {
    return static_cast<std::uint8_t>(little_endian(1));
}

std::uint16_t StateReader::u16() {
    return static_cast<std::uint16_t>(little_endian(2));
}

std::uint32_t StateReader::u32() {
    return static_cast<std::uint32_t>(little_endian(4));
}

std::uint64_t StateReader::u64() {
    return little_endian(8);
}

bool StateReader::boolean() {
    return at_most(u8(), 1) != 0;
}

void StateReader::bytes(std::span<std::uint8_t> out) {
    const auto in = take(out.size());
    if (in.size() == out.size()) {
        std::copy(in.begin(), in.end(), out.begin());
    }
}

std::optional<std::uint8_t> StateReader::optional_u8() {
    const bool present = boolean();
    const std::uint8_t value = u8();
    return present ? std::optional<std::uint8_t>(value) : std::nullopt;
}

std::optional<std::uint16_t> StateReader::optional_u16() {
    const bool present = boolean();
    const std::uint16_t value = u16();
    return present ? std::optional<std::uint16_t>(value) : std::nullopt;
}

std::uint8_t StateReader::at_most(std::uint8_t value, std::uint8_t max) {
    return static_cast<std::uint8_t>(at_most(std::uint32_t{value}, std::uint32_t{max}));
}

std::uint32_t StateReader::at_most(std::uint32_t value, std::uint32_t max) {
    if (value > max) {
        fail("a value in section " + section_ + " is out of range");
        return 0;
    }
    return value;
}

void StateReader::begin_section(std::string_view tag) {
    if (ok() && limit_ - position_ < kTagSize) {
        fail("section " + std::string(tag) + " is missing");
        return;
    }
    const auto found = take(kTagSize);
    if (!ok()) {
        return;
    }
    if (printable(found) != tag) {
        fail("expected section " + std::string(tag) + ", found " + printable(found));
        return;
    }
    const std::uint32_t length = u32();
    if (!ok()) {
        return;
    }
    if (data_.size() - position_ < length) {
        fail("section " + std::string(tag) + " runs past the end of the file");
        return;
    }
    section_ = tag;
    limit_ = position_ + length;
}

void StateReader::end_section() {
    if (ok() && position_ != limit_) {
        fail("section " + section_ + " is longer than this build expects");
    }
    section_.clear();
    limit_ = data_.size();
}

}  // namespace core
