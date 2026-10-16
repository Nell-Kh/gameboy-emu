#include "app/save_file.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "test_rom.h"

namespace {

namespace fs = std::filesystem;
using frontend::BatterySaver;
using frontend::ClockFooter;
using frontend::SaveRead;
using namespace std::chrono_literals;

constexpr std::size_t kRamSize = 0x2000;

// A fresh, empty directory per test, removed afterwards.
class SaveFile : public testing::Test {
protected:
    void SetUp() override {
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("gameboy-emu-") + info->test_suite_name() + "-" + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    [[nodiscard]] fs::path path(const std::string& name) const {
        return dir_ / name;
    }

    static void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }

    static std::vector<std::uint8_t> read_file(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

private:
    fs::path dir_;
};

// MBC1 + RAM + battery, 8 KiB RAM. The program enables RAM, then either adds
// one to 0xA000 once (`forever` false) or keeps adding one in a loop.
std::vector<std::uint8_t> counter_rom(bool forever) {
    auto rom = test::make_rom({
        0x3E, 0x0A,                                        // LD A, 0x0A
        0xEA, 0x00, 0x00,                                  // LD [0x0000], A   ; enable RAM
        0xFA, 0x00, 0xA0,                                  // 0x0105: LD A, [0xA000]
        0x3C,                                              // INC A
        0xEA, 0x00, 0xA0,                                  // LD [0xA000], A
        0x18,                                              // JR ...
        static_cast<std::uint8_t>(forever ? 0xF7 : 0xFE),  // back to 0x0105, or to itself
    });
    rom[0x0147] = 0x03;
    rom[0x0149] = 0x02;
    return rom;
}

TEST(SavePath, ReplacesTheExtensionWithSav) {
    EXPECT_EQ(frontend::save_path_for("roms/game.gb"), fs::path("roms/game.sav"));
    EXPECT_EQ(frontend::save_path_for("game"), fs::path("game.sav"));
    EXPECT_EQ(frontend::save_path_for("my.roms/game.gbc"), fs::path("my.roms/game.sav"));
}

TEST_F(SaveFile, AMissingFileIsNoFile) {
    EXPECT_EQ(frontend::read_save(path("none.sav"), kRamSize).status, SaveRead::Status::NoFile);
}

TEST_F(SaveFile, WriteThenReadGivesTheSameBytesAndLeavesNoTemporaryFile) {
    std::vector<std::uint8_t> ram(kRamSize);
    for (std::size_t i = 0; i < ram.size(); ++i) {
        ram[i] = static_cast<std::uint8_t>(i * 7);
    }
    ASSERT_EQ(frontend::write_save(path("game.sav"), ram), "");
    EXPECT_FALSE(fs::exists(path("game.sav.tmp")));
    const SaveRead read = frontend::read_save(path("game.sav"), kRamSize);
    ASSERT_EQ(read.status, SaveRead::Status::Loaded);
    EXPECT_EQ(read.ram, ram);
}

TEST_F(SaveFile, WritingReplacesAnOlderSave) {
    ASSERT_EQ(frontend::write_save(path("game.sav"), std::vector<std::uint8_t>(kRamSize, 1)), "");
    ASSERT_EQ(frontend::write_save(path("game.sav"), std::vector<std::uint8_t>(kRamSize, 2)), "");
    EXPECT_EQ(read_file(path("game.sav")), std::vector<std::uint8_t>(kRamSize, 2));
}

TEST_F(SaveFile, AFileOfTheWrongSizeIsRefusedWithTheReason) {
    write_file(path("game.sav"), std::vector<std::uint8_t>(100, 0x55));
    const SaveRead read = frontend::read_save(path("game.sav"), kRamSize);
    EXPECT_EQ(read.status, SaveRead::Status::WrongSize);
    EXPECT_TRUE(read.ram.empty());
    EXPECT_NE(read.message.find("100 bytes"), std::string::npos) << read.message;
}

TEST_F(SaveFile, SaverLoadsAnExistingSave) {
    std::vector<std::uint8_t> save(kRamSize, 0x00);
    save[0] = 41;
    write_file(path("game.sav"), save);
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom(false)), "");
    std::ostringstream log;
    const BatterySaver saver(gb, path("game.sav"), log);
    EXPECT_TRUE(saver.active());
    EXPECT_EQ(gb.battery_ram()[0], 41);
    gb.run_for(1000);
    EXPECT_EQ(gb.battery_ram()[0], 42);
    EXPECT_EQ(log.str(), "");
}

