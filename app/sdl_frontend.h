#pragma once

#include <string>

#include "core/gameboy.h"

namespace frontend {

struct WindowOptions {
    std::string title;
    int scale = 4;  // window size as a multiple of 160x144
};

// Opens a window and runs `gb` in it at the Game Boy's own frame rate until the
// window is closed or Escape is pressed. Returns the process exit status.
int run_window(core::GameBoy& gb, const WindowOptions& options);

}  // namespace frontend
