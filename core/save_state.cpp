#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/gameboy.h"
#include "core/state_io.h"

namespace core {

// The header (docs/cartridge-notes.md, section 7):
//
//   0   8  magic "GBEMUSTA"
//   8   4  format version
//   12  1  the ROM's header checksum (0x014D)
//   13  1  zero
//   14  2  the ROM's global checksum, as stored at 0x014E-0x014F
//   16  16 the ROM title, zero-padded
//   32  4  CRC-32 of everything after the header
//   36  12 reserved, zero
namespace {

constexpr std::string_view kMagic = "GBEMUSTA";
constexpr std::size_t kVersionAt = 8;
constexpr std::size_t kHeaderChecksumAt = 12;
constexpr std::size_t kZeroAt = 13;
constexpr std::size_t kGlobalChecksumAt = 14;
constexpr std::size_t kTitleAt = 16;
constexpr std::size_t kTitleSize = 16;
constexpr std::size_t kCrcAt = 32;
constexpr std::size_t kReservedAt = 36;

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    StateReader in(bytes.subspan(at, sizeof(std::uint32_t)));
    return in.u32();
}

}  // namespace

void GameBoy::save_sections(StateWriter& out) const {
    out.begin_section("CPU ");
    cpu_.save_state(out);
    out.end_section();
    bus_.save_state(out);
}

void GameBoy::load_sections(StateReader& in) {
    in.begin_section("CPU ");
    cpu_.load_state(in);
    in.end_section();
    bus_.load_state(in);
    if (in.ok() && !in.at_end()) {
        in.fail("there is data after the last section");
    }
}

std::vector<std::uint8_t> GameBoy::save_state() const {
    StateWriter payload;
    save_sections(payload);

    StateWriter header;
    for (const char c : kMagic) {
        header.u8(static_cast<std::uint8_t>(c));
    }
    header.u32(kStateVersion);
    header.u8(rom_identity_.header_checksum);
    header.u8(0);
    header.bytes(rom_identity_.global_checksum);
    header.bytes(rom_identity_.title);
    header.u32(crc32(payload.data()));
    while (header.data().size() < kStateHeaderSize) {
        header.u8(0);  // reserved
    }

    std::vector<std::uint8_t> file = header.data();
    file.insert(file.end(), payload.data().begin(), payload.data().end());
    return file;
}

std::string GameBoy::load_state(std::span<const std::uint8_t> state) {
    if (state.size() < kStateHeaderSize) {
        return "the file is too short to be a save state";
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), state.begin())) {
        return "the file is not a save state";
    }
    if (const std::uint32_t version = read_u32(state, kVersionAt); version != kStateVersion) {
        return "the save state is format version " + std::to_string(version) +
               "; this build reads version " + std::to_string(kStateVersion);
    }
    const auto title = state.subspan(kTitleAt, kTitleSize);
    if (state[kHeaderChecksumAt] != rom_identity_.header_checksum ||
        !std::equal(rom_identity_.global_checksum.begin(), rom_identity_.global_checksum.end(),
                    state.begin() + kGlobalChecksumAt) ||
        !std::equal(title.begin(), title.end(), rom_identity_.title.begin())) {
        const std::string name(title.begin(), std::find(title.begin(), title.end(), 0));
        return "the save state is for another ROM (\"" + name + "\")";
    }
    const auto reserved = state.subspan(kReservedAt, kStateHeaderSize - kReservedAt);
    if (state[kZeroAt] != 0 ||
        std::any_of(reserved.begin(), reserved.end(), [](std::uint8_t b) { return b != 0; })) {
        return "the save state has non-zero reserved header bytes";
    }
    const auto payload = state.subspan(kStateHeaderSize);
    if (crc32(payload) != read_u32(state, kCrcAt)) {
        return "the save state is damaged (its CRC-32 does not match)";
    }

    // The checks above cannot see a value a section does not allow (a
    // crafted file with a matching CRC). Keep the current machine so it can
    // be put back if the sections are refused halfway.
    StateWriter backup;
    save_sections(backup);
    StateReader in(payload);
    load_sections(in);
    if (in.ok()) {
        return "";
    }
    StateReader restore(backup.data());
    load_sections(restore);
    return "the save state was refused: " + in.error();
}

}  // namespace core