TEST_F(SaveFile, SaverDoesNothingWithoutBatteryRam) {
    auto rom = counter_rom(true);
    rom[0x0147] = 0x02;  // MBC1 + RAM, no battery
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log);
    EXPECT_FALSE(saver.active());
    gb.run_for(1000);
    saver.tick(BatterySaver::Clock::now());
    saver.flush();
    EXPECT_FALSE(fs::exists(path("game.sav")));
}

TEST_F(SaveFile, SaverNeverOverwritesAFileItRefused) {
    const std::vector<std::uint8_t> foreign(100, 0x55);
    write_file(path("game.sav"), foreign);
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom(true)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log);
    EXPECT_FALSE(saver.active());
    EXPECT_NE(log.str().find("will not be overwritten"), std::string::npos) << log.str();
    gb.run_for(1000);
    saver.tick(BatterySaver::Clock::now());
    saver.flush();
    EXPECT_EQ(read_file(path("game.sav")), foreign);
}

TEST_F(SaveFile, SaverWritesAtMostOnceASecondWhileRamChanges) {
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom(true)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log);
    const auto start = BatterySaver::Clock::now();

    gb.run_for(100);
    saver.tick(start);  // first change: written at once
    const std::uint8_t first = read_file(path("game.sav")).at(0);
    EXPECT_NE(first, 0);

    gb.run_for(100);
    saver.tick(start + 500ms);  // changed again, but too soon
    EXPECT_EQ(read_file(path("game.sav")).at(0), first);

    saver.tick(start + 1s);  // a second later: written
    EXPECT_EQ(read_file(path("game.sav")).at(0), gb.battery_ram()[0]);
    EXPECT_NE(gb.battery_ram()[0], first);
}

TEST_F(SaveFile, SaverDoesNotWriteWhenNothingChanged) {
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom(false)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log);
    saver.tick(BatterySaver::Clock::now());  // nothing has run yet
    saver.flush();
    EXPECT_FALSE(fs::exists(path("game.sav")));
}

TEST_F(SaveFile, FlushWritesTheLatestRam) {
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(counter_rom(false)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log);
    gb.run_for(1000);
    saver.flush();
    const auto saved = read_file(path("game.sav"));
    ASSERT_EQ(saved.size(), kRamSize);
    EXPECT_EQ(saved[0], 1);
}

// The clock footer (BGB's format) and the clock in the save file.

using Registers = core::Rtc::Registers;
constexpr std::int64_t kSavedAt = 1'700'000'000;  // a Unix time in 2023
constexpr std::uint64_t kDay = 86'400;

TEST(ClockFooter, EncodesEachRegisterAsA32BitFieldThenThe64BitTime) {
    const ClockFooter footer{{1, 2, 3, 4, 0xC1}, {5, 6, 7, 8, 0x40}, 0x0102030405060708};
    const auto bytes = frontend::encode_clock_footer(footer);
    std::vector<std::uint8_t> expected(48, 0x00);
    const std::vector<std::uint8_t> values = {1, 2, 3, 4, 0xC1, 5, 6, 7, 8, 0x40};
    for (std::size_t i = 0; i < values.size(); ++i) {
        expected[i * 4] = values[i];
    }
    const std::vector<std::uint8_t> time = {8, 7, 6, 5, 4, 3, 2, 1};
    std::copy(time.begin(), time.end(), expected.begin() + 40);
    EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), expected);
}

