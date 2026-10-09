#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace core {

// A cartridge as the bus sees it: 32 KiB of ROM address space at 0x0000-0x7FFF
// and 8 KiB of RAM address space at 0xA000-0xBFFF. What sits behind them, and
// how banks are switched, depends on the mapper chip (MBC) on the cartridge.
//
// The bus owns the inserted cartridge through a std::unique_ptr (ADR-012).
class Cartridge {
public:
    Cartridge(const Cartridge&) = delete;
    Cartridge& operator=(const Cartridge&) = delete;
    Cartridge(Cartridge&&) = delete;
    Cartridge& operator=(Cartridge&&) = delete;
    virtual ~Cartridge() = default;

    // 0x0000-0x7FFF.
    [[nodiscard]] virtual std::uint8_t read_rom(std::uint16_t address) const noexcept = 0;
    // ROM cannot be written; a mapper uses these writes as its control registers.
    virtual void write_rom(std::uint16_t address, std::uint8_t value) noexcept = 0;

    // 0xA000-0xBFFF. Reads 0xFF when there is no RAM or it is disabled.
    [[nodiscard]] virtual std::uint8_t read_ram(std::uint16_t address) const noexcept = 0;
    virtual void write_ram(std::uint16_t address, std::uint8_t value) noexcept = 0;

protected:
    Cartridge() = default;
};

// What the cartridge header (0x0134-0x014F) says about the hardware.
struct CartridgeHeader {
    std::string title;
    std::uint8_t type = 0;     // the mapper, at 0x0147
    std::size_t rom_size = 0;  // in bytes, decoded from 0x0148
    std::size_t ram_size = 0;  // in bytes, decoded from 0x0149
    bool sizes_valid = false;  // false if 0x0148 or 0x0149 holds an unknown code
};

inline constexpr std::size_t kHeaderEnd = 0x0150;

// Decodes the header. `rom` must be at least kHeaderEnd bytes long.
[[nodiscard]] CartridgeHeader read_header(std::span<const std::uint8_t> rom);

// Builds the right cartridge for a ROM image. On failure `cartridge` is null
// and `error` says why, in a sentence fit for the user.
struct CartridgeOrError {
    std::unique_ptr<Cartridge> cartridge;
    std::string error;
};

[[nodiscard]] CartridgeOrError make_cartridge(std::span<const std::uint8_t> rom);

}  // namespace core
