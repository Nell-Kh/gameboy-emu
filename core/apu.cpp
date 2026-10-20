#include "core/apu.h"

#include <algorithm>
#include <cmath>

#include "core/state_io.h"

namespace core {

namespace {

constexpr std::uint16_t kNr10 = 0xFF10;
constexpr std::uint16_t kNr13 = 0xFF13;
constexpr std::uint16_t kNr14 = 0xFF14;
constexpr std::uint16_t kNr30 = 0xFF1A;
constexpr std::uint16_t kNr50 = 0xFF24;
constexpr std::uint16_t kNr51 = 0xFF25;

// Where each channel's registers start: NRx0 (which only channels 1 and 3
// have) is at the base, NRx1 to NRx4 follow.
constexpr std::array<std::uint16_t, Apu::kChannels> kChannelBase = {0xFF10, 0xFF15, 0xFF1A, 0xFF1F};
constexpr std::uint16_t kLengthOffset = 1;    // NRx1
constexpr std::uint16_t kEnvelopeOffset = 2;  // NRx2
constexpr std::uint16_t kControlOffset = 4;   // NRx4
constexpr int kWaveChannel = 2;
constexpr std::uint8_t kOpenBus = 0xFF;

// Bits that read as 1 whatever was written, for 0xFF10-0xFF25: unused,
// write-only, and the unused addresses 0xFF15 and 0xFF1F.
constexpr std::array<std::uint8_t, 0x16> kReadMask = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,  // NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,  // (FF15), NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF,  // (FF1F), NR41-NR44
    0x00, 0x00,                    // NR50, NR51
};
constexpr std::uint8_t kNr52ReadMask = 0x70;

constexpr std::uint8_t kPowerBit = 0x80;
constexpr std::uint8_t kTriggerBit = 0x80;
constexpr std::uint8_t kLengthEnableBit = 0x40;
constexpr std::uint8_t kDacBits = 0xF8;     // NRx2: volume and direction
constexpr std::uint8_t kWaveDacBit = 0x80;  // NR30
constexpr std::uint8_t kEnvelopeUpBit = 0x08;
constexpr std::uint8_t kPeriodBits = 0x07;  // NRx2 envelope period, NR10 shift
constexpr std::uint8_t kSweepNegateBit = 0x08;
constexpr std::uint8_t kSquareLengthBits = 0x3F;
constexpr std::uint8_t kMaxVolume = 15;
constexpr std::uint16_t kMaxPeriod = 2047;

constexpr std::uint16_t kPeriodLowOffset = 3;     // NRx3
constexpr std::uint8_t kDutyShift = 6;            // NRx1 bits 6-7
constexpr std::uint32_t kSquareTicksPerStep = 4;  // per unit of (2048 - period)
constexpr std::uint16_t kPeriodRange = 2048;
constexpr std::uint8_t kDutySteps = 8;

// The four duty patterns, one bit per step, step 0 in bit 0 (Pan Docs):
// 12.5 %, 25 %, 50 %, 75 %.
constexpr std::array<std::uint8_t, 4> kDutyPatterns = {
    0b1000'0000,  // 0 0 0 0 0 0 0 1
    0b1000'0001,  // 1 0 0 0 0 0 0 1
    0b1110'0001,  // 1 0 0 0 0 1 1 1
    0b0111'1110,  // 0 1 1 1 1 1 1 0
};

constexpr int kNoiseChannel = 3;
constexpr std::uint16_t kNr43 = 0xFF22;
constexpr std::uint16_t kLfsrFull = 0x7FFF;
constexpr std::uint8_t kNoiseShortModeBit = 0x08;  // NR43 bit 3: 7-bit LFSR
constexpr std::uint8_t kNoiseStoppedShift = 14;    // clock shifts 14 and 15 stop it

constexpr std::uint16_t kNr32 = 0xFF1C;
constexpr std::uint8_t kWaveSamples = 32;  // two per wave RAM byte
constexpr std::uint32_t kWaveTicksPerSample = 2;
constexpr std::uint32_t kNotFetched = 0xFFFF;

// Three timings of the wave channel, in clock ticks, found by testing every
// combination against Blargg's dmg_sound 09, 10 and 12 (ADR-023): the only
// one that passes all three.
//  - The first fetch after a trigger comes this long after one sample period.
constexpr std::uint32_t kWaveTriggerDelay = 4;
//  - While the channel plays, the CPU reaches wave RAM only when its access
//    comes this long after a fetch (the access lands at the end of the CPU's
//    machine cycle in this emulator, ADR-005).
constexpr std::uint32_t kWaveReachableAfterFetch = 2;
//  - A retrigger corrupts wave RAM when the next fetch is this far away.
constexpr std::uint32_t kWaveCorruptBeforeFetch = 4;

constexpr std::uint16_t full_length(int channel) noexcept {
    return channel == kWaveChannel ? 256 : 64;
}

// A timer period of 0 counts as 8, for both the envelope and the sweep.
constexpr std::uint8_t timer_period(std::uint8_t period) noexcept {
    return period == 0 ? 8 : period;
}

constexpr std::uint8_t sweep_period(std::uint8_t nr10) noexcept {
    return (nr10 >> 4U) & kPeriodBits;
}

}  // namespace