TEST(ClockFooter, DecodesWhatItEncodes) {
    const ClockFooter footer{{59, 59, 23, 0xFF, 0x81}, {1, 2, 3, 4, 0}, kSavedAt};
    const auto bytes = frontend::encode_clock_footer(footer);
    const auto result = frontend::decode_clock_footer(bytes);
    ASSERT_TRUE(result.has_value());
    const ClockFooter decoded = result.value_or(ClockFooter{});
    EXPECT_EQ(decoded.live, footer.live);
    EXPECT_EQ(decoded.latched, footer.latched);
    EXPECT_EQ(decoded.saved_at, kSavedAt);
}

TEST(ClockFooter, Accepts44BytesWithA32BitTimeAndIgnoresTheHighBytesOfEachField) {
    std::vector<std::uint8_t> bytes(44, 0x00);
    bytes[0] = 30;    // live seconds
    bytes[1] = 0xEE;  // not part of the value
    bytes[20] = 31;   // latched seconds
    bytes[40] = 0x00;
    bytes[41] = 0xF1;
    bytes[42] = 0x53;
    bytes[43] = 0x65;  // 0x6553F100 = 1700000000
    const auto result = frontend::decode_clock_footer(bytes);
    ASSERT_TRUE(result.has_value());
    const ClockFooter decoded = result.value_or(ClockFooter{});
    EXPECT_EQ(decoded.live[0], 30);
    EXPECT_EQ(decoded.latched[0], 31);
    EXPECT_EQ(decoded.saved_at, kSavedAt);
}

TEST(ClockFooter, RefusesAnyOtherSize) {
    for (const std::size_t size : {0, 40, 43, 45, 47, 49}) {
        EXPECT_FALSE(frontend::decode_clock_footer(std::vector<std::uint8_t>(size)).has_value())
            << size;
    }
}

TEST_F(SaveFile, WithAClockTheFooterFollowsTheRam) {
    const std::vector<std::uint8_t> ram(kRamSize, 0x5A);
    const ClockFooter footer{{1, 2, 3, 4, 0}, {1, 2, 3, 4, 0}, kSavedAt};
    ASSERT_EQ(frontend::write_save(path("game.sav"), ram, &footer), "");
    const auto saved = read_file(path("game.sav"));
    ASSERT_EQ(saved.size(), kRamSize + 48);
    EXPECT_EQ(saved[kRamSize - 1], 0x5A);
    EXPECT_EQ(saved[kRamSize], 1);

    const SaveRead read = frontend::read_save(path("game.sav"), kRamSize, true);
    ASSERT_EQ(read.status, SaveRead::Status::Loaded);
    EXPECT_EQ(read.ram, ram);
    ASSERT_TRUE(read.clock.has_value());
    const ClockFooter clock = read.clock.value_or(ClockFooter{});
    EXPECT_EQ(clock.live, footer.live);
    EXPECT_EQ(clock.saved_at, kSavedAt);
}

TEST_F(SaveFile, WithAClockOnlyRamPlus44Or48BytesIsTheRightSize) {
    for (const std::size_t extra : {0, 1, 40, 47, 49, 96}) {
        write_file(path("game.sav"), std::vector<std::uint8_t>(kRamSize + extra));
        const SaveRead read = frontend::read_save(path("game.sav"), kRamSize, true);
        EXPECT_EQ(read.status, SaveRead::Status::WrongSize) << extra;
        EXPECT_NE(read.message.find("8240 or 8236 bytes"), std::string::npos) << read.message;
    }
    write_file(path("game.sav"), std::vector<std::uint8_t>(kRamSize + 44));
    EXPECT_EQ(frontend::read_save(path("game.sav"), kRamSize, true).status,
              SaveRead::Status::Loaded);
}

// MBC3 + timer + battery (0x0F, no RAM) or with 8 KiB RAM (0x10), running a
// program that loops forever and touches nothing.
std::vector<std::uint8_t> clock_rom(bool with_ram) {
    auto rom = test::make_rom({0x18, 0xFE});  // JR -2
    rom[0x0147] = with_ram ? 0x10 : 0x0F;
    rom[0x0149] = with_ram ? 0x02 : 0x00;
    return rom;
}

void write_clock_save(const fs::path& path, std::size_t ram_size, const ClockFooter& footer) {
    ASSERT_EQ(frontend::write_save(path, std::vector<std::uint8_t>(ram_size, 0x00), &footer), "");
}

