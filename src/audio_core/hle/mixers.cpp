// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "audio_core/hle/mixers.h"
#include "common/assert.h"
#include "common/logging/log.h"

namespace AudioCore::HLE {

void Mixers::Reset() {
    current_frame.fill({});
    state = {};
}

void Mixers::Sleep() {
    backup_state = state;
    backup_frame = current_frame;
}

void Mixers::Wakeup() {
    state = backup_state;
    current_frame = backup_frame;
    backup_state = {};
    backup_frame.fill({});
}

DspStatus Mixers::Tick(DspConfiguration& config, const IntermediateMixSamples& read_samples,
                       IntermediateMixSamples& write_samples,
                       const std::array<PlanarQuadFrame32, 3>& input) {
    ParseConfig(config);

    AuxReturn(read_samples);
    AuxSend(write_samples, input);

    MixCurrentFrame();

    return GetCurrentStatus();
}

void Mixers::ParseConfig(DspConfiguration& config) {
    if (!config.dirty_raw) {
        return;
    }

    if (config.aux_bus_enable_0_dirty) {
        config.aux_bus_enable_0_dirty.Assign(0);
        state.aux_bus_enable[0] = config.aux_bus_enable[0] != 0;
        LOG_TRACE(Audio_DSP, "mixers aux_bus_enable[0] = {}", config.aux_bus_enable[0]);
    }

    if (config.aux_bus_enable_1_dirty) {
        config.aux_bus_enable_1_dirty.Assign(0);
        state.aux_bus_enable[1] = config.aux_bus_enable[1] != 0;
        LOG_TRACE(Audio_DSP, "mixers aux_bus_enable[1] = {}", config.aux_bus_enable[1]);
    }

    if (config.master_volume_dirty) {
        config.master_volume_dirty.Assign(0);
        state.intermediate_mixer_volume[0] = config.master_volume;
        LOG_TRACE(Audio_DSP, "mixers master_volume = {}", config.master_volume);
    }

    if (config.aux_return_volume_0_dirty) {
        config.aux_return_volume_0_dirty.Assign(0);
        state.intermediate_mixer_volume[1] = config.aux_return_volume[0];
        LOG_TRACE(Audio_DSP, "mixers aux_return_volume[0] = {}", config.aux_return_volume[0]);
    }

    if (config.aux_return_volume_1_dirty) {
        config.aux_return_volume_1_dirty.Assign(0);
        state.intermediate_mixer_volume[2] = config.aux_return_volume[1];
        LOG_TRACE(Audio_DSP, "mixers aux_return_volume[1] = {}", config.aux_return_volume[1]);
    }

    if (config.output_format_dirty) {
        config.output_format_dirty.Assign(0);
        state.output_format = config.output_format;
        LOG_TRACE(Audio_DSP, "mixers output_format = {}",
                  static_cast<std::size_t>(config.output_format));
    }

    if (config.headphones_connected_dirty) {
        config.headphones_connected_dirty.Assign(0);
        // Do nothing. (Note: Whether headphones are connected does affect coefficients used for
        // surround sound.)
        LOG_TRACE(Audio_DSP, "mixers headphones_connected={}", config.headphones_connected);
    }

    if (config.dirty_raw) {
        LOG_DEBUG(Audio_DSP, "mixers remaining_dirty={:x}", config.dirty_raw);
    }

    config.dirty_raw = 0;
}

static s16 ClampToS16(s32 value) {
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}

static std::array<s16, 2> AddAndClampToS16(const std::array<s16, 2>& a,
                                           const std::array<s16, 2>& b) {
    return {ClampToS16(static_cast<s32>(a[0]) + static_cast<s32>(b[0])),
            ClampToS16(static_cast<s32>(a[1]) + static_cast<s32>(b[1]))};
}

static void CopySharedToPlanar(PlanarQuadFrame32& output,
                               const s32_le (&input)[4][samples_per_frame]) {
    if constexpr (std::is_same_v<s32_le, s32>) {
        static_assert(sizeof(output) == sizeof(input));
        std::memcpy(output.data(), input, sizeof(input));
    } else {
        for (std::size_t channel = 0; channel < output.size(); ++channel) {
            std::copy_n(input[channel], samples_per_frame, output[channel].begin());
        }
    }
}

static void CopyPlanarToShared(s32_le (&output)[4][samples_per_frame],
                               const PlanarQuadFrame32& input) {
    if constexpr (std::is_same_v<s32_le, s32>) {
        static_assert(sizeof(output) == sizeof(input));
        std::memcpy(output, input.data(), sizeof(output));
    } else {
        for (std::size_t channel = 0; channel < input.size(); ++channel) {
            std::copy(input[channel].begin(), input[channel].end(), output[channel]);
        }
    }
}

#if defined(__aarch64__)
static void DownmixStereoNEON(StereoFrame16& current_frame, float gain,
                              const PlanarQuadFrame32& samples) {
    static_assert(samples_per_frame % 4 == 0);
    static_assert(sizeof(StereoFrame16::value_type) == 2 * sizeof(s16));

    const float32x4_t gain_vec = vdupq_n_f32(gain);
    for (std::size_t sample = 0; sample < samples_per_frame; sample += 4) {
        int16x4x2_t accumulator = vld2_s16(current_frame[sample].data());

        const float32x4_t front_left = vcvtq_f32_s32(vld1q_s32(&samples[0][sample]));
        const float32x4_t front_right = vcvtq_f32_s32(vld1q_s32(&samples[1][sample]));
        const float32x4_t back_left = vcvtq_f32_s32(vld1q_s32(&samples[2][sample]));
        const float32x4_t back_right = vcvtq_f32_s32(vld1q_s32(&samples[3][sample]));

        // Keep the same multiply/FMA order emitted for the scalar AArch64 path.
        float32x4_t left = vmulq_f32(back_left, gain_vec);
        float32x4_t right = vmulq_f32(back_right, gain_vec);
        left = vfmaq_f32(left, front_left, gain_vec);
        right = vfmaq_f32(right, front_right, gain_vec);

        const int16x4_t left_s16 = vqmovn_s32(vcvtq_s32_f32(left));
        const int16x4_t right_s16 = vqmovn_s32(vcvtq_s32_f32(right));
        accumulator.val[0] = vqadd_s16(accumulator.val[0], left_s16);
        accumulator.val[1] = vqadd_s16(accumulator.val[1], right_s16);
        vst2_s16(current_frame[sample].data(), accumulator);
    }
}

static void DownmixMonoNEON(StereoFrame16& current_frame, float gain,
                            const PlanarQuadFrame32& samples) {
    static_assert(samples_per_frame % 4 == 0);
    static_assert(sizeof(StereoFrame16::value_type) == 2 * sizeof(s16));

    const float32x4_t gain_vec = vdupq_n_f32(gain);
    for (std::size_t sample = 0; sample < samples_per_frame; sample += 4) {
        int16x4x2_t accumulator = vld2_s16(current_frame[sample].data());

        const float32x4_t channel_0 = vcvtq_f32_s32(vld1q_s32(&samples[0][sample]));
        const float32x4_t channel_1 = vcvtq_f32_s32(vld1q_s32(&samples[1][sample]));
        const float32x4_t channel_2 = vcvtq_f32_s32(vld1q_s32(&samples[2][sample]));
        const float32x4_t channel_3 = vcvtq_f32_s32(vld1q_s32(&samples[3][sample]));

        // Match the scalar AArch64 operation order before dividing the mono sum by two.
        float32x4_t mono = vmulq_f32(channel_1, gain_vec);
        mono = vfmaq_f32(mono, channel_0, gain_vec);
        mono = vfmaq_f32(mono, channel_2, gain_vec);
        mono = vfmaq_f32(mono, channel_3, gain_vec);
        mono = vmulq_n_f32(mono, 0.5f);

        const int16x4_t mono_s16 = vqmovn_s32(vcvtq_s32_f32(mono));
        accumulator.val[0] = vqadd_s16(accumulator.val[0], mono_s16);
        accumulator.val[1] = vqadd_s16(accumulator.val[1], mono_s16);
        vst2_s16(current_frame[sample].data(), accumulator);
    }
}
#endif

void Mixers::DownmixAndMixIntoCurrentFrame(float gain, const PlanarQuadFrame32& samples) {
    // TODO(merry): Limiter. (Currently we're performing final mixing assuming a disabled limiter.)

    switch (state.output_format) {
    case OutputFormat::Mono:
#if defined(__aarch64__)
        DownmixMonoNEON(current_frame, gain, samples);
#else
        for (std::size_t sample = 0; sample < samples_per_frame; ++sample) {
            const s16 mono = ClampToS16(static_cast<s32>(
                (gain * samples[0][sample] + gain * samples[1][sample] +
                 gain * samples[2][sample] + gain * samples[3][sample]) /
                2));
            current_frame[sample] = AddAndClampToS16(current_frame[sample], {mono, mono});
        }
#endif
        return;

    case OutputFormat::Surround:
        // TODO(merry): Implement surround sound.
        // fallthrough

    case OutputFormat::Stereo:
#if defined(__aarch64__)
        DownmixStereoNEON(current_frame, gain, samples);
#else
        for (std::size_t sample = 0; sample < samples_per_frame; ++sample) {
            const s16 left =
                ClampToS16(static_cast<s32>(gain * samples[0][sample] + gain * samples[2][sample]));
            const s16 right =
                ClampToS16(static_cast<s32>(gain * samples[1][sample] + gain * samples[3][sample]));
            current_frame[sample] = AddAndClampToS16(current_frame[sample], {left, right});
        }
#endif
        return;
    }

    UNREACHABLE_MSG("Invalid output_format {}", static_cast<std::size_t>(state.output_format));
}

void Mixers::AuxReturn(const IntermediateMixSamples& read_samples) {
    if (state.aux_bus_enable[0]) {
        CopySharedToPlanar(state.intermediate_mix_buffer[1], read_samples.mix1.pcm32);
    }

    if (state.aux_bus_enable[1]) {
        CopySharedToPlanar(state.intermediate_mix_buffer[2], read_samples.mix2.pcm32);
    }
}

void Mixers::AuxSend(IntermediateMixSamples& write_samples,
                     const std::array<PlanarQuadFrame32, 3>& input) {
    state.intermediate_mix_buffer[0] = input[0];

    if (state.aux_bus_enable[0]) {
        CopyPlanarToShared(write_samples.mix1.pcm32, input[1]);
    } else {
        state.intermediate_mix_buffer[1] = input[1];
    }

    if (state.aux_bus_enable[1]) {
        CopyPlanarToShared(write_samples.mix2.pcm32, input[2]);
    } else {
        state.intermediate_mix_buffer[2] = input[2];
    }
}

void Mixers::MixCurrentFrame() {
    current_frame.fill({});

    // TODO(SachinV): This is probably not accurate, based on symbols from FE:Fates,
    // state.intermediate_mixer_volume[0] represents the master volume
    for (std::size_t mix = 0; mix < 3; mix++) {
        DownmixAndMixIntoCurrentFrame(state.intermediate_mixer_volume[mix],
                                      state.intermediate_mix_buffer[mix]);
    }

    // TODO(merry): Compressor. (We currently assume a disabled compressor.)
}

DspStatus Mixers::GetCurrentStatus() const {
    DspStatus status;
    status.unknown = 0;
    status.dropped_frames = 0;
    return status;
}

} // namespace AudioCore::HLE
