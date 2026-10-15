#include "app/save_file.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace frontend {

namespace {

constexpr std::size_t kFieldSize = 4;  // each clock register is a 32-bit field
constexpr std::size_t kRegisters = core::Rtc::kRegisterCount;
constexpr std::size_t kLatchedOffset = kRegisters * kFieldSize;   // 20
constexpr std::size_t kTimeOffset = 2 * kRegisters * kFieldSize;  // 40
constexpr unsigned kBitsPerByte = 8;

std::uint64_t read_le(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = bytes.size(); i > 0; --i) {
        value = (value << kBitsPerByte) | bytes[i - 1];
    }
    return value;
}

void write_le(std::span<std::uint8_t> out, std::uint64_t value) {
    for (std::uint8_t& byte : out) {
        byte = static_cast<std::uint8_t>(value);
        value >>= kBitsPerByte;
    }
}

std::string size_list(std::size_t ram_size, bool has_clock) {
    if (!has_clock) {
        return std::to_string(ram_size) + " bytes (its RAM)";
    }
    return std::to_string(ram_size + kClockFooterSize) + " or " +
           std::to_string(ram_size + kShortClockFooterSize) + " bytes (its RAM and the clock)";
}

}  // namespace

std::filesystem::path save_path_for(const std::filesystem::path& rom) {
    std::filesystem::path path = rom;
    path.replace_extension(".sav");
    return path;
}

std::array<std::uint8_t, kClockFooterSize> encode_clock_footer(const ClockFooter& footer) {
    std::array<std::uint8_t, kClockFooterSize> out{};
    const std::span<std::uint8_t> bytes(out);
    for (std::size_t i = 0; i < footer.live.size(); ++i) {
        write_le(bytes.subspan(i * kFieldSize, kFieldSize), footer.live[i]);
        write_le(bytes.subspan(kLatchedOffset + (i * kFieldSize), kFieldSize), footer.latched[i]);
    }
    write_le(bytes.subspan(kTimeOffset), static_cast<std::uint64_t>(footer.saved_at));
    return out;
}

std::optional<ClockFooter> decode_clock_footer(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kClockFooterSize && bytes.size() != kShortClockFooterSize) {
        return std::nullopt;
    }
    ClockFooter footer;
    for (std::size_t i = 0; i < footer.live.size(); ++i) {
        // The value is in the low byte; the other three are ignored.
        footer.live[i] = bytes[i * kFieldSize];
        footer.latched[i] = bytes[kLatchedOffset + (i * kFieldSize)];
    }
    // 64 bits, or 32 in the short form; a 32-bit time is unsigned.
    footer.saved_at = static_cast<std::int64_t>(read_le(bytes.subspan(kTimeOffset)));
    return footer;
}

SaveRead read_save(const std::filesystem::path& path, std::size_t ram_size, bool has_clock) {
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
    std::optional<ClockFooter> clock;
    if (has_clock && bytes.size() >= ram_size) {
        clock = decode_clock_footer(std::span<const std::uint8_t>(bytes).subspan(ram_size));
    }
    const bool right_size = has_clock ? clock.has_value() : bytes.size() == ram_size;
    if (!right_size) {
        result.status = SaveRead::Status::WrongSize;
        result.message = path.string() + " is " + std::to_string(bytes.size()) +
                         " bytes, but this cartridge's save is " + size_list(ram_size, has_clock) +
                         "; it was not loaded and will not be overwritten";
        return result;
    }
    bytes.resize(ram_size);
    result.status = SaveRead::Status::Loaded;
    result.ram = std::move(bytes);
    result.clock = clock;
    return result;
}

std::string write_save(const std::filesystem::path& path, std::span<const std::uint8_t> ram,
                       const ClockFooter* clock) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(ram.data()),
                   static_cast<std::streamsize>(ram.size()));
        if (clock != nullptr) {
            const auto footer = encode_clock_footer(*clock);
            file.write(reinterpret_cast<const char*>(footer.data()),
                       static_cast<std::streamsize>(footer.size()));
        }
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

std::int64_t BatterySaver::system_unix_time() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

BatterySaver::BatterySaver(core::GameBoy& gb, std::filesystem::path path, std::ostream& log,
                           UnixClock unix_now)
    : gb_(gb), path_(std::move(path)), log_(log), unix_now_(std::move(unix_now)) {
    if (!gb_.has_battery()) {
        return;
    }
    const SaveRead read = read_save(path_, gb_.battery_ram().size(), gb_.clock() != nullptr);
    switch (read.status) {
        case SaveRead::Status::NoFile:
            active_ = true;
            break;
        case SaveRead::Status::Loaded:
            load(read);
            break;
        case SaveRead::Status::WrongSize:
        case SaveRead::Status::Unreadable:
            log_ << "gameboy-emu: " << read.message << "\n";
            break;
    }
    gb_.clear_battery_ram_changed();
}

void BatterySaver::load(const SaveRead& read) {
    active_ = gb_.load_battery_ram(read.ram);
    core::Rtc* clock = gb_.clock();
    if (!active_ || clock == nullptr || !read.clock) {
        return;
    }
    clock->restore(read.clock->live, read.clock->latched);
    // The seconds the emulator was closed. A time in the future (a clock
    // set back since) adds nothing, as in BGB and VBA-M.
    const std::int64_t elapsed = unix_now_() - read.clock->saved_at;
    if (elapsed > 0) {
        clock->advance_seconds(static_cast<std::uint64_t>(elapsed));
    }
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
    if (active_ && (gb_.battery_ram_changed() || gb_.clock() != nullptr)) {
        write();
    }
}

void BatterySaver::write() {
    std::optional<ClockFooter> footer;
    if (const core::Rtc* clock = gb_.clock(); clock != nullptr) {
        footer = ClockFooter{clock->live(), clock->latched(), unix_now_()};
    }
    const std::string error = write_save(path_, gb_.battery_ram(), footer ? &*footer : nullptr);
    if (!error.empty()) {
        log_ << "gameboy-emu: " << error << "\n";
        return;  // still changed, so the next tick tries again
    }
    gb_.clear_battery_ram_changed();
}

}  // namespace frontend
