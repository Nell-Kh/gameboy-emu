// The SDL2 window: the only file in the project that knows about SDL.
//
// Each loop iteration handles input, runs the machine for one frame, shows
// that frame and queues its sound. With an audio device the queue sets the
// pace: a frame runs only while less than 50 ms of sound is waiting, so the
// game runs at the sound card's clock (ADR-025). Without one the loop waits
// for the wall clock instead, at the Game Boy's 59.73 frames per second
// (ADR-016).

#include "app/sdl_frontend.h"

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "app/dpad.h"
#include "app/state_file.h"
#include "core/apu.h"
#include "core/joypad.h"

namespace frontend {

namespace {

using Button = core::Joypad::Button;

constexpr int kWidth = core::GameBoy::kScreenWidth;
constexpr int kHeight = core::GameBoy::kScreenHeight;
constexpr int kExitOk = 0;
constexpr int kExitSdlError = 70;
// After a stall (the window being dragged, the machine sleeping) more than this
// many frames behind, start pacing afresh instead of racing to catch up.
constexpr std::uint64_t kMaxFramesBehind = 5;

// Shades 0 (lightest) to 3 as ARGB, in the green of the original screen.
constexpr std::array<std::uint32_t, 4> kPalette = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};

struct SdlDeleter {
    void operator()(SDL_Window* window) const noexcept {
        SDL_DestroyWindow(window);
    }
    void operator()(SDL_Renderer* renderer) const noexcept {
        SDL_DestroyRenderer(renderer);
    }
    void operator()(SDL_Texture* texture) const noexcept {
        SDL_DestroyTexture(texture);
    }
};

// The audio device, opened paused and fed through SDL's queue. Playback
// starts once kTargetQueuedBytes are waiting, so it never starts on an empty
// queue; after that, finding the queue empty means the device ran dry (an
// underrun, heard as a crackle), which is counted.
class AudioOut {
public:
    static constexpr std::uint32_t kTargetQueuedMs = 50;
    // 50 ms of stereo float samples, in bytes.
    static constexpr std::uint32_t kTargetQueuedBytes = core::Apu::kSampleRate * kTargetQueuedMs /
                                                        1000 * 2 *
                                                        static_cast<std::uint32_t>(sizeof(float));

    AudioOut() noexcept {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return;
        }
        SDL_AudioSpec want{};
        want.freq = static_cast<int>(core::Apu::kSampleRate);
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 512;
        SDL_AudioSpec have{};
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // SDL converts if needed
    }
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;
    AudioOut(AudioOut&&) = delete;
    AudioOut& operator=(AudioOut&&) = delete;
    ~AudioOut() {
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);
        }
    }

    [[nodiscard]] bool ok() const noexcept {
        return device_ != 0;
    }
    // True while enough sound is queued that the machine should wait.
    [[nodiscard]] bool full() const noexcept {
        return SDL_GetQueuedAudioSize(device_) >= kTargetQueuedBytes;
    }
    // Queues a frame's sound. Returns true if the queue had run dry first.
    bool queue(const std::vector<float>& samples) noexcept {
        const bool underrun = started_ && SDL_GetQueuedAudioSize(device_) == 0;
        if (underrun) {
            ++underruns_;
        }
        SDL_QueueAudio(device_, samples.data(),
                       static_cast<Uint32>(samples.size() * sizeof(float)));
        if (!started_ && full()) {
            SDL_PauseAudioDevice(device_, 0);
            started_ = true;
        }
        return underrun;
    }
    [[nodiscard]] std::uint64_t underruns() const noexcept {
        return underruns_;
    }

private:
    SDL_AudioDeviceID device_ = 0;
    bool started_ = false;
    std::uint64_t underruns_ = 0;
};

// Queues a frame's sound and says when an underrun happened, so the count
// printed on exit can be traced to its causes (the first second, a window
// drag, a stall out of nowhere).
void queue_frame_sound(AudioOut& audio, const std::vector<float>& samples, std::uint64_t frames,
                       std::uint64_t started, std::uint64_t counts_per_second) {
    if (!audio.queue(samples)) {
        return;
    }
    const std::uint64_t ms = (SDL_GetPerformanceCounter() - started) * 1000 / counts_per_second;
    std::cerr << "gameboy-emu: audio: underrun at frame " << frames << " (" << ms << " ms)\n";
}

