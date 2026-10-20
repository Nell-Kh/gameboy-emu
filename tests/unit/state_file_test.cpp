#include "app/state_file.h"

#include <gtest/gtest.h>

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
using frontend::StateSlot;
using Bytes = std::vector<std::uint8_t>;

// A fresh, empty directory per test, removed afterwards.
class StateFile : public testing::Test {
protected:
    void SetUp() override {
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("peasoup-") + info->test_suite_name() + "-" + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        ASSERT_EQ(gb_.load_rom(rom()), "");
    }
    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    // Increments 0xC000 forever.
    static Bytes rom() {
        return test::make_rom({0x21, 0x00, 0xC0, 0x34, 0x18, 0xFD});
    }

    [[nodiscard]] fs::path path() const {
        return dir_ / "game.state";
    }

    static Bytes read_file(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    core::GameBoy gb_;
    std::ostringstream log_;

private:
    fs::path dir_;
};

TEST(StatePath, ReplacesTheExtensionWithState) {
    EXPECT_EQ(frontend::state_path_for("roms/game.gb"), fs::path("roms/game.state"));
    EXPECT_EQ(frontend::state_path_for("game"), fs::path("game.state"));
}

TEST_F(StateFile, SaveWritesTheStateAndLeavesNoTemporaryFile) {
    gb_.run_for(10'000);
    StateSlot slot(gb_, path(), log_);
    ASSERT_TRUE(slot.save());
    EXPECT_EQ(read_file(path()), gb_.save_state());
    EXPECT_FALSE(fs::exists(fs::path(path()) += ".tmp"));
    EXPECT_EQ(log_.str(), "peasoup: state saved to " + path().string() + "\n");
}

TEST_F(StateFile, LoadPutsTheMachineBack) {
    gb_.run_for(10'000);
    StateSlot slot(gb_, path(), log_);
    ASSERT_TRUE(slot.save());
    const Bytes saved = gb_.save_state();
    gb_.run_for(50'000);
    ASSERT_TRUE(slot.load());
    EXPECT_EQ(gb_.save_state(), saved);
    EXPECT_NE(log_.str().find("state loaded from"), std::string::npos) << log_.str();
}

TEST_F(StateFile, LoadWithNoFileSaysSoAndChangesNothing) {
    gb_.run_for(10'000);
    const Bytes before = gb_.save_state();
    StateSlot slot(gb_, path(), log_);
    EXPECT_FALSE(slot.load());
    EXPECT_EQ(gb_.save_state(), before);
    EXPECT_EQ(log_.str(), "peasoup: no saved state at " + path().string() + " (F5 saves one)\n");
}

TEST_F(StateFile, ARefusedFileIsReportedAndChangesNothing) {
    {
        std::ofstream file(path(), std::ios::binary);
        file << "not a save state at all, but long enough to have a header......";
    }
    gb_.run_for(10'000);
    const Bytes before = gb_.save_state();
    StateSlot slot(gb_, path(), log_);
    EXPECT_FALSE(slot.load());
    EXPECT_EQ(gb_.save_state(), before);
    EXPECT_EQ(log_.str(),
              "peasoup: " + path().string() + " not loaded: the file is not a save state\n");
}

TEST_F(StateFile, SaveReportsAFileThatCannotBeWritten) {
    StateSlot slot(gb_, path() / "no" / "such" / "dir.state", log_);
    EXPECT_FALSE(slot.save());
    EXPECT_NE(log_.str().find("state not saved: cannot write"), std::string::npos) << log_.str();
}

}  // namespace
