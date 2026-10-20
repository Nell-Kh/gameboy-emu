// Save states (ADR-020; docs/cartridge-notes.md, section 7): the building
// blocks in core/state_io.h, then GameBoy::save_state and load_state on small
// generated ROMs. The determinism tests on real ROMs are in
// tests/save_state_rom_test.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "core/gameboy.h"
#include "core/state_io.h"
#include "test_rom.h"

namespace {

using core::GameBoy;
using core::StateReader;
using core::StateWriter;
using Bytes = std::vector<std::uint8_t>;

// --- state_io ---

TEST(Crc32, MatchesTheStandardCheckValue) {
    const std::string check = "123456789";
    const Bytes bytes(check.begin(), check.end());
    EXPECT_EQ(core::crc32(bytes), 0xCBF43926U);
    EXPECT_EQ(core::crc32({}), 0x00000000U);
}

TEST(StateWriter, WritesLittleEndianFieldsAndPatchesTheSectionLength) {
    StateWriter out;
    out.begin_section("ABCD");
    out.u8(0x01);
    out.u16(0x0302);
    out.u32(0x07060504);
    out.u64(0x0F0E0D0C0B0A0908);
    out.boolean(true);
    out.optional_u8(std::nullopt);
    out.optional_u16(std::uint16_t{0x1234});
    out.end_section();
    const Bytes expected = {'A',  'B',  'C',  'D',  21,   0,    0,    0,  // tag, length
                            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                            0x0C, 0x0D, 0x0E, 0x0F, 0x01, 0x00, 0x00, 0x01, 0x34, 0x12};
    EXPECT_EQ(out.data(), expected);
}

TEST(StateReader, ReadsBackWhatTheWriterWrote) {
    StateWriter out;
    out.begin_section("TEST");
    out.u16(0xBEEF);
    out.u64(0x0123456789ABCDEF);
    out.optional_u16(std::nullopt);
    out.optional_u8(std::uint8_t{7});
    out.end_section();
    StateReader in(out.data());
    in.begin_section("TEST");
    EXPECT_EQ(in.u16(), 0xBEEF);
    EXPECT_EQ(in.u64(), 0x0123456789ABCDEFU);
    EXPECT_FALSE(in.optional_u16().has_value());
    EXPECT_EQ(in.optional_u8(), std::optional<std::uint8_t>(7));
    in.end_section();
    EXPECT_TRUE(in.ok()) << in.error();
    EXPECT_TRUE(in.at_end());
}

TEST(StateReader, RefusesAWrongTagAShortSectionAndLeftoverBytes) {
    StateWriter out;
    out.begin_section("GOOD");
    out.u16(1);
    out.end_section();

    StateReader wrong_tag(out.data());
    wrong_tag.begin_section("BAD ");
    EXPECT_EQ(wrong_tag.error(), "expected section BAD , found GOOD");

    StateReader too_much(out.data());
    too_much.begin_section("GOOD");
    (void)too_much.u32();  // the section has only 2 bytes
    EXPECT_EQ(too_much.error(), "section GOOD is too short");

    StateReader too_little(out.data());
    too_little.begin_section("GOOD");
    (void)too_little.u8();
    too_little.end_section();
    EXPECT_EQ(too_little.error(), "section GOOD is longer than this build expects");
}

TEST(StateReader, RefusesValuesOutOfRangeAndKeepsTheFirstError) {
    const Bytes bytes = {2, 9};
    StateReader in(bytes);
    EXPECT_FALSE(in.boolean());  // 2 is not a boolean
    EXPECT_FALSE(in.ok());
    const std::string first = in.error();
    EXPECT_EQ(in.u8(), 0);  // after a failure every read is 0
    in.fail("another reason");
    EXPECT_EQ(in.error(), first);
}

// --- GameBoy::save_state and load_state ---

constexpr std::size_t kTitle = 0x0134;
constexpr std::size_t kType = 0x0147;
constexpr std::size_t kRamSizeCode = 0x0149;
constexpr std::size_t kHeaderChecksum = 0x014D;

// Increments 0xC000 forever, so work RAM, the tick count, the timer and the
// PPU all move while it runs. `type` and `ram_code` go into the header.
Bytes counter_rom(std::uint8_t type = 0x00, std::uint8_t ram_code = 0x00,
                  const std::string& title = "STATETEST") {
    Bytes rom = test::make_rom({
        0x21, 0x00, 0xC0,  // LD HL, 0xC000
        0x34,              // 0x0103: INC [HL]
        0x18, 0xFD,        // JR 0x0103
    });
    std::copy(title.begin(), title.end(), rom.begin() + kTitle);
    rom[kType] = type;
    rom[kRamSizeCode] = ram_code;
    rom[kHeaderChecksum] = 0x5A;
    rom[kHeaderChecksum + 1] = 0x12;
    rom[kHeaderChecksum + 2] = 0x34;
    return rom;
}

void start(GameBoy& gb, const Bytes& rom, std::uint64_t ticks) {
    ASSERT_EQ(gb.load_rom(rom), "");
    gb.run_for(ticks);
}

// The sections after the header, as tag and length.
std::vector<std::pair<std::string, std::uint32_t>> sections(const Bytes& state) {
    std::vector<std::pair<std::string, std::uint32_t>> out;
    std::size_t at = GameBoy::kStateHeaderSize;
    while (at + 8 <= state.size()) {
        const std::string tag(state.begin() + static_cast<std::ptrdiff_t>(at),
                              state.begin() + static_cast<std::ptrdiff_t>(at + 4));
        const std::uint32_t length = state[at + 4] | (state[at + 5] << 8U) |
                                     (state[at + 6] << 16U) |
                                     (static_cast<std::uint32_t>(state[at + 7]) << 24U);
        out.emplace_back(tag, length);
        at += 8 + length;
    }
    EXPECT_EQ(at, state.size());
    return out;
}

TEST(SaveState, TheHeaderHoldsTheMagicVersionRomIdentityAndCrc) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom()), "");
    const Bytes state = gb.save_state();
    ASSERT_GT(state.size(), GameBoy::kStateHeaderSize);
    const Bytes header(state.begin(), state.begin() + GameBoy::kStateHeaderSize);
    Bytes expected = {'P', 'E', 'A', 'S', 'O', 'U', 'P', 'S', 3, 0, 0, 0, 0x5A, 0, 0x12, 0x34};
    const std::string title = "STATETEST";
    expected.insert(expected.end(), title.begin(), title.end());
    expected.resize(32, 0);
    const std::uint32_t crc =
        core::crc32(std::span<const std::uint8_t>(state).subspan(GameBoy::kStateHeaderSize));
    for (unsigned i = 0; i < 4; ++i) {
        expected.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    }
    expected.resize(GameBoy::kStateHeaderSize, 0);
    EXPECT_EQ(header, expected);
}