// SDL_Init in the constructor, SDL_Quit in the destructor.
class SdlVideo {
public:
    SdlVideo() noexcept : ok_(SDL_Init(SDL_INIT_VIDEO) == 0) {}
    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;
    SdlVideo(SdlVideo&&) = delete;
    SdlVideo& operator=(SdlVideo&&) = delete;
    ~SdlVideo() {
        SDL_Quit();
    }
    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

private:
    bool ok_;
};

constexpr std::array<Button, 8> kButtons = {Button::Right,  Button::Left, Button::Up,
                                            Button::Down,   Button::A,    Button::B,
                                            Button::Select, Button::Start};

// Arrows: d-pad. X: A. Z: B. Enter: Start. Backspace: Select.
std::optional<Button> button_for(SDL_Keycode key) noexcept {
    switch (key) {
        case SDLK_RIGHT:
            return Button::Right;
        case SDLK_LEFT:
            return Button::Left;
        case SDLK_UP:
            return Button::Up;
        case SDLK_DOWN:
            return Button::Down;
        case SDLK_x:
            return Button::A;
        case SDLK_z:
            return Button::B;
        case SDLK_RETURN:
            return Button::Start;
        case SDLK_BACKSPACE:
            return Button::Select;
        default:
            return std::nullopt;
    }
}

bool is_direction(Button button) noexcept {
    return button == Button::Right || button == Button::Left || button == Button::Up ||
           button == Button::Down;
}

class Session {
public:
    Session(core::GameBoy& gb, const std::filesystem::path& state_path)
        : gb_(gb), state_path_(state_path), slot_(gb, state_path, std::cerr) {}

    // Handles every waiting event. Returns false once the user asked to quit.
    bool handle_events() {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
                case SDL_QUIT:
                    return false;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    if (!handle_key(event.key)) {
                        return false;
                    }
                    break;
                case SDL_WINDOWEVENT:
                    // Keys released while another window has focus never reach
                    // us, so let go of everything rather than leave a key stuck.
                    if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        release_all();
                    }
                    break;
                default:
                    break;
            }
        }
        return true;
    }

private:
    bool handle_key(const SDL_KeyboardEvent& key) {
        const bool down = key.type == SDL_KEYDOWN;
        if (key.keysym.sym == SDLK_ESCAPE && down) {
            return false;
        }
        if (key.repeat != 0) {
            return true;
        }
        if (down && !state_path_.empty() && key.keysym.sym == SDLK_F5) {
            slot_.save();
            return true;
        }
        if (down && !state_path_.empty() && key.keysym.sym == SDLK_F8) {
            if (slot_.load()) {
                // The state holds the buttons held when it was saved; the
                // keys held now are what count.
                apply_held();
            }
            return true;
        }
        const std::optional<Button> button = button_for(key.keysym.sym);
        if (!button) {
            return true;
        }
        if (is_direction(*button)) {
            dpad_.set_held(*button, down);
        } else {
            held_.at(static_cast<std::size_t>(*button)) = down;
        }
        apply_held();
        return true;
    }

    // Tells the machine which buttons are pressed now.
    void apply_held() {
        for (const Button button : kButtons) {
            gb_.set_button(button, is_direction(button)
                                       ? dpad_.pressed(button)
                                       : held_.at(static_cast<std::size_t>(button)));
        }
    }

    void release_all() {
        dpad_ = Dpad{};
        held_ = {};
        apply_held();
    }

    core::GameBoy& gb_;
    std::filesystem::path state_path_;
    StateSlot slot_;
    Dpad dpad_;
    std::array<bool, kButtons.size()> held_{};  // A, B, Select, Start, by Button
};

}  // namespace

