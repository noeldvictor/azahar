// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "audio_core/codec.h"
#include "audio_core/hle/common.h"
#include "audio_core/hle/source.h"
#include "audio_core/interpolate.h"
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/logging/log.h"
#include "core/memory.h"

namespace AudioCore::HLE {

namespace {

bool HasAudibleGain(const std::array<float, 4>& gains) {
#if defined(__aarch64__)
    const uint32x4_t zero_mask = vceqq_f32(vld1q_f32(gains.data()), vdupq_n_f32(0.0f));
    return vminvq_u32(zero_mask) == 0;
#else
    return std::any_of(gains.begin(), gains.end(), [](float gain) { return gain != 0.0f; });
#endif
}

} // Anonymous namespace

#if defined(__aarch64__)
namespace {

template <bool accumulate>
inline void MixSourceBand(s32* dest, float32x4_t samples, float32x4_t gain) {
    int32x4_t mixed = vcvtq_s32_f32(vmulq_f32(samples, gain));
    if constexpr (accumulate) {
        mixed = vaddq_s32(vld1q_s32(dest), mixed);
    }
    vst1q_s32(dest, mixed);
}

inline bool HasAudibleRearGain(const std::array<float, 4>& gains) {
    static_assert(sizeof(float) == sizeof(u32));
    const u64 bits =
        vget_lane_u64(vreinterpret_u64_u32(vreinterpret_u32_f32(vld1_f32(gains.data() + 2))), 0);
    return (bits & 0x7FFF'FFFF'7FFF'FFFFULL) != 0;
}

template <bool ramp_active, bool rear_active, bool accumulate>
void MixSourceFrameA64(PlanarQuadFrame32& dest, const StereoFrame16& source,
                       const std::array<float, 4>& ramp_start, const std::array<float, 4>& gains) {
    static_assert(samples_per_frame % 8 == 0);
    static_assert(sizeof(StereoFrame16::value_type) == 2 * sizeof(s16));

    const float32x4_t gain_0 = vdupq_n_f32(gains[0]);
    const float32x4_t gain_1 = vdupq_n_f32(gains[1]);
    float32x4_t gain_2{};
    float32x4_t gain_3{};
    if constexpr (rear_active) {
        gain_2 = vdupq_n_f32(gains[2]);
        gain_3 = vdupq_n_f32(gains[3]);
    }

    float32x4_t ramp_start_0{};
    float32x4_t ramp_start_1{};
    float32x4_t ramp_start_2{};
    float32x4_t ramp_start_3{};
    float32x4_t ramp_delta_0{};
    float32x4_t ramp_delta_1{};
    float32x4_t ramp_delta_2{};
    float32x4_t ramp_delta_3{};
    if constexpr (ramp_active) {
        ramp_start_0 = vdupq_n_f32(ramp_start[0]);
        ramp_start_1 = vdupq_n_f32(ramp_start[1]);
        ramp_delta_0 = vsubq_f32(gain_0, ramp_start_0);
        ramp_delta_1 = vsubq_f32(gain_1, ramp_start_1);
        if constexpr (rear_active) {
            ramp_start_2 = vdupq_n_f32(ramp_start[2]);
            ramp_start_3 = vdupq_n_f32(ramp_start[3]);
            ramp_delta_2 = vsubq_f32(gain_2, ramp_start_2);
            ramp_delta_3 = vsubq_f32(gain_3, ramp_start_3);
        }
    }

    constexpr float ramp_scale = 1.0f / static_cast<float>(samples_per_frame - 1);
    alignas(16) static constexpr std::array<u32, 8> initial_sample_indices{0, 1, 2, 3, 4, 5, 6, 7};
    uint32x4_t sample_indices_0 = vld1q_u32(initial_sample_indices.data());
    uint32x4_t sample_indices_1 = vld1q_u32(initial_sample_indices.data() + 4);
    const uint32x4_t sample_index_step = vdupq_n_u32(8);

    const StereoFrame16::value_type* source_band = source.data();
    s32* dest_0_band = dest[0].data();
    s32* dest_1_band = dest[1].data();
    s32* dest_2_band = dest[2].data();
    s32* dest_3_band = dest[3].data();
    const s32* const dest_end = dest_0_band + samples_per_frame;
    for (; dest_0_band != dest_end; source_band += 8, dest_0_band += 8, dest_1_band += 8) {
        // Two ordinary Q loads plus UZP are preferable to a structured LD2 on Thor's A510.
        const int16x8_t interleaved_0 = vld1q_s16(source_band[0].data());
        const int16x8_t interleaved_1 = vld1q_s16(source_band[4].data());
        const int16x8_t left = vuzp1q_s16(interleaved_0, interleaved_1);
        const int16x8_t right = vuzp2q_s16(interleaved_0, interleaved_1);

        const float32x4_t left_0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(left)));
        const float32x4_t left_1 = vcvtq_f32_s32(vmovl_high_s16(left));
        const float32x4_t right_0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(right)));
        const float32x4_t right_1 = vcvtq_f32_s32(vmovl_high_s16(right));

