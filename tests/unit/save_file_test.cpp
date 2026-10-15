#include "app/save_file.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "core/gameboy.h"
#include "test_rom.h"

namespace {

namespace fs = std::filesystem;
using frontend::BatterySaver;
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

}  // namespace