int run_window(core::GameBoy& gb, const WindowOptions& options, BatterySaver* saver) {
    // SIGINT and SIGTERM must reach us as SDL's quit event, so that closing the
    // terminal still writes the save file (ADR-019). SDL skips installing its
    // signal handlers when this hint is set; refuse to run without them.
    if (SDL_GetHintBoolean(SDL_HINT_NO_SIGNAL_HANDLERS, SDL_FALSE) == SDL_TRUE) {
        std::cerr << "gameboy-emu: SDL_HINT_NO_SIGNAL_HANDLERS is set; the save file would not "
                     "be written on SIGINT or SIGTERM\n";
        return kExitSdlError;
    }
    SDL_SetMainReady();
    const SdlVideo video;
    if (!video.ok()) {
        std::cerr << "gameboy-emu: cannot start SDL: " << SDL_GetError() << "\n";
        return kExitSdlError;
    }
    // Scale up with hard pixel edges, not blurred.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    const std::unique_ptr<SDL_Window, SdlDeleter> window(
        SDL_CreateWindow(options.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         kWidth * options.scale, kHeight * options.scale, SDL_WINDOW_RESIZABLE));
    if (!window) {
        std::cerr << "gameboy-emu: cannot open a window: " << SDL_GetError() << "\n";
        return kExitSdlError;
    }
    // No vsync: the loop below paces itself to the Game Boy's rate, which is
    // not the monitor's.
    const std::unique_ptr<SDL_Renderer, SdlDeleter> renderer(
        SDL_CreateRenderer(window.get(), -1, 0));
    if (!renderer) {
        std::cerr << "gameboy-emu: cannot create a renderer: " << SDL_GetError() << "\n";
        return kExitSdlError;
    }
    // Draw at 160x144 and let SDL scale by whole multiples to fit the window.
    SDL_RenderSetLogicalSize(renderer.get(), kWidth, kHeight);
    SDL_RenderSetIntegerScale(renderer.get(), SDL_TRUE);
    const std::unique_ptr<SDL_Texture, SdlDeleter> texture(SDL_CreateTexture(
        renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight));
    if (!texture) {
        std::cerr << "gameboy-emu: cannot create a texture: " << SDL_GetError() << "\n";
        return kExitSdlError;
    }

    std::optional<AudioOut> audio;
    if (!options.mute) {
        audio.emplace();
        if (!audio->ok()) {
            std::cerr << "gameboy-emu: no sound: cannot open an audio device: " << SDL_GetError()
                      << "\n";
            audio.reset();
        }
    }
    gb.set_audio_enabled(audio.has_value());

    const std::uint64_t counts_per_second = SDL_GetPerformanceFrequency();
    const std::uint64_t counts_per_frame =
        counts_per_second * core::GameBoy::kTicksPerFrame / core::GameBoy::kTicksPerSecond;
    const std::uint64_t started = SDL_GetPerformanceCounter();
    std::uint64_t next_frame = started;
    std::uint64_t frames = 0;

    Session session(gb, options.state_path);
    std::array<std::uint32_t, static_cast<std::size_t>(kWidth) * kHeight> pixels{};
    std::vector<float> samples;
    while (session.handle_events()) {
        if (audio && audio->full()) {
            SDL_Delay(1);  // the sound card is behind; let it catch up
            continue;
        }
        gb.run_frame();
        ++frames;
        if (saver != nullptr) {
            saver->tick(BatterySaver::Clock::now());
        }
        if (audio) {
            samples.clear();
            gb.take_audio(samples);
            queue_frame_sound(*audio, samples, frames, started, counts_per_second);
        }

        const core::Ppu::Frame& frame = gb.frame();
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels.at(i) = kPalette.at(frame.at(i) & 3U);
        }
        SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), kWidth * 4);
        SDL_RenderClear(renderer.get());
        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
        SDL_RenderPresent(renderer.get());

        if (audio) {
            continue;  // the audio queue paces the loop
        }
        next_frame += counts_per_frame;
        const std::uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame) {
            SDL_Delay(static_cast<Uint32>((next_frame - now) * 1000 / counts_per_second));
        } else if (now - next_frame > kMaxFramesBehind * counts_per_frame) {
            next_frame = now;
        }
    }
    if (audio) {
        // How well the pacing worked: 0 underruns is "no crackle" (ADR-025).
        const std::uint64_t seconds = (SDL_GetPerformanceCounter() - started) / counts_per_second;
        std::cerr << "gameboy-emu: audio: " << audio->underruns() << " underruns in " << frames
                  << " frames (" << seconds << " s)\n";
    }
    return kExitOk;
}

}  // namespace frontend