TEST_F(SaveFile, SaverAddsTheSecondsSinceTheSaveToTheClock) {
    // 1 day, 1 hour, 1 minute and 1 second later.
    write_clock_save(path("game.sav"), kRamSize, {{0, 0, 0, 0, 0}, {9, 9, 9, 9, 0}, kSavedAt});
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(clock_rom(true)), "");
    std::ostringstream log;
    const BatterySaver saver(gb, path("game.sav"), log, [] { return kSavedAt + 90'061; });
    EXPECT_TRUE(saver.active());
    ASSERT_NE(gb.clock(), nullptr);
    EXPECT_EQ(gb.clock()->live(), (Registers{1, 1, 1, 1, 0}));
    EXPECT_EQ(gb.clock()->latched(), (Registers{9, 9, 9, 9, 0}));  // as saved
    EXPECT_EQ(log.str(), "");
}

TEST_F(SaveFile, SaverCatchUpPassingDay511SetsTheCarry) {
    write_clock_save(path("game.sav"), 0, {{0, 0, 12, 0xFF, 0x01}, {}, kSavedAt});
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(clock_rom(false)), "");
    std::ostringstream log;
    const BatterySaver saver(gb, path("game.sav"), log, [] { return kSavedAt + kDay; });
    EXPECT_EQ(gb.clock()->live(), (Registers{0, 0, 12, 0x00, 0x80}));
}

TEST_F(SaveFile, SaverLeavesAHaltedClockAndIgnoresATimeInTheFuture) {
    write_clock_save(path("game.sav"), 0, {{1, 2, 3, 4, 0x40}, {}, kSavedAt});
    core::GameBoy halted;
    ASSERT_EQ(halted.load_rom(clock_rom(false)), "");
    std::ostringstream log;
    const BatterySaver halted_saver(halted, path("game.sav"), log, [] { return kSavedAt + kDay; });
    EXPECT_EQ(halted.clock()->live(), (Registers{1, 2, 3, 4, 0x40}));

    write_clock_save(path("game.sav"), 0, {{1, 2, 3, 4, 0}, {}, kSavedAt});
    core::GameBoy future;
    ASSERT_EQ(future.load_rom(clock_rom(false)), "");
    const BatterySaver future_saver(future, path("game.sav"), log, [] { return kSavedAt - 100; });
    EXPECT_EQ(future.clock()->live(), (Registers{1, 2, 3, 4, 0}));
}

TEST_F(SaveFile, AClockWithNoRamIsSavedAndSurvivesQuitAndRelaunch) {
    std::int64_t now = kSavedAt;
    const auto unix_now = [&now] { return now; };
    {
        core::GameBoy gb;
        ASSERT_EQ(gb.load_rom(clock_rom(false)), "");
        std::ostringstream log;
        BatterySaver saver(gb, path("game.sav"), log, unix_now);
        EXPECT_TRUE(saver.active());
        gb.run_for(3 * core::GameBoy::kTicksPerSecond);
        saver.flush();  // the clock moved, so the exit writes even with no RAM change
    }
    const auto saved = read_file(path("game.sav"));
    ASSERT_EQ(saved.size(), 48U);
    EXPECT_EQ(saved[0], 3);  // live seconds
    EXPECT_EQ(frontend::decode_clock_footer(saved).value_or(ClockFooter{}).saved_at, kSavedAt);

    now = kSavedAt + 10;  // closed for ten seconds
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(clock_rom(false)), "");
    std::ostringstream log;
    const BatterySaver saver(gb, path("game.sav"), log, unix_now);
    EXPECT_EQ(gb.clock()->live()[0], 13);
}

TEST_F(SaveFile, SaverNeverOverwritesAClockSaveOfTheWrongSize) {
    const std::vector<std::uint8_t> ram_only(kRamSize, 0x55);  // no footer
    write_file(path("game.sav"), ram_only);
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(clock_rom(true)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log, [] { return kSavedAt; });
    EXPECT_FALSE(saver.active());
    gb.run_for(1000);
    saver.flush();
    EXPECT_EQ(read_file(path("game.sav")), ram_only);
}

