#include "app/save_file.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace frontend {

std::filesystem::path save_path_for(const std::filesystem::path& rom) {
    std::filesystem::path path = rom;
    path.replace_extension(".sav");
    return path;
}

SaveRead read_save(const std::filesystem::path& path, std::size_t ram_size) {
    SaveRead result;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return result;  // NoFile
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.status = SaveRead::Status::Unreadable;
        result.message = "cannot read " + path.string();
        return result;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>()};
    if (bytes.size() != ram_size) {
        result.status = SaveRead::Status::WrongSize;
        result.message = path.string() + " is " + std::to_string(bytes.size()) +
                         " bytes, but this cartridge has " + std::to_string(ram_size) +
                         " bytes of RAM; it was not loaded and will not be overwritten";
        return result;
    }
    result.status = SaveRead::Status::Loaded;
    result.ram = std::move(bytes);
    return result;
}

std::string write_save(const std::filesystem::path& path, std::span<const std::uint8_t> ram) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(ram.data()),
                   static_cast<std::streamsize>(ram.size()));
        if (!file) {
            return "cannot write " + temporary.string();
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        return "cannot replace " + path.string() + ": " + error.message();
    }
    return "";
}

BatterySaver::BatterySaver(core::GameBoy& gb, std::filesystem::path path, std::ostream& log)
    : gb_(gb), path_(std::move(path)), log_(log) {
    const std::span<const std::uint8_t> ram = gb_.battery_ram();
    if (ram.empty()) {
        return;
    }
    const SaveRead read = read_save(path_, ram.size());
    switch (read.status) {
        case SaveRead::Status::NoFile:
            active_ = true;
            break;
        case SaveRead::Status::Loaded:
            active_ = gb_.load_battery_ram(read.ram);
            break;
        case SaveRead::Status::WrongSize:
        case SaveRead::Status::Unreadable:
            log_ << "gameboy-emu: " << read.message << "\n";
            break;
    }
    gb_.clear_battery_ram_changed();
}

bool BatterySaver::active() const noexcept {
    return active_;
}

void BatterySaver::tick(Clock::time_point now) {
    if (!active_ || !gb_.battery_ram_changed()) {
        return;
    }
    if (written_ && now - last_write_ < kMinInterval) {
        return;
    }
    write();
    last_write_ = now;
    written_ = true;
}

void BatterySaver::flush() {
    if (active_ && gb_.battery_ram_changed()) {
        write();
    }
}

void BatterySaver::write() {
    if (const std::string error = write_save(path_, gb_.battery_ram()); !error.empty()) {
        log_ << "gameboy-emu: " << error << "\n";
        return;  // still changed, so the next tick tries again
    }
    gb_.clear_battery_ram_changed();
}

}  // namespace frontend
