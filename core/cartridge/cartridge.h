#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace core {

class StateReader;
class StateWriter;

class Rtc;

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

    // Called by the bus with the clock ticks that have passed, like the timer.
    // Only a cartridge with its own clock (MBC3 + timer) does anything with it.
    virtual void tick(std::uint32_t /*t_cycles*/) noexcept {}

    // Battery-backed RAM, which the frontend keeps in a save file
    // (docs/cartridge-notes.md, section 6). The core never touches files.
    //
    // True if the cartridge type has a battery and there is RAM or a clock
    // for it to keep.
    [[nodiscard]] bool has_battery() const noexcept;
    // The real-time clock (MBC3 + timer), or null for every other cartridge.
    [[nodiscard]] virtual Rtc* clock() noexcept {
        return nullptr;
    }
    [[nodiscard]] virtual const Rtc* clock() const noexcept {
        return nullptr;
    }
    // The whole RAM chip, every bank, as the save file stores it.
    [[nodiscard]] std::span<const std::uint8_t> ram() const noexcept;
    // Replaces the RAM contents, as when a save file is loaded. Returns false
    // and changes nothing if `data` is not exactly the RAM size.
    [[nodiscard]] bool load_ram(std::span<const std::uint8_t> data) noexcept;
    // Whether the program has changed what the battery keeps since the last
    // clear: a RAM byte, or a clock register it wrote (the clock ticking on
    // its own does not count). The frontend uses this to decide when to write
    // the save file.
    [[nodiscard]] bool ram_changed() const noexcept;
    void clear_ram_changed() noexcept;
    // For a write the battery keeps that is not a RAM byte, and for a reset
    // that must not lose a change not yet saved.
    void mark_changed() noexcept;

    // Save states: the mapper's registers, then the RAM, then the clock if
    // there is one. Loading RAM that differs from the current RAM counts as
    // a change, so the battery save is brought up to date.
    void save_state(StateWriter& out) const;
    void load_state(StateReader& in);

protected:
    Cartridge(std::size_t ram_size, bool battery);

    [[nodiscard]] std::size_t ram_size() const noexcept;
    // `offset` is into the whole chip and must be below ram_size().
    [[nodiscard]] std::uint8_t ram_at(std::size_t offset) const noexcept;
    void set_ram_at(std::size_t offset, std::uint8_t value) noexcept;

    // The mapper's own part of a save state, before and after the RAM.
    virtual void save_registers(StateWriter& /*out*/) const {}
    virtual void load_registers(StateReader& /*in*/) {}
    virtual void save_clock(StateWriter& /*out*/) const {}
    virtual void load_clock(StateReader& /*in*/) {}

private:
    std::vector<std::uint8_t> ram_;
    bool battery_;
    bool ram_changed_ = false;
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