Apu::Apu() noexcept {
    // Register values after the DMG boot ROM, as stored (they read back with
    // kReadMask applied): NR11 duty 2, NR12 F3, NR50 77, NR51 F3. Channel 1 is
    // still marked on from the boot sound, its envelope already at 0 (choice:
    // the volume the boot sound ends on is not documented).
    reg(0xFF11) = 0x80;
    reg(0xFF12) = 0xF3;
    reg(kNr50) = 0x77;
    reg(kNr51) = 0xF3;
    channels_[0].on = true;
    channels_[0].length = full_length(0);
}

bool Apu::is_register(std::uint16_t address) noexcept {
    return (address >= kFirstRegister && address <= kNr52) ||
           (address >= kWaveRamStart && address <= kWaveRamEnd);
}

std::uint8_t Apu::reg(std::uint16_t address) const noexcept {
    return registers_.at(address - kFirstRegister);
}

std::uint8_t& Apu::reg(std::uint16_t address) noexcept {
    return registers_.at(address - kFirstRegister);
}

bool Apu::channel_on(int channel) const noexcept {
    return channels_.at(static_cast<std::size_t>(channel)).on;
}

std::uint16_t Apu::length_counter(int channel) const noexcept {
    return channels_.at(static_cast<std::size_t>(channel)).length;
}

std::uint8_t Apu::volume(int channel) const noexcept {
    return channels_.at(static_cast<std::size_t>(channel)).volume;
}

std::uint16_t Apu::square1_period() const noexcept {
    return static_cast<std::uint16_t>(reg(kNr13) | ((reg(kNr14) & kPeriodBits) << 8U));
}

std::uint16_t Apu::square_period(int channel) const noexcept {
    const std::uint16_t base = kChannelBase.at(static_cast<std::size_t>(channel));
    return static_cast<std::uint16_t>(reg(base + kPeriodLowOffset) |
                                      ((reg(base + kControlOffset) & kPeriodBits) << 8U));
}

void Apu::set_square1_period(std::uint16_t period) noexcept {
    reg(kNr13) = static_cast<std::uint8_t>(period);
    reg(kNr14) =
        static_cast<std::uint8_t>((reg(kNr14) & ~kPeriodBits) | ((period >> 8U) & kPeriodBits));
}

bool Apu::dac_on(int channel) const noexcept {
    if (channel == kWaveChannel) {
        return (reg(kNr30) & kWaveDacBit) != 0;
    }
    return (reg(kChannelBase.at(static_cast<std::size_t>(channel)) + kEnvelopeOffset) & kDacBits) !=
           0;
}

// Steps 0, 2, 4 and 6 clock the length counters.
bool Apu::next_step_skips_length() const noexcept {
    return (step_ & 1U) != 0;
}

