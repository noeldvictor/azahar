// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "audio_core/hle/common.h"
#include "audio_core/hle/filter.h"
#include "audio_core/hle/shared_memory.h"
#include "common/common_types.h"

namespace AudioCore::HLE {

#if defined(__aarch64__)
namespace {

int16x4_t LoadStereo(const std::array<s16, 2>& sample) {
    static_assert(sizeof(sample) == sizeof(u32));
    u32 packed;
    std::memcpy(&packed, sample.data(), sizeof(packed));
    return vcreate_s16(packed);
}

std::array<s16, 2> StoreStereo(int16x4_t sample) {
    const u32 packed = vget_lane_u32(vreinterpret_u32_s16(sample), 0);
    std::array<s16, 2> result;
    std::memcpy(result.data(), &packed, sizeof(packed));
    return result;
}

int16x4_t BroadcastCoefficient(s32 coefficient) {
    return vdup_n_s16(static_cast<s16>(coefficient));
}

} // Anonymous namespace
#endif

void SourceFilters::Reset() {
    Enable(false, false);
}

void SourceFilters::Enable(bool simple, bool biquad) {
    simple_filter_enabled = simple;
    biquad_filter_enabled = biquad;

    if (!simple)
        simple_filter.Reset();
    if (!biquad)
        biquad_filter.Reset();
}

void SourceFilters::Configure(SourceConfiguration::Configuration::SimpleFilter config) {
    simple_filter.Configure(config);
}

void SourceFilters::Configure(SourceConfiguration::Configuration::BiquadFilter config) {
    biquad_filter.Configure(config);
}

void SourceFilters::ProcessFrame(StereoFrame16& frame) {
    if (!simple_filter_enabled && !biquad_filter_enabled)
        return;

    if (simple_filter_enabled) {
        if (simple_filter.IsPassthrough()) {
            simple_filter.AdvancePassthrough(frame);
        } else {
            simple_filter.ProcessFrame(frame);
        }
    }

    if (biquad_filter_enabled) {
        if (biquad_filter.IsPassthrough()) {
            biquad_filter.AdvancePassthrough(frame);
        } else {
            biquad_filter.ProcessFrame(frame);
        }
    }
}

// SimpleFilter

void SourceFilters::SimpleFilter::Reset() {
    y1.fill(0);
    // Configure as passthrough.
    a1 = 0;
    b0 = 1 << 15;
}

void SourceFilters::SimpleFilter::Configure(
    SourceConfiguration::Configuration::SimpleFilter config) {

    a1 = config.a1;
    b0 = config.b0;
}

bool SourceFilters::SimpleFilter::IsPassthrough() const {
    return a1 == 0 && b0 == 1 << 15;
}

void SourceFilters::SimpleFilter::AdvancePassthrough(const StereoFrame16& frame) {
    y1 = frame.back();
}

void SourceFilters::SimpleFilter::ProcessFrame(StereoFrame16& frame) {
#if defined(__aarch64__)
    const int16x4_t b0_vector = BroadcastCoefficient(b0);
    const int16x4_t a1_vector = BroadcastCoefficient(a1);
    int16x4_t previous = LoadStereo(y1);

    for (auto& sample : frame) {
        const int16x4_t input = LoadStereo(sample);
        int32x4_t accumulator = vmull_s16(input, b0_vector);
        accumulator = vmlal_s16(accumulator, previous, a1_vector);
        previous = vqmovn_s32(vshrq_n_s32(accumulator, 15));
        sample = StoreStereo(previous);
    }

    y1 = StoreStereo(previous);
#else
    FilterFrame(frame, *this);
#endif
}

std::array<s16, 2> SourceFilters::SimpleFilter::ProcessSample(const std::array<s16, 2>& x0) {
    std::array<s16, 2> y0;
    for (std::size_t i = 0; i < 2; i++) {
        const s32 tmp = (b0 * x0[i] + a1 * y1[i]) >> 15;
        y0[i] = std::clamp(tmp, -32768, 32767);
    }

    y1 = y0;

    return y0;
}

// BiquadFilter

void SourceFilters::BiquadFilter::Reset() {
    x1.fill(0);
    x2.fill(0);
    y1.fill(0);
    y2.fill(0);
    // Configure as passthrough.
    a1 = a2 = b1 = b2 = 0;
    b0 = 1 << 14;
}

void SourceFilters::BiquadFilter::Configure(
    SourceConfiguration::Configuration::BiquadFilter config) {

    a1 = config.a1;
    a2 = config.a2;
    b0 = config.b0;
    b1 = config.b1;
    b2 = config.b2;
}

bool SourceFilters::BiquadFilter::IsPassthrough() const {
    return a1 == 0 && a2 == 0 && b0 == 1 << 14 && b1 == 0 && b2 == 0;
}

void SourceFilters::BiquadFilter::AdvancePassthrough(const StereoFrame16& frame) {
    x2 = frame[frame.size() - 2];
    x1 = frame.back();
    y2 = x2;
    y1 = x1;
}

void SourceFilters::BiquadFilter::ProcessFrame(StereoFrame16& frame) {
#if defined(__aarch64__)
    const int16x4_t b0_vector = BroadcastCoefficient(b0);
    const int16x4_t b1_vector = BroadcastCoefficient(b1);
    const int16x4_t b2_vector = BroadcastCoefficient(b2);
    const int16x4_t a1_vector = BroadcastCoefficient(a1);
    const int16x4_t a2_vector = BroadcastCoefficient(a2);
    int16x4_t previous_x1 = LoadStereo(x1);
    int16x4_t previous_x2 = LoadStereo(x2);
    int16x4_t previous_y1 = LoadStereo(y1);
    int16x4_t previous_y2 = LoadStereo(y2);

    for (auto& sample : frame) {
        const int16x4_t input = LoadStereo(sample);
        int32x4_t accumulator = vmull_s16(input, b0_vector);
        accumulator = vmlal_s16(accumulator, previous_x1, b1_vector);
        accumulator = vmlal_s16(accumulator, previous_x2, b2_vector);
        accumulator = vmlal_s16(accumulator, previous_y1, a1_vector);
        accumulator = vmlal_s16(accumulator, previous_y2, a2_vector);
        const int16x4_t output = vqmovn_s32(vshrq_n_s32(accumulator, 14));
        sample = StoreStereo(output);

        previous_x2 = previous_x1;
        previous_x1 = input;
        previous_y2 = previous_y1;
        previous_y1 = output;
    }

    x1 = StoreStereo(previous_x1);
    x2 = StoreStereo(previous_x2);
    y1 = StoreStereo(previous_y1);
    y2 = StoreStereo(previous_y2);
#else
    FilterFrame(frame, *this);
#endif
}

std::array<s16, 2> SourceFilters::BiquadFilter::ProcessSample(const std::array<s16, 2>& x0) {
    std::array<s16, 2> y0;
    for (std::size_t i = 0; i < 2; i++) {
        const s32 tmp = (b0 * x0[i] + b1 * x1[i] + b2 * x2[i] + a1 * y1[i] + a2 * y2[i]) >> 14;
        y0[i] = std::clamp(tmp, -32768, 32767);
    }

    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;

    return y0;
}

} // namespace AudioCore::HLE
