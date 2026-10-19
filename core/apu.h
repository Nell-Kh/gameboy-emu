#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

// The audio processing unit (docs/apu-notes.md): sound registers 0xFF10-0xFF26
// and wave RAM 0xFF30-0xFF3F, the 512 Hz frame sequencer, and per channel the
// on/off state, length counter, volume envelope, (channel 1) frequency sweep
// and the four waveforms. The mixed output comes in a later M5 step.
//
// Channels are numbered 0-3 here (Pan Docs' channels 1-4): 0 and 1 are the
// square channels, 2 the wave channel, 3 the noise channel.
class Apu {
public:
    static constexpr std::uint16_t kFirstRegister = 0xFF10;
    static constexpr std::uint16_t kNr52 = 0xFF26;
    static constexpr std::uint16_t kWaveRamStart = 0xFF30;
    static constexpr std::uint16_t kWaveRamEnd = 0xFF3F;
    static constexpr int kChannels = 4;

    // The state the boot ROM leaves behind (docs/apu-notes.md, section 3).
    Apu() noexcept;

    // True for 0xFF10-0xFF26 and 0xFF30-0xFF3F, including the unused
    // addresses in that range (0xFF15, 0xFF1F), which read 0xFF.
    [[nodiscard]] static bool is_register(std::uint16_t address) noexcept;

    // Unused and write-only bits read as 1 (docs/apu-notes.md, section 3).
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    // While the APU is powered off only NR52, wave RAM and the length parts
    // of NR11, NR21, NR31 and NR41 can be written.
    void write(std::uint16_t address, std::uint8_t value) noexcept;

    // One step of the frame sequencer. The bus calls this on each falling
    // edge of DIV bit 4 (512 times a second), so a DIV write can clock it.
    void step_frame_sequencer() noexcept;

    // Runs the channels' period timers for `t_cycles` clock ticks; the bus
    // calls this as time passes, like the timer's.
    void tick(std::uint32_t t_cycles) noexcept;

    // A channel's digital output, 0-15, which its DAC turns into a level:
    // for a square channel the current duty step (0 or 1) times its volume,
    // for the wave channel the current 4-bit sample shifted by its volume
    // code, for the noise channel the inverted low bit of its shift register
    // times its volume. 0 while the channel is off.
    [[nodiscard]] std::uint8_t output(int channel) const noexcept;

    // For tests and, later, the mixer.
    [[nodiscard]] bool powered() const noexcept {
        return powered_;
    }
    [[nodiscard]] bool channel_on(int channel) const noexcept;
    [[nodiscard]] std::uint16_t length_counter(int channel) const noexcept;
    [[nodiscard]] std::uint8_t volume(int channel) const noexcept;
    // Channel 1's period (NR13 and the low 3 bits of NR14), which the sweep
    // rewrites.
    [[nodiscard]] std::uint16_t square1_period() const noexcept;

private:
    struct Channel {
        bool on = false;
        bool length_enabled = false;
        std::uint16_t length = 0;  // counts down to 0; 64 or 256 steps
        std::uint8_t volume = 0;   // 0-15, from the envelope (not channel 3)
        std::uint8_t envelope_timer = 0;
        std::uint32_t timer = 0;    // clock ticks until the next waveform step
        std::uint8_t position = 0;  // the step within the waveform: duty step 0-7
    };

    // Offsets of the registers from 0xFF10.
    static constexpr std::size_t kRegisterCount = 0x17;

    [[nodiscard]] std::uint8_t reg(std::uint16_t address) const noexcept;
    [[nodiscard]] std::uint8_t& reg(std::uint16_t address) noexcept;
    [[nodiscard]] bool dac_on(int channel) const noexcept;
    [[nodiscard]] bool next_step_skips_length() const noexcept;

    void write_length(int channel, std::uint8_t value) noexcept;
    void write_control(int channel, std::uint8_t value) noexcept;  // NRx4
    void trigger(int channel) noexcept;
    void power_off() noexcept;

    void clock_length() noexcept;
    void clock_envelope() noexcept;
    void clock_sweep() noexcept;
    // Channel 1's next sweep period from the shadow register; turns the
    // channel off if it is above 2047.
    std::uint16_t sweep_calculate() noexcept;
    void set_square1_period(std::uint16_t period) noexcept;
    // The 11-bit period of a square channel (0 or 1), from NRx3 and NRx4.
    [[nodiscard]] std::uint16_t square_period(int channel) const noexcept;
    void tick_square(int channel, std::uint32_t t_cycles) noexcept;
    // Clock ticks between shifts of the noise LFSR, from NR43; 0 if the
    // clock shift (14 or 15) stops it.
    [[nodiscard]] std::uint32_t noise_period() const noexcept;
    void tick_noise(std::uint32_t t_cycles) noexcept;
    // Clock ticks per wave sample: (2048 - period) * 2.
    [[nodiscard]] std::uint32_t wave_period() const noexcept;
    void tick_wave(std::uint32_t t_cycles) noexcept;
    // On the DMG the CPU reaches wave RAM while channel 3 plays only in the
    // moment the channel fetches a sample (docs/apu-notes.md, section 6).
    [[nodiscard]] bool wave_ram_reachable() const noexcept;
    // The wave RAM byte channel 3 is playing from.
    [[nodiscard]] std::size_t wave_byte() const noexcept;
    // A CPU write to 0xFF30-0xFF3F.
    void write_wave_ram(std::uint16_t address, std::uint8_t value) noexcept;

    std::array<std::uint8_t, kRegisterCount> registers_{};
    std::array<std::uint8_t, 16> wave_ram_{};
    std::array<Channel, kChannels> channels_{};
    bool powered_ = true;
    std::uint8_t step_ = 0;  // the frame-sequencer step that comes next, 0-7

    // Channel 1's sweep.
    std::uint16_t sweep_shadow_ = 0;
    std::uint8_t sweep_timer_ = 0;
    bool sweep_enabled_ = false;
    bool sweep_negated_ = false;  // a calculation used negate since the trigger

    // Channel 4's linear feedback shift register, 15 bits.
    std::uint16_t lfsr_ = 0x7FFF;

    // Channel 3: the byte fetched from wave RAM (its two samples play in
    // turn), and the clock ticks since that fetch.
    std::uint8_t wave_sample_ = 0;
    std::uint32_t since_wave_fetch_ = 0xFFFF;
};

}  // namespace core
