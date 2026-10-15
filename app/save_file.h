#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <vector>

#include "core/gameboy.h"

namespace frontend {

// Battery saves: the file next to the ROM that keeps a cartridge's battery RAM
// between runs (docs/cartridge-notes.md, section 6; ADR-019). No SDL here, so
// tests can drive it directly.

// `game.gb` -> `game.sav`, in the same directory.
[[nodiscard]] std::filesystem::path save_path_for(const std::filesystem::path& rom);

// The clock footer BGB appends to the RAM of an MBC3 + timer save, which other
// emulators read and write too: the live S, M, H, DL, DH, then the latched
// copy, each as a little-endian 32-bit field with the value in the low byte
// (offsets 0-39), then the Unix time of the save as a 64-bit field (40-47).
// Old VBA wrote a 32-bit time, so 44 bytes are also accepted on load.
struct ClockFooter {
    core::Rtc::Registers live{};
    core::Rtc::Registers latched{};
    std::int64_t saved_at = 0;  // Unix time, in seconds
};

inline constexpr std::size_t kClockFooterSize = 48;
inline constexpr std::size_t kShortClockFooterSize = 44;

[[nodiscard]] std::array<std::uint8_t, kClockFooterSize> encode_clock_footer(
    const ClockFooter& footer);
// Returns nothing unless `bytes` is 44 or 48 bytes long.
[[nodiscard]] std::optional<ClockFooter> decode_clock_footer(std::span<const std::uint8_t> bytes);

struct SaveRead {
    enum class Status : std::uint8_t {
        NoFile,     // nothing saved yet
        Loaded,     // `ram` holds the file
        WrongSize,  // a file of the wrong size: not ours to load or overwrite
        Unreadable  // exists but could not be read
    };
    Status status = Status::NoFile;
    std::vector<std::uint8_t> ram;
    std::optional<ClockFooter> clock;  // when read with has_clock
    std::string message;               // why, for WrongSize and Unreadable
};

// Reads a save file meant for `ram_size` bytes of RAM, followed by a clock
// footer if `has_clock`. With a clock, only RAM + 44 or RAM + 48 bytes is the
// right size.
[[nodiscard]] SaveRead read_save(const std::filesystem::path& path, std::size_t ram_size,
                                 bool has_clock = false);

// Writes `ram`, then the clock footer if there is one, to `path` through
// `path` + ".tmp" and a rename, so a crash mid-write leaves the old file
// whole. Returns an error message, or an empty string on success.
[[nodiscard]] std::string write_save(const std::filesystem::path& path,
                                     std::span<const std::uint8_t> ram,
                                     const ClockFooter* clock = nullptr);

// Keeps one GameBoy's battery RAM and clock in its save file: loads it on
// construction, writes it at most once a second while the program changes
// it, and on flush(). Does nothing for a cartridge without a battery, and
// never writes over a file it refused to load.
//
// With a clock, loading adds the seconds since the file was written, read
// from `unix_now` (the only place the wall clock is read; ADR-018). A saved
// time in the future adds nothing.
class BatterySaver {
public:
    using Clock = std::chrono::steady_clock;
    using UnixClock = std::function<std::int64_t()>;
    static constexpr std::chrono::seconds kMinInterval{1};

    // The current Unix time, in seconds, from the system clock.
    [[nodiscard]] static std::int64_t system_unix_time();

    BatterySaver(core::GameBoy& gb, std::filesystem::path path, std::ostream& log,
                 UnixClock unix_now = system_unix_time);

    // Called once per frame.
    void tick(Clock::time_point now);
    // Called on exit: writes if the program changed anything since the last
    // write, and always for a cartridge with a clock, which has moved on.
    void flush();

    // False for a cartridge without a battery, or after a refused file.
    [[nodiscard]] bool active() const noexcept;

private:
    void load(const SaveRead& read);
    void write();

    core::GameBoy& gb_;
    std::filesystem::path path_;
    std::ostream& log_;
    UnixClock unix_now_;
    bool active_ = false;
    bool written_ = false;
    Clock::time_point last_write_;
};

}  // namespace frontend
