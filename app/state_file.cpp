#include "app/state_file.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app/save_file.h"

namespace frontend {

std::filesystem::path state_path_for(const std::filesystem::path& rom) {
    std::filesystem::path path = rom;
    path.replace_extension(".state");
    return path;
}

StateSlot::StateSlot(core::GameBoy& gb, std::filesystem::path path, std::ostream& log)
    : gb_(gb), path_(std::move(path)), log_(log) {}

bool StateSlot::save() {
    const std::vector<std::uint8_t> state = gb_.save_state();
    if (const std::string error = write_file_atomically(path_, state); !error.empty()) {
        log_ << "gameboy-emu: state not saved: " << error << "\n";
        return false;
    }
    log_ << "gameboy-emu: state saved to " << path_.string() << "\n";
    return true;
}

bool StateSlot::load() {
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        log_ << "gameboy-emu: no saved state at " << path_.string() << " (F5 saves one)\n";
        return false;
    }
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        log_ << "gameboy-emu: cannot read " << path_.string() << "\n";
        return false;
    }
    const std::vector<std::uint8_t> state{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};
    if (const std::string refused = gb_.load_state(state); !refused.empty()) {
        log_ << "gameboy-emu: " << path_.string() << " not loaded: " << refused << "\n";
        return false;
    }
    log_ << "gameboy-emu: state loaded from " << path_.string() << "\n";
    return true;
}

}  // namespace frontend