std::uint8_t Apu::read(std::uint16_t address) const noexcept {
    if (address >= kWaveRamStart) {
        if (channels_[kWaveChannel].on) {
            return wave_ram_reachable() ? wave_ram_.at(wave_byte()) : kOpenBus;
        }
        return wave_ram_.at(address - kWaveRamStart);
    }
    if (address == kNr52) {
        std::uint8_t value = kNr52ReadMask | (powered_ ? kPowerBit : 0);
        for (int channel = 0; channel < kChannels; ++channel) {
            if (channel_on(channel)) {
                value |= static_cast<std::uint8_t>(1U << static_cast<unsigned>(channel));
            }
        }
        return value;
    }
    const std::size_t index = address - kFirstRegister;
    return registers_.at(index) | kReadMask.at(index);
}

void Apu::write_wave_ram(std::uint16_t address, std::uint8_t value) noexcept {
    if (!channels_[kWaveChannel].on) {
        wave_ram_.at(address - kWaveRamStart) = value;
    } else if (wave_ram_reachable()) {
        wave_ram_.at(wave_byte()) = value;
    }
}

void Apu::write(std::uint16_t address, std::uint8_t value) noexcept {
    if (address >= kWaveRamStart) {
        write_wave_ram(address, value);
        return;
    }
    if (address == kNr52) {
        if ((value & kPowerBit) == 0) {
            power_off();
        } else if (!powered_) {
            powered_ = true;
            step_ = 0;  // the next step is step 0
        }
        return;
    }
    if (address == 0xFF15 || address == 0xFF1F) {
        return;  // no register
    }
    for (int channel = 0; channel < kChannels; ++channel) {
        const std::uint16_t base = kChannelBase.at(static_cast<std::size_t>(channel));
        if (address == base + kLengthOffset) {
            // On the DMG the length can be written even with the power off.
            write_length(channel, value);
            if (powered_) {
                reg(address) = value;
            }
            return;
        }
    }
    if (!powered_) {
        return;
    }
    if (address == kNr10) {
        // Leaving negate mode after a calculation has used it turns channel 1 off.
        if (sweep_negated_ && (reg(kNr10) & kSweepNegateBit) != 0 &&
            (value & kSweepNegateBit) == 0) {
            channels_[0].on = false;
        }
        reg(address) = value;
        return;
    }
    reg(address) = value;
    for (int channel = 0; channel < kChannels; ++channel) {
        const std::uint16_t base = kChannelBase.at(static_cast<std::size_t>(channel));
        if (address == base + kControlOffset) {
            write_control(channel, value);
        }
        if (!dac_on(channel)) {
            channels_.at(static_cast<std::size_t>(channel)).on = false;
        }
    }
}

void Apu::write_length(int channel, std::uint8_t value) noexcept {
    Channel& c = channels_.at(static_cast<std::size_t>(channel));
    const std::uint8_t steps = channel == kWaveChannel ? value : (value & kSquareLengthBits);
    c.length = static_cast<std::uint16_t>(full_length(channel) - steps);
}

void Apu::write_control(int channel, std::uint8_t value) noexcept {
    Channel& c = channels_.at(static_cast<std::size_t>(channel));
    const bool was_enabled = c.length_enabled;
    c.length_enabled = (value & kLengthEnableBit) != 0;
    // Enabling length when the next step will not clock it clocks it once now
    // (docs/apu-notes.md, section 4).
    if (!was_enabled && c.length_enabled && next_step_skips_length() && c.length != 0) {
        --c.length;
        if (c.length == 0 && (value & kTriggerBit) == 0) {
            c.on = false;
        }
    }
    if ((value & kTriggerBit) != 0) {
        trigger(channel);
    }
}