TEST_F(SaveFile, AShort44ByteFooterIsReadAndTheNextSaveIs48Bytes) {
    std::vector<std::uint8_t> old(kRamSize + 44, 0x00);
    old[0] = 0x77;            // RAM
    old[kRamSize] = 20;       // live seconds
    old[kRamSize + 20] = 21;  // latched seconds
    const std::uint32_t saved_at = 1'700'000'000;
    for (std::size_t i = 0; i < 4; ++i) {
        old[kRamSize + 40 + i] = static_cast<std::uint8_t>(saved_at >> (8 * i));
    }
    write_file(path("game.sav"), old);
    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(clock_rom(true)), "");
    std::ostringstream log;
    BatterySaver saver(gb, path("game.sav"), log, [] { return kSavedAt; });
    ASSERT_TRUE(saver.active());
    EXPECT_EQ(gb.battery_ram()[0], 0x77);
    EXPECT_EQ(gb.clock()->live()[0], 20);
    EXPECT_EQ(gb.clock()->latched()[0], 21);
    EXPECT_EQ(read_file(path("game.sav")), old);  // reading does not touch the file

    saver.flush();
    const auto saved = read_file(path("game.sav"));
    ASSERT_EQ(saved.size(), kRamSize + 48);
    const ClockFooter footer =
        frontend::decode_clock_footer(std::span<const std::uint8_t>(saved).subspan(kRamSize))
            .value_or(ClockFooter{});
    EXPECT_EQ(saved[0], 0x77);
    EXPECT_EQ(footer.live[0], 20);
    EXPECT_EQ(footer.latched[0], 21);
    EXPECT_EQ(footer.saved_at, kSavedAt);
}

// The file pairs the clock with the moment it was written, so a crash after a
// write loses nothing: the catch-up from that write lands where a clean exit
// would have left the clock (with the emulator running at real speed).
//
// This test has teeth: stamping the footer 3 seconds early in
// BatterySaver::write() (`unix_now_() - 3`) makes it fail, because the
// catch-up then over-advances by those 3 seconds.
TEST_F(SaveFile, AfterACrashTheClockCatchesUpToWhereACleanExitWouldHaveLeftIt) {
    // MBC3 + timer + RAM + battery: writes one RAM byte, then loops.
    auto rom = test::make_rom({
        0x3E, 0x0A,        // LD A, 0x0A
        0xEA, 0x00, 0x00,  // LD [0x0000], A   ; enable RAM
        0x3E, 0x01,        // LD A, 1
        0xEA, 0x00, 0xA0,  // LD [0xA000], A
        0x18, 0xFE,        // JR -2
    });
    rom[0x0147] = 0x10;
    rom[0x0149] = 0x02;
    std::int64_t now = kSavedAt;
    const auto unix_now = [&now] { return now; };
    constexpr std::uint64_t kRunSeconds = 5;
    constexpr std::int64_t kClosedSeconds = 50;

    Registers at_crash{};
    {
        core::GameBoy gb;
        ASSERT_EQ(gb.load_rom(rom), "");
        std::ostringstream log;
        BatterySaver saver(gb, path("game.sav"), log, unix_now);
        gb.run_for(1000);                        // the RAM write
        saver.tick(BatterySaver::Clock::now());  // the last save before the crash
        ASSERT_TRUE(fs::exists(path("game.sav")));
        gb.run_for(kRunSeconds * core::GameBoy::kTicksPerSecond);
        now += static_cast<std::int64_t>(kRunSeconds);
        at_crash = gb.clock()->live();
        // No flush: the process dies here.
    }
    ASSERT_GE(at_crash[0], kRunSeconds);  // the clock ran on after the last save

    now += kClosedSeconds;
    core::Rtc clean_exit;  // where a clean exit at the crash would have led
    clean_exit.restore(at_crash, at_crash);
    clean_exit.advance_seconds(kClosedSeconds);

    core::GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    std::ostringstream log;
    const BatterySaver saver(gb, path("game.sav"), log, unix_now);
    EXPECT_EQ(gb.clock()->live(), clean_exit.live());
}

}  // namespace
