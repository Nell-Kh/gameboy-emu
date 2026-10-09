#include "core/cartridge/cartridge.h"

#include <array>
#include <string>
#include <string_view>

#include "core/cartridge/mbc1.h"
#include "core/cartridge/no_mbc.h"

namespace core {

namespace {

constexpr std::size_t kTitleStart = 0x0134;
constexpr std::size_t kTitleEnd = 0x0144;
constexpr std::size_t kTypeAddress = 0x0147;
constexpr std::size_t kRomSizeAddress = 0x0148;
constexpr std::size_t kRamSizeAddress = 0x0149;

constexpr std::size_t kMinRomSize = 0x8000;
constexpr std::uint8_t kMaxRomSizeCode = 0x08;

// RAM size codes 0-5. Code 1 (2 KiB) never appeared in a licensed game but is
// documented.
constexpr std::array<std::size_t, 6> kRamSizes = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};

std::string hex_byte(std::uint8_t value) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    return std::string("0x") + kDigits[value >> 4U] + kDigits[value & 0x0FU];
}

enum class Mapper : std::uint8_t { None, Mbc1, Unsupported };

Mapper mapper_for(std::uint8_t type) {
    switch (type) {
        case 0x00:  // ROM only
        case 0x08:  // ROM + RAM
        case 0x09:  // ROM + RAM + battery
            return Mapper::None;
        case 0x01:  // MBC1
        case 0x02:  // MBC1 + RAM
        case 0x03:  // MBC1 + RAM + battery
            return Mapper::Mbc1;
        default:
            return Mapper::Unsupported;
    }
}

}  // namespace

CartridgeHeader read_header(std::span<const std::uint8_t> rom) {
    CartridgeHeader header;
    for (std::size_t i = kTitleStart; i < kTitleEnd && rom[i] != 0; ++i) {
        header.title.push_back(static_cast<char>(rom[i]));
    }
    header.type = rom[kTypeAddress];
    const std::uint8_t rom_code = rom[kRomSizeAddress];
    const std::uint8_t ram_code = rom[kRamSizeAddress];
    header.sizes_valid = rom_code <= kMaxRomSizeCode && ram_code < kRamSizes.size();
    if (header.sizes_valid) {
        header.rom_size = kMinRomSize << rom_code;
        header.ram_size = kRamSizes.at(ram_code);
    }
    return header;
}

CartridgeOrError make_cartridge(std::span<const std::uint8_t> rom) {
    if (rom.size() < kHeaderEnd) {
        return {nullptr, "the file is too small to be a Game Boy ROM (" +
                             std::to_string(rom.size()) + " bytes)"};
    }
    const CartridgeHeader header = read_header(rom);
    if (!header.sizes_valid) {
        return {nullptr, "the header has an unknown ROM or RAM size code (" +
                             hex_byte(rom[kRomSizeAddress]) + ", " +
                             hex_byte(rom[kRamSizeAddress]) + ")"};
    }
    if (rom.size() != header.rom_size) {
        return {nullptr, "the header says the ROM is " + std::to_string(header.rom_size) +
                             " bytes but the file is " + std::to_string(rom.size())};
    }
    switch (mapper_for(header.type)) {
        case Mapper::None:
            if (header.ram_size > 0x2000) {
                return {nullptr, "a cartridge without a mapper cannot have more than 8 KiB of RAM"};
            }
            return {std::make_unique<NoMbc>(rom, header.ram_size), ""};
        case Mapper::Mbc1:
            return {std::make_unique<Mbc1>(rom, header.ram_size), ""};
        case Mapper::Unsupported:
            break;
    }
    return {nullptr, "cartridge type " + hex_byte(header.type) + " is not supported yet"};
}

}  // namespace core
