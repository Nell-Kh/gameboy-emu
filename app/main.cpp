// gameboy-emu: command-line frontend.
//
// Until the SDL window arrives in M3 the only mode is --headless: run a ROM
// with no display and report what it printed over the serial port. That is
// exactly what the CPU test ROMs need, locally and in CI.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

struct Options {
    std::string rom_path;
    std::uint64_t max_seconds = kDefaultMaxSeconds;
    bool headless = false;
    bool trace = false;
};

void print_usage(std::ostream& out) {
    out << "gameboy-emu " << core::GameBoy::version() << "\n"
        << "\n"
        << "Usage: gameboy-emu --headless ROM [--max-seconds N] [--trace]\n"
        << "\n"
        << "  --headless       run ROM without a window and print its serial output\n"
        << "  --max-seconds N  give up after N emulated seconds (default " << kDefaultMaxSeconds
        << ")\n"
        << "  --trace          print the CPU state before every instruction\n"
        << "\n"
        << "Exit status: 0 if the ROM printed \"Passed\", 1 if it printed \"Failed\",\n"
        << "2 if it did neither before the time limit.\n";
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

// Returns nothing if the arguments make no sense; the reason goes to `err`.
std::optional<Options> parse_arguments(std::span<const std::string_view> args, std::ostream& err) {
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--trace") {
            options.trace = true;
        } else if (arg == "--max-seconds") {
            if (i + 1 >= args.size()) {
                err << "gameboy-emu: --max-seconds needs a number\n";
                return std::nullopt;
            }
            const auto seconds = parse_number(args[++i]);
            if (!seconds) {
                err << "gameboy-emu: not a number: " << args[i] << "\n";
                return std::nullopt;
            }
            options.max_seconds = *seconds;
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
    if (!options.headless) {
        err << "gameboy-emu: only --headless is available so far\n";
        return std::nullopt;
    }
    if (options.rom_path.empty()) {
        err << "gameboy-emu: no ROM given\n";
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

int run_headless(const Options& options) {
    const auto rom = read_file(options.rom_path);
    if (!rom) {
        std::cerr << "gameboy-emu: cannot read " << options.rom_path << "\n";
        return kExitNoInput;
    }

    core::GameBoy gb;
    if (const std::string error = gb.load_rom(*rom); !error.empty()) {
        std::cerr << "gameboy-emu: cannot run " << options.rom_path << ": " << error << "\n";
        return kExitBadRom;
    }

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
    return run_headless(*options);
}
