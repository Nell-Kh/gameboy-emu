#pragma once

#include <filesystem>
#include <string>

#include "app/save_file.h"
#include "core/gameboy.h"

namespace frontend {

struct WindowOptions {
    std::string title;
    int scale = 4;  // window size as a multiple of 160x144
    // Where F5 saves the state and F8 loads it from; empty turns both off.
    std::filesystem::path state_path;
};

// Opens a window and runs `gb` in it at the Game Boy's own frame rate until the
// window is closed, Escape is pressed, or the process gets SIGINT or SIGTERM
// (SDL turns both into its quit event). `saver`, if not null, is ticked once
// per frame; the caller flushes it afterwards. F5 saves a state to
// `options.state_path` and F8 loads it. Returns the process exit status.
int run_window(core::GameBoy& gb, const WindowOptions& options, BatterySaver* saver);

}  // namespace frontend
