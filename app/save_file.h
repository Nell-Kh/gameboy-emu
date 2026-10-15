#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

struct SaveRead {
    enum class Status : std::uint8_t {
        NoFile,     // nothing saved yet
        Loaded,     // `ram` holds the file
        WrongSize,  // a file of the wrong size: not ours to load or overwrite
        Unreadable  // exists but could not be read
    };
    Status status = Status::NoFile;
    std::vector<std::uint8_t> ram;
    std::string message;  // why, for WrongSize and Unreadable
};

// Reads a save file meant for `ram_size` bytes of RAM.
[[nodiscard]] SaveRead read_save(const std::filesystem::path& path, std::size_t ram_size);

// Writes `ram` to `path` through `path` + ".tmp" and a rename, so a crash
// mid-write leaves the old file whole. Returns an error message, or an empty
// string on success.
[[nodiscard]] std::string write_save(const std::filesystem::path& path,
                                     std::span<const std::uint8_t> ram);

// Keeps one GameBoy's battery RAM in its save file: loads it on construction,
// writes it at most once a second while it changes, and on flush(). Does
// nothing for a cartridge without battery RAM, and never writes over a file it
// refused to load.
class BatterySaver {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kMinInterval{1};

    BatterySaver(core::GameBoy& gb, std::filesystem::path path, std::ostream& log);

    // Called once per frame.
    void tick(Clock::time_point now);
    // Writes now if the RAM changed since the last write; called on exit.
    void flush();

    // False for a cartridge without battery RAM, or after a refused file.
    [[nodiscard]] bool active() const noexcept;

private:
    void write();

    core::GameBoy& gb_;
    std::filesystem::path path_;
    std::ostream& log_;
    bool active_ = false;
    bool written_ = false;
    Clock::time_point last_write_;
};

}  // namespace frontend