        float32x4_t band_gain_0_0 = gain_0;
        float32x4_t band_gain_0_1 = gain_0;
        float32x4_t band_gain_1_0 = gain_1;
        float32x4_t band_gain_1_1 = gain_1;
        float32x4_t band_gain_2_0{};
        float32x4_t band_gain_2_1{};
        float32x4_t band_gain_3_0{};
        float32x4_t band_gain_3_1{};
        if constexpr (rear_active) {
            band_gain_2_0 = gain_2;
            band_gain_2_1 = gain_2;
            band_gain_3_0 = gain_3;
            band_gain_3_1 = gain_3;
        }
        if constexpr (ramp_active) {
            const float32x4_t progress_0 = vmulq_n_f32(vcvtq_f32_u32(sample_indices_0), ramp_scale);
            const float32x4_t progress_1 = vmulq_n_f32(vcvtq_f32_u32(sample_indices_1), ramp_scale);
            band_gain_0_0 = vfmaq_f32(ramp_start_0, progress_0, ramp_delta_0);
            band_gain_0_1 = vfmaq_f32(ramp_start_0, progress_1, ramp_delta_0);
            band_gain_1_0 = vfmaq_f32(ramp_start_1, progress_0, ramp_delta_1);
            band_gain_1_1 = vfmaq_f32(ramp_start_1, progress_1, ramp_delta_1);
            if constexpr (rear_active) {
                band_gain_2_0 = vfmaq_f32(ramp_start_2, progress_0, ramp_delta_2);
                band_gain_2_1 = vfmaq_f32(ramp_start_2, progress_1, ramp_delta_2);
                band_gain_3_0 = vfmaq_f32(ramp_start_3, progress_0, ramp_delta_3);
                band_gain_3_1 = vfmaq_f32(ramp_start_3, progress_1, ramp_delta_3);
            }
            sample_indices_0 = vaddq_u32(sample_indices_0, sample_index_step);
            sample_indices_1 = vaddq_u32(sample_indices_1, sample_index_step);
        }

        MixSourceBand<accumulate>(dest_0_band, left_0, band_gain_0_0);
        MixSourceBand<accumulate>(dest_0_band + 4, left_1, band_gain_0_1);
        MixSourceBand<accumulate>(dest_1_band, right_0, band_gain_1_0);
        MixSourceBand<accumulate>(dest_1_band + 4, right_1, band_gain_1_1);
        if constexpr (rear_active) {
            MixSourceBand<accumulate>(dest_2_band, left_0, band_gain_2_0);
            MixSourceBand<accumulate>(dest_2_band + 4, left_1, band_gain_2_1);
            MixSourceBand<accumulate>(dest_3_band, right_0, band_gain_3_0);
            MixSourceBand<accumulate>(dest_3_band + 4, right_1, band_gain_3_1);
            dest_2_band += 8;
            dest_3_band += 8;
        }
    }

    if constexpr (!accumulate && !rear_active) {
        static_assert(std::is_trivially_copyable_v<PlanarQuadFrame32::value_type>);
        std::memset(dest.data() + 2, 0, 2 * sizeof(dest[0]));
    }
}

} // Anonymous namespace
#endif

