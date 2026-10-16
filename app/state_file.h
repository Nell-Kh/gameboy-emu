#pragma once

#include <filesystem>
#include <ostream>

#include "core/gameboy.h"

namespace frontend {

// Save states from the window (ADR-020; docs/cartridge-notes.md, section 7):
// one file per ROM, `game.state` next to it. F5 saves, F8 loads. No SDL here,
// so tests can drive it directly; headless mode never uses it.

// `game.gb` -> `game.state`, in the same directory.
[[nodiscard]] std::filesystem::path state_path_for(const std::filesystem::path& rom);

class StateSlot {
public:
    StateSlot(core::GameBoy& gb, std::filesystem::path path, std::ostream& log);

    // Writes the machine to the file (through a temporary file and a rename)
    // and says so in the log. Returns false, with the reason in the log, if
    // it could not be written.
    bool save();
    // Loads the file into the machine. Returns false, with the reason in the
    // log, if there is no file or it was refused; the machine is then
    // unchanged.
    bool load();

private:
    core::GameBoy& gb_;
    std::filesystem::path path_;
    std::ostream& log_;
};

}  // namespace frontend