void Apu::trigger(int channel) noexcept {
    Channel& c = channels_.at(static_cast<std::size_t>(channel));
    if (channel == kWaveChannel) {
        // Retriggering on the DMG just before the channel fetches a sample
        // overwrites the start of wave RAM from the byte at its current
        // position: just the first byte if that is one of bytes 0-3,
        // otherwise all four bytes of its aligned group (docs/apu-notes.md,
        // section 6).
        if (c.on && c.timer == kWaveCorruptBeforeFetch) {
            const std::size_t byte = wave_byte();
            if (byte < 4) {
                wave_ram_[0] = wave_ram_.at(byte);
            } else {
                const std::size_t group = byte & ~std::size_t{3};
                std::copy_n(wave_ram_.begin() + static_cast<std::ptrdiff_t>(group), 4,
                            wave_ram_.begin());
            }
        }
        // The position restarts, but the sample already fetched plays first.
        c.position = 0;
        c.timer = wave_period() + kWaveTriggerDelay;
        since_wave_fetch_ = kNotFetched;
    }
    if (c.length == 0) {
        c.length = full_length(channel);
        if (c.length_enabled && next_step_skips_length()) {
            --c.length;
        }
    }
    c.on = dac_on(channel);
    if (channel < kWaveChannel) {
        // The duty step is not reset; only the timer is reloaded.
        c.timer = (kPeriodRange - square_period(channel)) * kSquareTicksPerStep;
    }
    if (channel == kNoiseChannel) {
        lfsr_ = kLfsrFull;
        c.timer = noise_period();
    }
    if (channel != kWaveChannel) {
        const std::uint8_t envelope =
            reg(kChannelBase.at(static_cast<std::size_t>(channel)) + kEnvelopeOffset);
        c.volume = envelope >> 4U;
        c.envelope_timer = timer_period(envelope & kPeriodBits);
    }
    if (channel == 0) {
        const std::uint8_t nr10 = reg(kNr10);
        sweep_shadow_ = square1_period();
        sweep_timer_ = timer_period(sweep_period(nr10));
        sweep_enabled_ = sweep_period(nr10) != 0 || (nr10 & kPeriodBits) != 0;
        sweep_negated_ = false;
        if ((nr10 & kPeriodBits) != 0) {
            (void)sweep_calculate();  // only the overflow check
        }
    }
}

void Apu::power_off() noexcept {
    powered_ = false;
    // Every register is cleared, NR52 aside. The length counters are not:
    // on the DMG they keep their values and can still be written.
    registers_.fill(0);
    for (Channel& c : channels_) {
        const std::uint16_t length = c.length;
        c = Channel{};
        c.length = length;
    }
    sweep_shadow_ = 0;
    sweep_timer_ = 0;
    sweep_enabled_ = false;
    sweep_negated_ = false;
    wave_sample_ = 0;
    since_wave_fetch_ = kNotFetched;
}

void Apu::step_frame_sequencer() noexcept {
    if (!powered_) {
        return;
    }
    switch (step_) {
        case 0:
        case 4:
            clock_length();
            break;
        case 2:
        case 6:
            clock_length();
            clock_sweep();
            break;
        case 7:
            clock_envelope();
            break;
        default:
            break;
    }
    step_ = (step_ + 1) & 7U;
}

void Apu::clock_length() noexcept {
    for (Channel& c : channels_) {
        if (c.length_enabled && c.length > 0) {
            --c.length;
            if (c.length == 0) {
                c.on = false;
            }
        }
    }
}

void Apu::clock_envelope() noexcept {
    for (int channel = 0; channel < kChannels; ++channel) {
        if (channel == kWaveChannel) {
            continue;
        }
        Channel& c = channels_.at(static_cast<std::size_t>(channel));
        const std::uint8_t envelope =
            reg(kChannelBase.at(static_cast<std::size_t>(channel)) + kEnvelopeOffset);
        const std::uint8_t period = envelope & kPeriodBits;
        if (period == 0) {
            continue;
        }
        if (c.envelope_timer > 0) {
            --c.envelope_timer;
        }
        if (c.envelope_timer == 0) {
            c.envelope_timer = period;
            if ((envelope & kEnvelopeUpBit) != 0 && c.volume < kMaxVolume) {
                ++c.volume;
            } else if ((envelope & kEnvelopeUpBit) == 0 && c.volume > 0) {
                --c.volume;
            }
        }
    }
}