namespace {

template <bool accumulate>
void MixSourceFrame(PlanarQuadFrame32& dest, const StereoFrame16& source,
                    const std::array<float, 4>& ramp_start, const std::array<float, 4>& gains,
                    bool ramp_active) {
#if defined(__aarch64__)
    const bool rear_silent =
        !HasAudibleRearGain(gains) && (!ramp_active || !HasAudibleRearGain(ramp_start));
    if (ramp_active) {
        if (rear_silent) {
            MixSourceFrameA64<true, false, accumulate>(dest, source, ramp_start, gains);
        } else {
            MixSourceFrameA64<true, true, accumulate>(dest, source, ramp_start, gains);
        }
    } else {
        if (rear_silent) {
            MixSourceFrameA64<false, false, accumulate>(dest, source, ramp_start, gains);
        } else {
            MixSourceFrameA64<false, true, accumulate>(dest, source, ramp_start, gains);
        }
    }
#else
    constexpr float ramp_scale = 1.0f / static_cast<float>(samples_per_frame - 1);
    for (std::size_t sample = 0; sample < samples_per_frame; ++sample) {
        const float progress = static_cast<float>(sample) * ramp_scale;
        for (std::size_t channel = 0; channel < dest.size(); ++channel) {
            const float gain = ramp_active ? ramp_start[channel] +
                                                 (gains[channel] - ramp_start[channel]) * progress
                                           : gains[channel];
            const s32 mixed = static_cast<s32>(gain * source[sample][channel & 1]);
            if constexpr (accumulate) {
                dest[channel][sample] += mixed;
            } else {
                dest[channel][sample] = mixed;
            }
        }
    }
#endif
}

CITRA_NO_INLINE void DefineSourceFrame(PlanarQuadFrame32& dest, const StereoFrame16& source,
                                       const std::array<float, 4>& ramp_start,
                                       const std::array<float, 4>& gains, bool ramp_active) {
    MixSourceFrame<false>(dest, source, ramp_start, gains, ramp_active);
}

} // Anonymous namespace

SourceStatus::Status Source::Tick(SourceConfiguration::Configuration& config,
                                  const s16_le (&adpcm_coeffs)[16]) {
    ParseConfig(config, adpcm_coeffs);

    if (state.enabled) {
        GenerateFrame();
    }

    return GetCurrentStatus();
}

void Source::MixInto(std::array<PlanarQuadFrame32, 3>& dest) {
    if (!state.enabled) {
        state.gain_ramp_start = state.gain;
        state.gain_ramp_active.fill(false);
        return;
    }

    for (std::size_t mix = 0; mix < dest.size(); ++mix) {
        const std::array<float, 4>& gains = state.gain[mix];
        const bool ramp_active = state.gain_ramp_active[mix];
        const std::array<float, 4>& ramp_start = state.gain_ramp_start[mix];

        // The DSP exposes three routing buses, but games commonly leave one or both auxiliary
        // buses at exact zero gain. A zero-to-zero frame contributes nothing even for signed zero;
        // NaN and every nonzero ramp still take the arithmetic path.
        const bool silent = !HasAudibleGain(gains) && (!ramp_active || !HasAudibleGain(ramp_start));
        if (!silent) {
            MixSourceFrame<true>(dest[mix], current_frame, ramp_start, gains, ramp_active);
        }

        if (ramp_active) {
            state.gain_ramp_start[mix] = gains;
            state.gain_ramp_active[mix] = false;
        }
    }
}