// The section sizes are the ones in the table in docs/cartridge-notes.md,
// section 7, so the document and the code cannot drift apart.
TEST(SaveState, SectionsComeInOrderWithTheDocumentedSizes) {
    const std::vector<std::pair<std::string, std::uint32_t>> fixed = {
        {"CPU ", 17}, {"BUS ", 8461}, {"TIMR", 7},  {"SERL", 6},
        {"DMA ", 8},  {"JOYP", 2},    {"APU ", 97}, {"PPU ", 31442},
    };
    struct Cart {
        std::uint8_t type;
        std::uint8_t ram_code;
        std::uint32_t cart_size;
    };
    // No mapper: RAM only. MBC1: 4 register bytes. MBC3 + timer: 4 register
    // bytes, then RAM, then the clock (5 + 5 + 4). MBC5: 4 register bytes.
    for (const Cart cart : {Cart{0x00, 0x00, 0}, Cart{0x03, 0x02, 4 + 0x2000},
                            Cart{0x10, 0x02, 4 + 0x2000 + 14}, Cart{0x1B, 0x03, 4 + 0x8000}}) {
        GameBoy gb;
        ASSERT_EQ(gb.load_rom(counter_rom(cart.type, cart.ram_code)), "");
        auto expected = fixed;
        expected.emplace_back("CART", cart.cart_size);
        EXPECT_EQ(sections(gb.save_state()), expected) << "type " << int{cart.type};
    }
}

