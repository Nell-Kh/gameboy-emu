// Save-state determinism on real ROMs (ADR-020; docs/cartridge-notes.md,
// section 7). A loaded state must continue exactly as the machine it was
// saved from: same frame, registers, tick count, sound samples and every
// other byte of state, given the same input.
//
// The ROMs: Blargg's combined cpu_instrs (MBC1, timer and serial), rtc3test
// played through its menu (input, the MBC3 clock, screen updates), Blargg's
// combined dmg_sound (every sound channel busy), and two Mooneye ROMs that
// keep OAM DMA and the PPU busy, saved at many points in the middle of
// transfers and lines. The design named Libbet's demo; Libbet is built by
// hand and is not in the CI test-ROM collection, so rtc3test is the
// input-driven ROM here.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "core/gameboy.h"
#include "screenshot.h"

namespace {

using core::GameBoy;
using Button = core::Joypad::Button;
using Bytes = std::vector<std::uint8_t>;

Bytes rom_at(const std::string& path) {
    Bytes rom = test::read_file(path);
    EXPECT_FALSE(rom.empty()) << "cannot read " << path;
    return rom;
}

// Input for frames [first, first + count): button presses by frame number,
// each held for kHoldFrames frames.
struct Press {
    int frame;
    Button button;
};
constexpr int kHoldFrames = 5;

// With `sound`, the samples of every frame are collected there (the core
// keeps at most a second of them).
void run_frames(GameBoy& gb, int first, int count, std::initializer_list<Press> presses = {},
                std::vector<float>* sound = nullptr) {
    for (int frame = first; frame < first + count; ++frame) {
        for (const Press& press : presses) {
            if (frame == press.frame) {
                gb.set_button(press.button, true);
            }
            if (frame == press.frame + kHoldFrames) {
                gb.set_button(press.button, false);
            }
        }
        gb.run_frame();
        if (sound != nullptr) {
            gb.take_audio(*sound);
        }
    }
}

// Everything that must match after the same run from the same state.
void expect_same_machine(const GameBoy& a, const GameBoy& b) {
    EXPECT_EQ(a.cycles(), b.cycles());
    const auto& ra = a.cpu().registers();
    const auto& rb = b.cpu().registers();
    EXPECT_EQ(ra.pc, rb.pc);
    EXPECT_EQ(ra.sp, rb.sp);
    EXPECT_EQ(ra.af(), rb.af());
    EXPECT_EQ(ra.bc(), rb.bc());
    EXPECT_EQ(ra.de(), rb.de());
    EXPECT_EQ(ra.hl(), rb.hl());
    EXPECT_TRUE(a.frame() == b.frame()) << "the frames differ";
    EXPECT_TRUE(a.save_state() == b.save_state()) << "the states differ";
}

// Runs `n` frames, saves, runs `m` more frames and keeps the machine; loads
// the state and runs the same `m` frames with the same input; both must end
// identical. `presses` may fall before or after the save. The sound of the
// `m` frames must match too: the sound output is not in the state (it
// belongs to the window), so it is switched on at the save on both sides,
// the second being a fresh machine. That sound is left in `sound_out`.
void check_replay(const Bytes& rom, int n, int m, std::initializer_list<Press> presses,
                  std::vector<float>* sound_out = nullptr) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    run_frames(gb, 0, n, presses);
    const Bytes saved = gb.save_state();
    const std::size_t serial_at_save = gb.serial_output().size();

    gb.set_audio_enabled(true);
    std::vector<float> sound_first;
    run_frames(gb, n, m, presses, &sound_first);
    const std::string serial_first = gb.serial_output().substr(serial_at_save);
    GameBoy first;  // the machine at the end of the first run, as a state
    ASSERT_EQ(first.load_rom(rom), "");
    ASSERT_EQ(first.load_state(gb.save_state()), "");

    ASSERT_EQ(gb.load_state(saved), "");
    const std::size_t serial_at_load = gb.serial_output().size();
    run_frames(gb, n, m, presses);
    EXPECT_EQ(gb.serial_output().substr(serial_at_load), serial_first);
    expect_same_machine(gb, first);