CITRA_NO_INLINE u32 Source::MixIntoFirst(std::array<PlanarQuadFrame32, 3>& dest) {
    if (!state.enabled) {
        state.gain_ramp_start = state.gain;
        state.gain_ramp_active.fill(false);
        return 0;
    }

    u32 defined_mask = 0;
    for (std::size_t mix = 0; mix < dest.size(); ++mix) {
        const std::array<float, 4>& gains = state.gain[mix];
        const bool ramp_active = state.gain_ramp_active[mix];
        const std::array<float, 4>& ramp_start = state.gain_ramp_start[mix];
        const bool silent = !HasAudibleGain(gains) && (!ramp_active || !HasAudibleGain(ramp_start));
        if (!silent) {
            DefineSourceFrame(dest[mix], current_frame, ramp_start, gains, ramp_active);
            defined_mask |= u32{1} << mix;
        }

        if (ramp_active) {
            state.gain_ramp_start[mix] = gains;
            state.gain_ramp_active[mix] = false;
        }
    }
    return defined_mask;
}

void Source::Reset() {
    current_frame.fill({});
    state = {};
}

void Source::Sleep() {
    backup_frame = current_frame;
    backup_state = state;
}

void Source::Wakeup() {
    current_frame = backup_frame;
    state = backup_state;
    backup_frame.fill({});
    backup_state = {};
}

void Source::SetMemory(Memory::MemorySystem& memory) {
    memory_system = &memory;
}