void Apu::clock_sweep() noexcept {
    if (sweep_timer_ > 0) {
        --sweep_timer_;
    }
    if (sweep_timer_ != 0) {
        return;
    }
    const std::uint8_t nr10 = reg(kNr10);
    sweep_timer_ = timer_period(sweep_period(nr10));
    if (!sweep_enabled_ || sweep_period(nr10) == 0) {
        return;
    }
    const std::uint16_t period = sweep_calculate();
    if (period <= kMaxPeriod && (nr10 & kPeriodBits) != 0) {
        sweep_shadow_ = period;
        set_square1_period(period);
        (void)sweep_calculate();  // checked again, not written back
    }
}

std::uint16_t Apu::sweep_calculate() noexcept {
    const std::uint8_t nr10 = reg(kNr10);
    const std::uint16_t delta = sweep_shadow_ >> (nr10 & kPeriodBits);
    std::uint16_t period = 0;
    if ((nr10 & kSweepNegateBit) != 0) {
        period = static_cast<std::uint16_t>(sweep_shadow_ - delta);
        sweep_negated_ = true;
    } else {
        period = static_cast<std::uint16_t>(sweep_shadow_ + delta);
    }
    if (period > kMaxPeriod) {
        channels_[0].on = false;
    }
    return period;
}

// Field by field, in the order of the table in docs/cartridge-notes.md,
// section 7; a unit test checks the section's length against it.
void Apu::save_state(StateWriter& out) const {
    out.bytes(registers_);
    out.bytes(wave_ram_);
    out.boolean(powered_);
    out.u8(step_);
    for (const Channel& c : channels_) {
        out.boolean(c.on);
        out.boolean(c.length_enabled);
        out.u16(c.length);
        out.u8(c.volume);
        out.u8(c.envelope_timer);
        out.u32(c.timer);
        out.u8(c.position);
    }
    out.u16(sweep_shadow_);
    out.u8(sweep_timer_);
    out.boolean(sweep_enabled_);
    out.boolean(sweep_negated_);
    out.u16(lfsr_);
    out.u8(wave_sample_);
    out.u32(since_wave_fetch_);
}

void Apu::load_state(StateReader& in) {
    constexpr std::uint8_t kLastStep = 7;
    constexpr std::uint8_t kLongestTimer = timer_period(0);  // 8, for envelope and sweep
    in.bytes(registers_);
    in.bytes(wave_ram_);
    powered_ = in.boolean();
    step_ = in.at_most(in.u8(), kLastStep);
    for (int channel = 0; channel < kChannels; ++channel) {
        Channel& c = channels_[static_cast<std::size_t>(channel)];
        c.on = in.boolean();
        c.length_enabled = in.boolean();
        c.length = static_cast<std::uint16_t>(
            in.at_most(std::uint32_t{in.u16()}, std::uint32_t{full_length(channel)}));
        c.volume = in.at_most(in.u8(), kMaxVolume);
        c.envelope_timer = in.at_most(in.u8(), kLongestTimer);
        c.timer = in.u32();
        // The position indexes the duty pattern or wave RAM.
        const std::uint8_t last_position =
            channel == kWaveChannel ? kWaveSamples - 1 : kDutySteps - 1;
        c.position = in.at_most(in.u8(), last_position);
    }
    sweep_shadow_ =
        static_cast<std::uint16_t>(in.at_most(std::uint32_t{in.u16()}, std::uint32_t{kMaxPeriod}));
    sweep_timer_ = in.at_most(in.u8(), kLongestTimer);
    sweep_enabled_ = in.boolean();
    sweep_negated_ = in.boolean();
    lfsr_ =
        static_cast<std::uint16_t>(in.at_most(std::uint32_t{in.u16()}, std::uint32_t{kLfsrFull}));
    wave_sample_ = in.u8();
    since_wave_fetch_ = in.u32();
}

void Apu::tick(std::uint32_t t_cycles) noexcept {
    if (output_enabled_) {
        // At the level the channels had during these ticks. Powered off the
        // level is silence, but the samples still come: the window's pace
        // hangs on them (ADR-025).
        produce_samples(t_cycles);
    }
    if (!powered_) {
        return;
    }
    tick_square(0, t_cycles);
    tick_square(1, t_cycles);
    tick_wave(t_cycles);
    tick_noise(t_cycles);
}

