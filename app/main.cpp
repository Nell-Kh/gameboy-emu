// gameboy-emu: command-line frontend.
//
// Two modes. By default the ROM runs in an SDL window (sdl_frontend.cpp).
// With --headless it runs with no display and reports what it printed over the
// serial port, which is what the CPU test ROMs need, locally and in CI.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "app/save_file.h"
#include "app/sdl_frontend.h"
#include "core/gameboy.h"

namespace {

// Exit codes. 0/1 follow the test ROM's own verdict.
constexpr int kExitPassed = 0;
constexpr int kExitFailed = 1;
constexpr int kExitNoVerdict = 2;
constexpr int kExitUsage = 64;
constexpr int kExitBadRom = 65;
constexpr int kExitNoInput = 66;

constexpr std::uint64_t kDefaultMaxSeconds = 60;
constexpr int kDefaultScale = 4;
constexpr std::uint64_t kMaxScale = 16;

struct Options {
    std::string rom_path;
    std::uint64_t max_seconds = kDefaultMaxSeconds;
    int scale = kDefaultScale;
    bool headless = false;
    bool trace = false;
    bool headless_only_option = false;  // --max-seconds or --trace was given
    bool scale_given = false;
};

void print_usage(std::ostream& out) {
    out << "gameboy-emu " << core::GameBoy::version() << "\n"
        << "\n"
        << "Usage: gameboy-emu ROM [--scale N]\n"
        << "       gameboy-emu --headless ROM [--max-seconds N] [--trace]\n"
        << "\n"
        << "  ROM              play ROM in a window\n"
        << "  --scale N        window size as a multiple of 160x144 (default " << kDefaultScale
        << ")\n"
        << "\n"
        << "  Keys: arrows = d-pad, X = A, Z = B, Enter = Start, Backspace = Select,\n"
        << "        Esc = quit\n"
        << "  Battery saves (and a cartridge clock) are kept next to the ROM, with the\n"
        << "  extension .sav.\n"
        << "\n"
        << "  --headless       run ROM without a window and print its serial output\n"
        << "  --max-seconds N  give up after N emulated seconds (default " << kDefaultMaxSeconds
        << ")\n"
        << "  --trace          print the CPU state before every instruction\n"
        << "\n"
        << "Headless exit status: 0 if the ROM printed \"Passed\", 1 if it printed\n"
        << "\"Failed\", 2 if it did neither before the time limit.\n";
}

std::optional<std::uint64_t> parse_number(std::string_view text) {
    std::uint64_t value = 0;
    const char* const end = text.data() + text.size();
    const auto [parsed_to, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || parsed_to != end) {
        return std::nullopt;
    }
    return value;
}

// Reads the number that follows the option at args[i] and moves i past it.
std::optional<std::uint64_t> option_value(std::span<const std::string_view> args, std::size_t& i,
                                          std::ostream& err) {
    const std::string_view option = args[i];
    if (i + 1 >= args.size()) {
        err << "gameboy-emu: " << option << " needs a number\n";
        return std::nullopt;
    }
    const auto value = parse_number(args[++i]);
    if (!value) {
        err << "gameboy-emu: not a number: " << args[i] << "\n";
    }
    return value;
}

// Checks the options that only make sense together. Returns false, with the
// reason in `err`, if they do not.
bool options_fit(const Options& options, std::ostream& err) {
    if (!options.headless && options.headless_only_option) {
        err << "gameboy-emu: --max-seconds and --trace need --headless\n";
        return false;
    }
    if (options.headless && options.scale_given) {
        err << "gameboy-emu: --scale has no effect with --headless\n";
        return false;
    }
    if (options.rom_path.empty()) {
        err << "gameboy-emu: no ROM given\n";
        return false;
    }
    return true;
}

// Returns nothing if the arguments make no sense; the reason goes to `err`.
std::optional<Options> parse_arguments(std::span<const std::string_view> args, std::ostream& err) {
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--trace") {
            options.trace = true;
            options.headless_only_option = true;
        } else if (arg == "--scale") {
            const auto scale = option_value(args, i, err);
            if (!scale) {
                return std::nullopt;
            }
            if (*scale < 1 || *scale > kMaxScale) {
                err << "gameboy-emu: --scale must be 1 to " << kMaxScale << "\n";
                return std::nullopt;
            }
            options.scale = static_cast<int>(*scale);
            options.scale_given = true;
        } else if (arg == "--max-seconds") {
            const auto seconds = option_value(args, i, err);
            if (!seconds) {
                return std::nullopt;
            }
            options.max_seconds = *seconds;
            options.headless_only_option = true;
        } else if (arg.starts_with("--")) {
            err << "gameboy-emu: unknown option " << arg << "\n";
            return std::nullopt;
        } else if (options.rom_path.empty()) {
            options.rom_path = arg;
        } else {
            err << "gameboy-emu: more than one ROM given\n";
            return std::nullopt;
        }
    }
    if (!options_fit(options, err)) {
        return std::nullopt;
    }
    return options;
}