void Source::ParseConfig(SourceConfiguration::Configuration& config,
                         const s16_le (&adpcm_coeffs)[16]) {
    if (!config.dirty_raw) {
        return;
    }

    if (config.reset_flag) {
        config.reset_flag.Assign(0);
        Reset();
        LOG_TRACE(Audio_DSP, "source_id={} reset", source_id);
    }

    if (config.partial_reset_flag) {
        config.partial_reset_flag.Assign(0);
        state.input_queue = std::priority_queue<Buffer, std::vector<Buffer>, BufferOrder>{};
        LOG_TRACE(Audio_DSP, "source_id={} partial_reset", source_id);
    }

    if (config.enable_dirty) {
        config.enable_dirty.Assign(0);
        state.enabled = config.enable != 0;
        LOG_TRACE(Audio_DSP, "source_id={} enable={}", source_id, state.enabled);
    }

    if (config.sync_count_dirty) {
        config.sync_count_dirty.Assign(0);
        state.sync_count = config.sync_count;
        LOG_TRACE(Audio_DSP, "source_id={} sync={}", source_id, state.sync_count);
    }

    if (config.rate_multiplier_dirty) {
        config.rate_multiplier_dirty.Assign(0);
        state.rate_multiplier = config.rate_multiplier;
        LOG_TRACE(Audio_DSP, "source_id={} rate={}", source_id, state.rate_multiplier);

        if (state.rate_multiplier <= 0) {
            LOG_ERROR(Audio_DSP, "Was given an invalid rate multiplier: source_id={} rate={}",
                      source_id, state.rate_multiplier);
            state.rate_multiplier = 1.0f;
            // Note: Actual firmware starts producing garbage if this occurs.
        }
    }

    if (config.adpcm_coefficients_dirty) {
        config.adpcm_coefficients_dirty.Assign(0);
        std::transform(adpcm_coeffs, adpcm_coeffs + state.adpcm_coeffs.size(),
                       state.adpcm_coeffs.begin(),
                       [](const auto& coeff) { return static_cast<s16>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id={} adpcm update", source_id);
    }

    if (config.gain_0_dirty) {
        config.gain_0_dirty.Assign(0);
        state.gain_ramp_start[0] = state.gain[0];
        state.gain_ramp_active[0] = true;
        std::transform(config.gain[0], config.gain[0] + state.gain[0].size(), state.gain[0].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id={} gain 0 update", source_id);
    }

    if (config.gain_1_dirty) {
        config.gain_1_dirty.Assign(0);
        state.gain_ramp_start[1] = state.gain[1];
        state.gain_ramp_active[1] = true;
        std::transform(config.gain[1], config.gain[1] + state.gain[1].size(), state.gain[1].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id={} gain 1 update", source_id);
    }

    if (config.gain_2_dirty) {
        config.gain_2_dirty.Assign(0);
        state.gain_ramp_start[2] = state.gain[2];
        state.gain_ramp_active[2] = true;
        std::transform(config.gain[2], config.gain[2] + state.gain[2].size(), state.gain[2].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id={} gain 2 update", source_id);
    }

    if (config.filters_enabled_dirty) {
        config.filters_enabled_dirty.Assign(0);
        state.filters.Enable(static_cast<bool>(config.simple_filter_enabled),
                             static_cast<bool>(config.biquad_filter_enabled));
        LOG_TRACE(Audio_DSP, "source_id={} enable_simple={} enable_biquad={}", source_id,
                  config.simple_filter_enabled.Value(), config.biquad_filter_enabled.Value());
    }

    if (config.simple_filter_dirty) {
        config.simple_filter_dirty.Assign(0);
        state.filters.Configure(config.simple_filter);
        LOG_TRACE(Audio_DSP, "source_id={} simple filter update", source_id);
    }

    if (config.biquad_filter_dirty) {
        config.biquad_filter_dirty.Assign(0);
        state.filters.Configure(config.biquad_filter);
        LOG_TRACE(Audio_DSP, "source_id={} biquad filter update", source_id);
    }

    if (config.interpolation_dirty) {
        config.interpolation_dirty.Assign(0);
        state.interpolation_mode = config.interpolation_mode;
        LOG_TRACE(Audio_DSP, "source_id={} interpolation_mode={}", source_id,
                  static_cast<std::size_t>(state.interpolation_mode));
    }

    if (config.format_dirty || config.embedded_buffer_dirty) {
        config.format_dirty.Assign(0);
        state.format = config.format;
        LOG_TRACE(Audio_DSP, "source_id={} format={}", source_id,
                  static_cast<std::size_t>(state.format));
    }

    if (config.mono_or_stereo_dirty || config.embedded_buffer_dirty) {
        config.mono_or_stereo_dirty.Assign(0);
        state.mono_or_stereo = config.mono_or_stereo;
        LOG_TRACE(Audio_DSP, "source_id={} mono_or_stereo={}", source_id,
                  static_cast<std::size_t>(state.mono_or_stereo));
    }

    // play_position applies only to the embedded buffer, and defaults to 0 w/o a dirty bit
    // This will be the starting sample for the first time the buffer is played.
    u32_dsp play_position = {};
    if (config.play_position_dirty) {
        config.play_position_dirty.Assign(0);
        play_position = config.play_position;
    }

    // TODO(xperia64): Is this in the correct spot in terms of the bit handling order?
    if (config.partial_embedded_buffer_dirty) {
        config.partial_embedded_buffer_dirty.Assign(0);

        // As this bit is set by the game, three config options are also updated:
        // buffer_id (after a check comparing the buffer_id to something, probably to make sure it's
        // the same buffer?), flags2_raw.is_looping, and length.

        // Use the latched physical address instead of the current config value, which may be
        // invalid. PCM16 can decode directly from the current position instead of rebuilding and
        // then erasing the already-consumed prefix.
        const u8* const memory =
            memory_system->GetPhysicalPointer(state.current_buffer_physical_address & 0xFFFFFFFC);

        if (memory) {
            const unsigned num_channels = state.mono_or_stereo == MonoOrStereo::Stereo ? 2 : 1;
            switch (state.format) {
            case Format::PCM8:
                // TODO(xperia64): This may just work fine like PCM16, but I haven't tested and
                // couldn't find any test case games
                UNIMPLEMENTED_MSG("{} not handled for partial buffer updates", "PCM8");
                // state.current_buffer = Codec::DecodePCM8(num_channels, memory, config.length);
                break;
            case Format::PCM16:
                // Tomodachi Life can shrink the declared length when dialogue is skipped. Keep the
                // established reset behavior in that case; otherwise decode only the suffix that
                // the old full-decode-plus-erase path retained.
                if (config.length < state.current_sample_number) {
                    state.current_sample_number = 0;
                }
                state.current_buffer = Codec::DecodePCM16FromSample(
                    num_channels, memory, config.length, state.current_sample_number);
                state.current_buffer_length = config.length;
                state.current_buffer_mono_or_stereo = state.mono_or_stereo;
                state.current_buffer_format = state.format;
                state.current_buffer_is_looping = config.is_looping != 0;
                break;
            case Format::ADPCM:
                // TODO(xperia64): Are partial embedded buffer updates even valid for ADPCM? What
                // about the adpcm state?
                UNIMPLEMENTED_MSG("{} not handled for partial buffer updates", "ADPCM");
                /* state.current_buffer =
                    Codec::DecodeADPCM(memory, config.length, state.adpcm_coeffs,
                   state.adpcm_state); */
                break;
            default:
                UNIMPLEMENTED();
                break;
            }
        }
        LOG_TRACE(Audio_DSP, "partially updating embedded buffer addr={:#010x} len={} id={}",
                  state.current_buffer_physical_address, static_cast<u32>(config.length),
                  config.buffer_id);
    }

    if (config.embedded_buffer_dirty) {
        config.embedded_buffer_dirty.Assign(0);
        // HACK
        // Luigi's Mansion Dark Moon configures the embedded buffer with an extremely large value
        // for length, causing the Dequeue method to allocate a buffer of that size, eating up all
        // of the users RAM. It appears that the game is calculating the length of the sample by
        // using some value from the DSP and subtracting another value, which causes it to
        // underflow. We need to investigate further into what value the game is reading from and
        // fix that, but as a stop gap, we can just prevent these underflowed values from playing in
        // the mean time
        if (static_cast<s32>(config.length) < 0) {
            LOG_ERROR(
                Audio_DSP,
                "Skipping embedded buffer sample! Application passed in improper value for length. "
                "addr {:X} length {:X}",
                static_cast<u32>(config.physical_address), static_cast<u32>(config.length));
        } else {
            state.input_queue.emplace(Buffer{
                config.physical_address,
                config.length,
                static_cast<u8>(config.adpcm_ps),
                {config.adpcm_yn[0], config.adpcm_yn[1]},
                static_cast<bool>(config.adpcm_dirty),
                static_cast<bool>(config.is_looping),
                config.buffer_id,
                state.mono_or_stereo,
                state.format,
                false,
                play_position,
                false,
            });
        }
        LOG_TRACE(Audio_DSP, "enqueuing embedded addr={:#010x} len={} id={} start={}",
                  static_cast<u32>(config.physical_address), static_cast<u32>(config.length),
                  config.buffer_id, static_cast<u32>(config.play_position));
    }

    if (config.loop_related_dirty && config.loop_related != 0) {
        config.loop_related_dirty.Assign(0);
        LOG_WARNING(Audio_DSP, "Unhandled complex loop with loop_related={:#010x}",
                    static_cast<u32>(config.loop_related));
    }

    if (config.buffer_queue_dirty) {
        config.buffer_queue_dirty.Assign(0);
        for (std::size_t i = 0; i < 4; i++) {
            if (config.buffers_dirty & (1 << i)) {
                const auto& b = config.buffers[i];
                if (static_cast<s32>(b.length) < 0) {
                    LOG_ERROR(Audio_DSP,
                              "Skipping buffer queue sample! Game passed in improper value for "
                              "length. addr {:X} length {:X}",
                              static_cast<u32>(b.physical_address), static_cast<u32>(b.length));
                } else {
                    state.input_queue.emplace(Buffer{
                        b.physical_address,
                        b.length,
                        static_cast<u8>(b.adpcm_ps),
                        {b.adpcm_yn[0], b.adpcm_yn[1]},
                        b.adpcm_dirty != 0,
                        b.is_looping != 0,
                        b.buffer_id,
                        state.mono_or_stereo,
                        state.format,
                        true,  // from_queue
                        0,     // play_position
                        false, // has_played
                    });
                }
                LOG_TRACE(Audio_DSP, "enqueuing queued {} addr={:#010x} len={} id={}", i,
                          static_cast<u32>(b.physical_address), static_cast<u32>(b.length),
                          b.buffer_id);
            }
        }
        config.buffers_dirty = 0;
    }

    if (config.dirty_raw) {
        LOG_DEBUG(Audio_DSP, "source_id={} remaining_dirty={:x}", source_id, config.dirty_raw);
    }

    config.dirty_raw = 0;
}

void Source::GenerateFrame() {
    // A looping PCM buffer can be updated by the application while the DSP is
    // playing it. Refresh only the not-yet-consumed part from emulated RAM.
    //
    // current_sample_number is maintained below from the exact amount of input
    // removed by AudioInterp, so the memory offset and current_buffer stay in sync.
    if (!state.current_buffer.empty() && state.current_buffer_is_looping &&
        state.current_buffer_format == Format::PCM16 &&
        state.current_buffer_physical_address != 0 &&
        state.current_sample_number < state.current_buffer_length) {
        const unsigned num_channels =
            state.current_buffer_mono_or_stereo == MonoOrStereo::Stereo ? 2 : 1;

        const u32 byte_offset =
            state.current_sample_number * num_channels * static_cast<u32>(sizeof(s16));
        const PAddr physical_address =
            (state.current_buffer_physical_address & 0xFFFFFFFC) + byte_offset;
        const u8* const memory = memory_system->GetPhysicalPointer(physical_address);

        if (memory) {
            const u32 remaining_samples = state.current_buffer_length - state.current_sample_number;
            state.current_buffer = Codec::DecodePCM16(num_channels, memory, remaining_samples);
        }
    }

    if (state.current_buffer.empty()) {
        // A dequeued buffer intentionally starts on the next DSP frame, so every early return
        // from this path must replace the previous frame with silence.
        current_frame.fill({});

        // TODO(SachinV): Should dequeue happen at the end of the frame generation?
        if (DequeueBuffer()) {
            return;
        }
        state.enabled = false;
        state.buffer_update = true;
        state.last_buffer_id = state.current_buffer_id;
        state.current_buffer_id = 0;
        return;
    }

    std::size_t frame_position = 0;
    while (frame_position < current_frame.size()) {
        if (state.current_buffer.empty() && !DequeueBuffer()) {
            break;
        }

        // AudioInterp consumes samples from current_buffer and already preserves
        // its own fractional phase/history. Use what it actually consumed instead
        // of reconstructing the source position with a truncated float product.
        const std::size_t input_size_before = state.current_buffer.size();

        switch (state.interpolation_mode) {
        case InterpolationMode::None:
            AudioInterp::None(state.interp_state, state.current_buffer, state.rate_multiplier,
                              current_frame, frame_position);
            break;
        case InterpolationMode::Linear:
            AudioInterp::Linear(state.interp_state, state.current_buffer, state.rate_multiplier,
                                current_frame, frame_position);
            break;
        case InterpolationMode::Polyphase:
            // TODO(merry): Implement polyphase interpolation
            AudioInterp::Linear(state.interp_state, state.current_buffer, state.rate_multiplier,
                                current_frame, frame_position);
            break;
        default:
            UNIMPLEMENTED();
            break;
        }

        const std::size_t input_size_after = state.current_buffer.size();
        ASSERT(input_size_after <= input_size_before);
        state.current_sample_number += static_cast<u32>(input_size_before - input_size_after);
    }

    // Resampling overwrites the complete produced prefix. Clear only an unwritten underrun tail
    // instead of writing silence over every active frame immediately before replacing it.
    if (frame_position < current_frame.size()) {
        std::fill(current_frame.begin() + frame_position, current_frame.end(),
                  std::array<s16, 2>{});
    }

    state.filters.ProcessFrame(current_frame);
}

bool Source::DequeueBuffer() {
    ASSERT_MSG(state.current_buffer.empty(),
               "Shouldn't dequeue; we still have data in current_buffer");

    if (state.input_queue.empty())
        return false;

    Buffer buf = state.input_queue.top();
    state.input_queue.pop();

    if (buf.adpcm_dirty) {
        state.adpcm_state.yn1 = buf.adpcm_yn[0];
        state.adpcm_state.yn2 = buf.adpcm_yn[1];
    }

    // This physical address masking occurs due to how the DSP DMA hardware is configured by the
    // firmware.
    const u8* const memory = memory_system->GetPhysicalPointer(buf.physical_address & 0xFFFFFFFC);
    if (memory) {
        const unsigned num_channels = buf.mono_or_stereo == MonoOrStereo::Stereo ? 2 : 1;
        switch (buf.format) {
        case Format::PCM8:
            state.current_buffer = Codec::DecodePCM8(num_channels, memory, buf.length);
            break;
        case Format::PCM16:
            state.current_buffer = Codec::DecodePCM16(num_channels, memory, buf.length);
            break;
        case Format::ADPCM:
            DEBUG_ASSERT(num_channels == 1);
            state.current_buffer =
                Codec::DecodeADPCM(memory, buf.length, state.adpcm_coeffs, state.adpcm_state);
            break;
        default:
            UNIMPLEMENTED();
            break;
        }
    } else {
        LOG_WARNING(Audio_DSP,
                    "source_id={} buffer_id={} length={}: Invalid physical address {:#010x}",
                    source_id, buf.buffer_id, buf.length, buf.physical_address);
        state.current_buffer.clear();
        return true;
    }

    // the first playthrough starts at play_position, loops start at the beginning of the buffer
    state.current_sample_number = (!buf.has_played) ? buf.play_position : 0;
    state.current_buffer_physical_address = buf.physical_address;
    state.current_buffer_length = buf.length;
    state.current_buffer_mono_or_stereo = buf.mono_or_stereo;
    state.current_buffer_format = buf.format;
    state.current_buffer_is_looping = buf.is_looping;
    state.current_buffer_id = buf.buffer_id;
    state.last_buffer_id = 0;
    state.buffer_update = buf.from_queue && !buf.has_played;

    if (buf.is_looping) {
        buf.has_played = true;
        state.input_queue.push(buf);
    }

    // Because our interpolation consumes samples instead of using an index,
    // let's just consume the samples up to the current sample number.
    state.current_buffer.erase(
        state.current_buffer.begin(),
        std::next(state.current_buffer.begin(), state.current_sample_number));

    LOG_TRACE(Audio_DSP,
              "source_id={} buffer_id={} from_queue={} current_buffer.size()={}, "
              "buf.has_played={}, buf.play_position={}",
              source_id, buf.buffer_id, buf.from_queue, state.current_buffer.size(), buf.has_played,
              buf.play_position);
    return true;
}

SourceStatus::Status Source::GetCurrentStatus() {
    SourceStatus::Status ret;

    // Applications depend on the correct emulation of
    // current_buffer_id_dirty and current_buffer_id to synchronise
    // audio with video.
    ret.is_enabled = state.enabled;
    ret.current_buffer_id_dirty = state.buffer_update ? 1 : 0;
    state.buffer_update = false;
    ret.sync_count = state.sync_count;
    ret.buffer_position = state.current_sample_number;
    ret.current_buffer_id = state.current_buffer_id;
    ret.last_buffer_id = state.last_buffer_id;

    return ret;
}

} // namespace AudioCore::HLE