TEST(SaveState, LoadingPutsTheMachineBackWhereItWasSaved) {
    GameBoy gb;
    start(gb, counter_rom(0x03, 0x02), 100'000);
    const Bytes saved = gb.save_state();
    const auto registers = gb.cpu().registers();
    const std::uint64_t cycles = gb.cycles();
    const std::uint8_t counter = gb.bus().read8(0xC000);

    gb.run_for(250'000);
    ASSERT_NE(gb.cycles(), cycles);
    ASSERT_EQ(gb.load_state(saved), "");
    EXPECT_EQ(gb.cycles(), cycles);
    EXPECT_EQ(gb.cpu().registers().pc, registers.pc);
    EXPECT_EQ(gb.cpu().registers().af(), registers.af());
    EXPECT_EQ(gb.bus().read8(0xC000), counter);
    EXPECT_EQ(gb.save_state(), saved);
}

TEST(SaveState, AStateLoadsIntoAFreshGameBoyWithTheSameRom) {
    const Bytes rom = counter_rom(0x10, 0x02);  // MBC3 + timer + RAM: the clock too
    GameBoy first;
    start(first, rom, 3 * GameBoy::kTicksPerSecond);
    const Bytes saved = first.save_state();
    GameBoy second;
    ASSERT_EQ(second.load_rom(rom), "");
    ASSERT_EQ(second.load_state(saved), "");
    EXPECT_EQ(second.save_state(), saved);
    ASSERT_NE(second.clock(), nullptr);
    EXPECT_EQ(second.clock()->live()[0], 3);
}

TEST(SaveState, LoadingDifferentBatteryRamCountsAsAChange) {
    const Bytes rom = counter_rom(0x03, 0x02);  // MBC1 + RAM + battery
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    Bytes ram(0x2000, 0x00);
    ram[5] = 0x99;
    ASSERT_TRUE(gb.load_battery_ram(ram));
    const Bytes saved = gb.save_state();
    ASSERT_TRUE(gb.load_battery_ram(Bytes(0x2000, 0x00)));
    ASSERT_FALSE(gb.battery_ram_changed());
    ASSERT_EQ(gb.load_state(saved), "");
    EXPECT_EQ(gb.battery_ram()[5], 0x99);
    EXPECT_TRUE(gb.battery_ram_changed());
}

// Each refused file must leave the machine exactly as it was.
class Refused : public testing::Test {
protected:
    void SetUp() override {
        start(gb_, counter_rom(0x03, 0x02), 50'000);
        good_ = gb_.save_state();
        gb_.run_for(70'000);
        before_ = gb_.save_state();
    }

    void expect_refused(const Bytes& state, const std::string& reason) {
        EXPECT_EQ(gb_.load_state(state), reason);
        EXPECT_EQ(gb_.save_state(), before_);
    }

    // Rewrites the CRC so only the change under test is wrong.
    static void fix_crc(Bytes& state) {
        const std::uint32_t crc =
            core::crc32(std::span<const std::uint8_t>(state).subspan(GameBoy::kStateHeaderSize));
        for (unsigned i = 0; i < 4; ++i) {
            state[32 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
        }
    }

    // Offset of the first payload byte of section `tag`.
    [[nodiscard]] std::size_t payload_of(const std::string& tag) const {
        std::size_t at = GameBoy::kStateHeaderSize;
        for (const auto& [name, length] : sections(good_)) {
            if (name == tag) {
                return at + 8;
            }
            at += 8 + length;
        }
        ADD_FAILURE() << "no section " << tag;
        return 0;
    }

    GameBoy gb_;
    Bytes good_;
    Bytes before_;
};

TEST_F(Refused, ATooShortFile) {
    expect_refused(Bytes(good_.begin(), good_.begin() + 47),
                   "the file is too short to be a save state");
}

TEST_F(Refused, AWrongMagic) {
    Bytes state = good_;
    state[0] = 'X';
    expect_refused(state, "the file is not a save state");
}

TEST_F(Refused, AnotherFormatVersion) {
    Bytes state = good_;
    state[8] = 2;
    expect_refused(state, "the save state is format version 2; this build reads version 3");
    state[8] = 4;
    expect_refused(state, "the save state is format version 4; this build reads version 3");
}

// States from v0.4.0 (version 1) and v0.5.0 (version 2) have the old magic;
// they are refused with where they came from, and there is no converter
// (docs/apu-notes.md, section 8; ADR-026).
TEST_F(Refused, AStateFromBeforeTheRename) {
    Bytes state = good_;
    const std::string old_magic = "GBEMUSTA";
    std::copy(old_magic.begin(), old_magic.end(), state.begin());
    state[8] = 2;
    expect_refused(state,
                   "the save state is from gameboy-emu 0.5.0 (format version 2); this build "
                   "reads version 3");
    state[8] = 1;
    expect_refused(state,
                   "the save state is from gameboy-emu 0.4.0 (format version 1); this build "
                   "reads version 3");
}

TEST_F(Refused, AStateForAnotherRom) {
    GameBoy other;
    ASSERT_EQ(other.load_rom(counter_rom(0x03, 0x02, "OTHERGAME")), "");
    expect_refused(other.save_state(), "the save state is for another ROM (\"OTHERGAME\")");
    Bytes checksum = good_;
    checksum[12] ^= 0x01;
    expect_refused(checksum, "the save state is for another ROM (\"STATETEST\")");
    Bytes global = good_;
    global[15] ^= 0x01;
    expect_refused(global, "the save state is for another ROM (\"STATETEST\")");
}

TEST_F(Refused, AnyNonZeroReservedByte) {
    for (const std::size_t at :
         {std::size_t{13}, std::size_t{36}, std::size_t{41}, std::size_t{47}}) {
        Bytes state = good_;
        state[at] = 0x01;
        expect_refused(state, "the save state has non-zero reserved header bytes");
    }
}

TEST_F(Refused, ADamagedOrTruncatedPayload) {
    Bytes flipped = good_;
    flipped[good_.size() / 2] ^= 0x10;
    expect_refused(flipped, "the save state is damaged (its CRC-32 does not match)");
    expect_refused(Bytes(good_.begin(), good_.end() - 1),
                   "the save state is damaged (its CRC-32 does not match)");
}

TEST_F(Refused, AValueASectionDoesNotAllowEvenWithAMatchingCrc) {
    Bytes ime = good_;
    ime[payload_of("CPU ") + 12] = 3;  // IME: 0, 1 or 2
    fix_crc(ime);
    expect_refused(ime, "the save state was refused: a value in section CPU  is out of range");

    Bytes step = good_;
    step[payload_of("APU ") + 23 + 16 + 1] = 8;  // the frame-sequencer step: 0-7
    fix_crc(step);
    expect_refused(step, "the save state was refused: a value in section APU  is out of range");

    // A bad value late in the file, after CPU, BUS and the rest were loaded:
    // they must be put back.
    Bytes mode = good_;
    mode[payload_of("PPU ") + 8192 + 160 + 10 + 12] = 4;  // the PPU mode: 0-3
    fix_crc(mode);
    expect_refused(mode, "the save state was refused: a value in section PPU  is out of range");
}

TEST_F(Refused, ASectionOfTheWrongLength) {
    Bytes state = good_;
    state[payload_of("TIMR") - 4] = 8;  // the length field: 7 -> 8
    fix_crc(state);
    // TIMR now claims the first byte of the next section's tag.
    EXPECT_NE(gb_.load_state(state), "");
    EXPECT_EQ(gb_.save_state(), before_);
}

TEST_F(Refused, AMissingSection) {
    Bytes state = good_;
    const std::size_t cart = payload_of("CART") - 8;
    state.resize(cart);  // drop CART
    fix_crc(state);
    expect_refused(state, "the save state was refused: section CART is missing");
}

// --- Reset (R in the window): off and on again, with the battery's contents ---

TEST(Reset, StartsTheMachineAfreshButKeepsBatteryRamAndTheClock) {
    const Bytes rom = counter_rom(0x10, 0x02);  // MBC3 + timer + RAM + battery
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    Bytes ram(gb.battery_ram().size());
    for (std::size_t i = 0; i < ram.size(); ++i) {
        ram[i] = static_cast<std::uint8_t>(i * 7);
    }
    ASSERT_TRUE(gb.load_battery_ram(ram));
    gb.set_audio_enabled(true);
    gb.run_for(3 * GameBoy::kTicksPerSecond + 12'345);
    const auto clock = gb.clock()->live();
    const std::uint32_t sub_second = gb.clock()->sub_second();
    ASSERT_NE(gb.bus().read8(0xC000), 0);

    gb.reset();
    GameBoy fresh;
    ASSERT_EQ(fresh.load_rom(rom), "");
    EXPECT_EQ(gb.cycles(), 0U);
    EXPECT_EQ(gb.cpu().registers().pc, fresh.cpu().registers().pc);
    EXPECT_EQ(gb.bus().read8(0xC000), fresh.bus().read8(0xC000));
    EXPECT_TRUE(
        std::equal(ram.begin(), ram.end(), gb.battery_ram().begin(), gb.battery_ram().end()));
    EXPECT_EQ(gb.clock()->live(), clock);
    EXPECT_EQ(gb.clock()->sub_second(), sub_second);
    EXPECT_EQ(gb.rom_title(), "STATETEST");

    // Sound output is still on, and the machine runs as a fresh one does.
    gb.run_for(GameBoy::kTicksPerSecond / 10);
    std::vector<float> sound;
    gb.take_audio(sound);
    EXPECT_FALSE(sound.empty());
    fresh.run_for(GameBoy::kTicksPerSecond / 10);
    EXPECT_EQ(gb.cpu().registers().pc, fresh.cpu().registers().pc);
    EXPECT_EQ(gb.bus().read8(0xC000), fresh.bus().read8(0xC000));
}

// Changes not yet written to the save file must still be written after a
// reset; a reset alone is not a change.
TEST(Reset, KeepsWhetherBatteryRamHasUnsavedChanges) {
    // MBC1 + RAM + battery; the program enables RAM and writes 0x42 to A000.
    Bytes rom = test::make_rom({
        0x3E, 0x0A,        // LD A, 0x0A
        0xEA, 0x00, 0x00,  // LD [0x0000], A   ; enable RAM
        0x3E, 0x42,        // LD A, 0x42
        0xEA, 0x00, 0xA0,  // LD [0xA000], A
        0x18, 0xFE,        // JR -2
    });
    rom[kType] = 0x03;
    rom[kRamSizeCode] = 0x02;
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    gb.run_for(100);
    ASSERT_TRUE(gb.battery_ram_changed());
    gb.reset();
    EXPECT_TRUE(gb.battery_ram_changed());
    EXPECT_EQ(gb.battery_ram()[0], 0x42);
    gb.clear_battery_ram_changed();
    gb.reset();
    EXPECT_FALSE(gb.battery_ram_changed());
    EXPECT_EQ(gb.battery_ram()[0], 0x42);
}

TEST(Reset, DoesNothingBeforeARomIsLoaded) {
    GameBoy gb;
    gb.reset();
    EXPECT_EQ(gb.cycles(), 0U);
    EXPECT_EQ(gb.rom_title(), "");
}

// --- Every mapper register and the joypad come back in a fresh machine ---
//
// A field left out of both save and load would still give byte-identical
// round trips, so these check behaviour: a program sets non-default values,
// and a fresh GameBoy loaded from the state must read the same banks.

constexpr std::size_t kBankSize = 0x4000;
constexpr std::size_t kMarker = 0x1000;  // where each bank holds its number

// `banks` 16 KiB banks, each holding its number (low, high byte) at offset
// 0x1000, with `program` at 0x0100.
Bytes banked_rom(std::size_t banks, std::uint8_t type, std::uint8_t rom_code, std::uint8_t ram_code,
                 std::initializer_list<std::uint8_t> program) {
    Bytes rom = test::make_rom(program);
    rom.resize(banks * kBankSize, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[(bank * kBankSize) + kMarker] = static_cast<std::uint8_t>(bank);
        rom[(bank * kBankSize) + kMarker + 1] = static_cast<std::uint8_t>(bank >> 8U);
    }
    rom[kType] = type;
    rom[0x0148] = rom_code;
    rom[kRamSizeCode] = ram_code;
    return rom;
}

std::uint16_t bank_at(const GameBoy& gb, std::uint16_t window) {
    const std::uint16_t at = window + kMarker;
    return static_cast<std::uint16_t>(gb.bus().read8(at) | (gb.bus().read8(at + 1) << 8U));
}

// Runs the program, then loads its state into a fresh GameBoy.
void run_then_reload(const Bytes& rom, GameBoy& fresh) {
    GameBoy gb;
    start(gb, rom, 10'000);
    ASSERT_EQ(fresh.load_rom(rom), "");
    ASSERT_EQ(fresh.load_state(gb.save_state()), "");
}

TEST(SaveState, Mbc1RegistersAreInTheState) {
    // RAM on, bank1 = 5, bank2 = 1, mode 1, then 0x42 written to RAM. With a
    // 512 KiB ROM, bank2 in mode 1 selects RAM bank 1 (and leaves the ROM
    // banks alone, so the program keeps running).
    const Bytes rom =
        banked_rom(32, 0x03, 0x04, 0x03, {0x3E, 0x0A, 0xEA, 0x00, 0x00,  // RAM enable
                                          0x3E, 0x05, 0xEA, 0x00, 0x20,  // bank1 = 5
                                          0x3E, 0x01, 0xEA, 0x00, 0x40,  // bank2 = 1
                                          0x3E, 0x01, 0xEA, 0x00, 0x60,  // mode 1
                                          0x3E, 0x42, 0xEA, 0x00, 0xA0,  // [A000] = 0x42
                                          0x18, 0xFE});
    GameBoy fresh;
    run_then_reload(rom, fresh);
    EXPECT_EQ(bank_at(fresh, 0x4000), 0x05);
    EXPECT_EQ(fresh.bus().read8(0xA000), 0x42);  // RAM bank 1: needs bank2 and mode 1
    EXPECT_EQ(fresh.battery_ram()[0x2000], 0x42);
}

TEST(SaveState, Mbc3RegistersAreInTheState) {
    // RAM and clock on, ROM bank 0x25, clock register 0x0A (hours) selected
    // after setting it to 7.
    const Bytes rom =
        banked_rom(64, 0x10, 0x05, 0x03, {0x3E, 0x0A, 0xEA, 0x00, 0x00,  // enable
                                          0x3E, 0x25, 0xEA, 0x00, 0x20,  // ROM bank 0x25
                                          0x3E, 0x0A, 0xEA, 0x00, 0x40,  // select hours
                                          0x3E, 0x07, 0xEA, 0x00, 0xA0,  // hours = 7
                                          0x18, 0xFE});
    GameBoy fresh;
    run_then_reload(rom, fresh);
    EXPECT_EQ(bank_at(fresh, 0x4000), 0x25);
    EXPECT_EQ(fresh.bus().read8(0xA000), 7);  // enabled, and the select points at H
}

TEST(SaveState, Mbc5RegistersAreInTheState) {
    // RAM on, ROM bank 0x105 (bit 8 set), RAM bank 2 holding 0x42.
    const Bytes rom =
        banked_rom(512, 0x1B, 0x08, 0x03, {0x3E, 0x0A, 0xEA, 0x00, 0x00,  // RAM enable
                                           0x3E, 0x05, 0xEA, 0x00, 0x20,  // ROM bank low = 5
                                           0x3E, 0x01, 0xEA, 0x00, 0x30,  // ROM bank bit 8
                                           0x3E, 0x02, 0xEA, 0x00, 0x40,  // RAM bank 2
                                           0x3E, 0x42, 0xEA, 0x00, 0xA0,  // [A000] = 0x42
                                           0x18, 0xFE});
    GameBoy fresh;
    run_then_reload(rom, fresh);
    EXPECT_EQ(bank_at(fresh, 0x4000), 0x105);
    EXPECT_EQ(fresh.bus().read8(0xA000), 0x42);
    EXPECT_EQ(fresh.battery_ram()[std::size_t{2} * 0x2000], 0x42);
}

TEST(SaveState, AHeldButtonIsInTheState) {
    GameBoy gb;
    start(gb, counter_rom(), 1000);
    gb.set_button(core::Joypad::Button::A, true);
    GameBoy fresh;
    ASSERT_EQ(fresh.load_rom(counter_rom()), "");
    ASSERT_EQ(fresh.load_state(gb.save_state()), "");
    // After boot both groups are selected, so A shows as bit 0 low.
    EXPECT_EQ(fresh.bus().read8(0xFF00) & 0x01, 0);
    EXPECT_EQ(gb.bus().read8(0xFF00), fresh.bus().read8(0xFF00));
}

}  // namespace