std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

void append_hex(std::string& out, std::uint16_t value, int digits) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(value >> static_cast<unsigned>(shift)) & 0x0FU]);
    }
}

// One line per instruction, in the format the Gameboy Doctor tool compares
// against reference logs:
//   A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,13,02
std::string trace_line(const core::GameBoy& gb) {
    const core::Registers& r = gb.cpu().registers();
    const std::array<std::pair<std::string_view, std::uint8_t>, 8> bytes = {{
        {"A:", r.a},
        {" F:", r.f},
        {" B:", r.b},
        {" C:", r.c},
        {" D:", r.d},
        {" E:", r.e},
        {" H:", r.h},
        {" L:", r.l},
    }};
    std::string line;
    for (const auto& [label, value] : bytes) {
        line += label;
        append_hex(line, value, 2);
    }
    line += " SP:";
    append_hex(line, r.sp, 4);
    line += " PC:";
    append_hex(line, r.pc, 4);
    line += " PCMEM:";
    for (std::uint16_t offset = 0; offset < 4; ++offset) {
        if (offset != 0) {
            line.push_back(',');
        }
        append_hex(line, gb.bus().read8(static_cast<std::uint16_t>(r.pc + offset)), 2);
    }
    return line;
}

// Reads the ROM and inserts it. Returns an exit status if that failed.
std::optional<int> load(core::GameBoy& gb, const std::string& path) {
    const auto rom = read_file(path);
    if (!rom) {
        std::cerr << "gameboy-emu: cannot read " << path << "\n";
        return kExitNoInput;
    }
    if (const std::string error = gb.load_rom(*rom); !error.empty()) {
        std::cerr << "gameboy-emu: cannot run " << path << ": " << error << "\n";
        return kExitBadRom;
    }
    return std::nullopt;
}

int run_headless(core::GameBoy& gb, const Options& options) {
    const std::uint64_t limit = options.max_seconds * core::GameBoy::kTicksPerSecond;
    const std::string& serial = gb.serial_output();
    std::size_t printed = 0;
    int verdict = kExitNoVerdict;

    while (gb.cycles() < limit && !gb.cpu().locked()) {
        if (options.trace) {
            std::cout << trace_line(gb) << '\n';
        }
        gb.step();

        if (serial.size() == printed) {
            continue;
        }
        // With --trace the serial text would be lost between the trace lines,
        // so it is printed once at the end instead.
        if (!options.trace) {
            std::cout << std::string_view(serial).substr(printed) << std::flush;
        }
        printed = serial.size();
        if (serial.find("Passed") != std::string::npos) {
            verdict = kExitPassed;
            break;
        }
        if (serial.find("Failed") != std::string::npos) {
            verdict = kExitFailed;
            break;
        }
    }

    if (options.trace) {
        std::cerr << serial << '\n';
    }
    if (verdict == kExitNoVerdict) {
        std::cerr << "\ngameboy-emu: no verdict after " << gb.cycles() << " ticks"
                  << (gb.cpu().locked() ? " (CPU locked on an undefined opcode)" : "") << "\n";
    } else if (serial.empty() || serial.back() != '\n') {
        std::cout << '\n';
    }
    return verdict;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }
    const auto options = parse_arguments(args, std::cerr);
    if (!options) {
        std::cerr << "Try gameboy-emu --help\n";
        return kExitUsage;
    }
    core::GameBoy gb;
    if (const auto failed = load(gb, options->rom_path)) {
        return *failed;
    }
    if (options->headless) {
        return run_headless(gb, *options);
    }
    // Battery saves live next to the ROM; headless runs never touch them.
    frontend::BatterySaver saver(gb, frontend::save_path_for(options->rom_path), std::cerr);
    const std::string name = std::filesystem::path(options->rom_path).filename().string();
    const int status = frontend::run_window(
        gb, {.title = name + " - gameboy-emu", .scale = options->scale}, &saver);
    saver.flush();
    return status;
}