bool Apu::any_dac_on() const noexcept {
    for (int channel = 0; channel < kChannels; ++channel) {
        if (dac_on(channel)) {
            return true;
        }
    }
    return false;
}

Apu::Stereo Apu::mix() const noexcept {
    constexpr float kDacSteps = 7.5F;        // digital 0-15 to analog -1 to 1
    constexpr float kLoudest = 4.0F * 8.0F;  // four channels at volume 8
    Stereo level;
    const std::uint8_t nr51 = reg(kNr51);
    for (int channel = 0; channel < kChannels; ++channel) {
        if (!dac_on(channel)) {
            continue;  // a DAC that is off adds nothing
        }
        const float analog = (static_cast<float>(output(channel)) / kDacSteps) - 1.0F;
        const auto bit = static_cast<unsigned>(channel);
        if ((nr51 & (0x10U << bit)) != 0) {
            level.left += analog;
        }
        if ((nr51 & (0x01U << bit)) != 0) {
            level.right += analog;
        }
    }
    const std::uint8_t nr50 = reg(kNr50);
    level.left *= static_cast<float>(((nr50 >> 4U) & 7U) + 1) / kLoudest;
    level.right *= static_cast<float>((nr50 & 7U) + 1) / kLoudest;
    return level;
}

void Apu::set_output_enabled(bool enabled) noexcept {
    output_enabled_ = enabled;
}

void Apu::take_samples(std::vector<float>& out) {
    out.insert(out.end(), samples_.begin(), samples_.end());
    samples_.clear();
}

// A box filter: each sample is the average level over its own stretch of
// clock ticks, split exactly where a sample ends inside a tick run.
void Apu::produce_samples(std::uint32_t t_cycles) {
    constexpr std::uint64_t kUnitsPerSample = 4'194'304;  // clock ticks per second
    const Stereo level = powered_ ? mix() : Stereo{};
    std::uint64_t units = std::uint64_t{t_cycles} * kSampleRate;
    while (sample_phase_ + units >= kUnitsPerSample) {
        const std::uint64_t part = kUnitsPerSample - sample_phase_;
        sample_sum_.left += level.left * static_cast<float>(part);
        sample_sum_.right += level.right * static_cast<float>(part);
        finish_sample(sample_sum_);
        sample_sum_ = Stereo{};
        sample_phase_ = 0;
        units -= part;
    }
    sample_phase_ += units;
    sample_sum_.left += level.left * static_cast<float>(units);
    sample_sum_.right += level.right * static_cast<float>(units);
}

// The high-pass filter of the output capacitor (Blargg's sound notes on
// the gbdev wiki): it removes the constant offset a DAC leaves, and lets
// the output fall to silence when every DAC is off.
void Apu::finish_sample(Stereo sum) {
    constexpr float kUnitsPerSample = 4'194'304.0F;
    // The capacitor keeps 0.999958 of its charge per clock tick.
    static const float kCharge = std::pow(0.999958F, kUnitsPerSample / kSampleRate);
    const Stereo in{sum.left / kUnitsPerSample, sum.right / kUnitsPerSample};
    Stereo out;
    if (any_dac_on()) {
        out.left = in.left - capacitor_.left;
        out.right = in.right - capacitor_.right;
        capacitor_.left = in.left - (out.left * kCharge);
        capacitor_.right = in.right - (out.right * kCharge);
    }
    if (samples_.size() >= 2 * kMaxQueuedSamples) {
        ++dropped_samples_;
        return;
    }
    samples_.push_back(out.left);
    samples_.push_back(out.right);
}

std::uint32_t Apu::wave_period() const noexcept {
    return (kPeriodRange - square_period(kWaveChannel)) * kWaveTicksPerSample;
}

std::size_t Apu::wave_byte() const noexcept {
    return channels_[kWaveChannel].position / 2U;
}

bool Apu::wave_ram_reachable() const noexcept {
    return since_wave_fetch_ == kWaveReachableAfterFetch;
}