    GameBoy replay;
    ASSERT_EQ(replay.load_rom(rom), "");
    ASSERT_EQ(replay.load_state(saved), "");
    replay.set_audio_enabled(true);
    std::vector<float> sound_replay;
    run_frames(replay, n, m, presses, &sound_replay);
    EXPECT_EQ(sound_replay.size(), sound_first.size());
    EXPECT_TRUE(sound_replay == sound_first) << "the sound differs";
    expect_same_machine(replay, first);
    if (sound_out != nullptr) {
        *sound_out = std::move(sound_first);
    }
}

TEST(SaveStateDeterminism, BlarggCpuInstrsContinuesIdentically) {
    const Bytes rom = rom_at(std::string(GB_CSP_DIR) + "/blargg/cpu_instrs/cpu_instrs.gb");
    check_replay(rom, 120, 300, {});
}

// Every channel is busy in dmg_sound, so a channel's counter left out of the
// state would show in the sound after the load.
TEST(SaveStateDeterminism, BlarggDmgSoundContinuesWithTheSameSound) {
    const Bytes rom = rom_at(std::string(GB_CSP_DIR) + "/blargg/dmg_sound/dmg_sound.gb");
    std::vector<float> sound;
    check_replay(rom, 120, 300, {}, &sound);
    EXPECT_TRUE(std::any_of(sound.begin(), sound.end(), [](float s) { return s != 0.0F; }))
        << "the sound is silence";
}

TEST(SaveStateDeterminism, Rtc3TestWithInputContinuesIdentically) {
    const Bytes rom = rom_at(std::string(GB_CSP_DIR) + "/rtc3test/rtc3test.gb");
    // Saved before the menu input (sub-second writes: Down, Down, A), then
    // run for 10 emulated seconds of the subtest.
    check_replay(rom, 40, 660, {{60, Button::Down}, {80, Button::Down}, {100, Button::A}});
}

// Save, load into a fresh GameBoy, save again: the two files must be
// byte-identical, at many points.
void check_round_trips(const Bytes& rom, int frames, int every,
                       std::initializer_list<Press> presses = {}) {
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    for (int frame = 0; frame < frames; frame += every) {
        run_frames(gb, frame, every, presses);
        const Bytes saved = gb.save_state();
        GameBoy fresh;
        ASSERT_EQ(fresh.load_rom(rom), "");
        ASSERT_EQ(fresh.load_state(saved), "") << "frame " << frame;
        ASSERT_TRUE(fresh.save_state() == saved) << "frame " << frame;
    }
}

TEST(SaveStateDeterminism, SaveLoadSaveIsByteIdentical) {
    check_round_trips(rom_at(std::string(GB_CSP_DIR) + "/blargg/cpu_instrs/cpu_instrs.gb"), 300,
                      30);
    check_round_trips(rom_at(std::string(GB_CSP_DIR) + "/rtc3test/rtc3test.gb"), 600, 25,
                      {{60, Button::A}});
}

// Saves at many points between instructions (in the middle of OAM DMA
// transfers, PPU lines and timer periods), and checks that a fresh GameBoy
// loaded from each state runs the next instructions exactly as the original.
void check_mid_run(const std::string& path) {
    constexpr int kPoints = 40;
    constexpr int kStepsBetweenPoints = 997;  // not a multiple of any period
    constexpr int kStepsAfter = 3000;
    const Bytes rom = rom_at(path);
    GameBoy gb;
    ASSERT_EQ(gb.load_rom(rom), "");
    for (int point = 0; point < kPoints; ++point) {
        for (int i = 0; i < kStepsBetweenPoints; ++i) {
            gb.step();
        }
        const Bytes saved = gb.save_state();
        GameBoy fresh;
        ASSERT_EQ(fresh.load_rom(rom), "");
        ASSERT_EQ(fresh.load_state(saved), "");
        for (int i = 0; i < kStepsAfter; ++i) {
            gb.step();
            fresh.step();
        }
        ASSERT_TRUE(gb.save_state() == fresh.save_state()) << path << ", point " << point;
    }
}

TEST(SaveStateDeterminism, StatesSavedMidDmaAndMidLineContinueIdentically) {
    check_mid_run(std::string(GB_MOONEYE_DIR) + "/acceptance/oam_dma_restart.gb");
    check_mid_run(std::string(GB_MOONEYE_DIR) + "/acceptance/ppu/intr_2_mode0_timing_sprites.gb");
}

}  // namespace