// Each timer expiry moves to the next sample and fetches its byte from wave
// RAM; the clock ticks since the last fetch decide whether the CPU can reach
// wave RAM (wave_ram_reachable).
void Apu::tick_wave(std::uint32_t t_cycles) noexcept {
    Channel& c = channels_[kWaveChannel];
    if (!c.on) {
        return;
    }
    bool fetched = false;
    while (t_cycles >= c.timer) {
        t_cycles -= c.timer;
        c.timer = wave_period();
        c.position = (c.position + 1) % kWaveSamples;
        wave_sample_ = wave_ram_.at(wave_byte());
        fetched = true;
    }
    c.timer -= t_cycles;
    if (fetched) {
        since_wave_fetch_ = t_cycles;
    } else if (since_wave_fetch_ < kNotFetched) {
        since_wave_fetch_ = std::min(since_wave_fetch_ + t_cycles, kNotFetched);
    }
}

// The timer counts down (2048 - period) * 4 clock ticks per duty step. A new
// period takes effect when the timer next reloads. The timer only runs while
// the channel is on (choice: what it does while off is not documented, and
// shows only as the duty step a later trigger starts from).
void Apu::tick_square(int channel, std::uint32_t t_cycles) noexcept {
    Channel& c = channels_.at(static_cast<std::size_t>(channel));
    if (!c.on) {
        return;
    }
    while (t_cycles >= c.timer) {
        t_cycles -= c.timer;
        c.timer = (kPeriodRange - square_period(channel)) * kSquareTicksPerStep;
        c.position = (c.position + 1) % kDutySteps;
    }
    c.timer -= t_cycles;
}

// The divisor code r (NR43 bits 0-2) gives 8 clock ticks for 0 and 16 * r
// otherwise, shifted left by the clock shift (bits 4-7) (Pan Docs).
std::uint32_t Apu::noise_period() const noexcept {
    const std::uint8_t nr43 = reg(kNr43);
    const std::uint32_t shift = nr43 >> 4U;
    if (shift >= kNoiseStoppedShift) {
        return 0;
    }
    const std::uint32_t code = nr43 & kPeriodBits;
    const std::uint32_t divisor = code == 0 ? 8 : 16 * code;
    return divisor << shift;
}

// Each shift: the XOR of bits 0 and 1 goes into bit 14 (and also bit 6 in
// short mode) as the register shifts right. Like the square channels' timer,
// it only runs while the channel is on.
void Apu::tick_noise(std::uint32_t t_cycles) noexcept {
    Channel& c = channels_[kNoiseChannel];
    const std::uint32_t period = noise_period();
    if (!c.on || period == 0) {
        return;
    }
    const bool short_mode = (reg(kNr43) & kNoiseShortModeBit) != 0;
    while (t_cycles >= c.timer) {
        t_cycles -= c.timer;
        c.timer = period;
        const auto feedback = static_cast<std::uint16_t>((lfsr_ ^ (lfsr_ >> 1U)) & 1U);
        lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1U) | (feedback << 14U));
        if (short_mode) {
            lfsr_ = static_cast<std::uint16_t>((lfsr_ & ~(1U << 6U)) | (feedback << 6U));
        }
    }
    c.timer -= t_cycles;
}

std::uint8_t Apu::output(int channel) const noexcept {
    const Channel& c = channels_.at(static_cast<std::size_t>(channel));
    if (!c.on) {
        return 0;
    }
    if (channel == kWaveChannel) {
        // Even positions play the high nibble. Volume code 0 mutes, 1-3
        // shift right by 0, 1 or 2.
        constexpr std::array<std::uint8_t, 4> kShift = {4, 0, 1, 2};
        const std::uint8_t sample =
            (c.position % 2U) == 0 ? (wave_sample_ >> 4U) : (wave_sample_ & 0x0FU);
        return sample >> kShift.at((reg(kNr32) >> 5U) & 3U);
    }
    if (channel == kNoiseChannel) {
        return (lfsr_ & 1U) == 0 ? c.volume : 0;
    }
    const std::uint8_t duty =
        reg(kChannelBase.at(static_cast<std::size_t>(channel)) + kLengthOffset) >> kDutyShift;
    const bool high = ((kDutyPatterns.at(duty) >> c.position) & 1U) != 0;
    return high ? c.volume : 0;
}

}  // namespace core
