// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "audio_core/interpolate.h"
#include "common/assert.h"

namespace AudioCore::AudioInterp {

// Calculations are done in fixed point with 24 fractional bits.
// (This is not verified. This was chosen for minimal error.)
constexpr u64 scale_factor = 1 << 24;
constexpr u64 scale_mask = scale_factor - 1;

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

std::array<s16, 2> LinearSample(u64 fraction, const std::array<s16, 2>& x0,
                                const std::array<s16, 2>& x1) {
    const int16x4_t x0_vector = LoadStereo(x0);
    const int16x4_t x1_vector = LoadStereo(x1);

    // Saturate the stereo difference to the DSP's signed 16-bit range. Shifting the Q24
    // fraction left by seven turns it into Q31, so SQDMULH computes the same truncated
    // (fraction * delta) >> 24 result for both channels without widening to 64 bits.
    const int16x4_t delta = vqmovn_s32(vsubl_s16(x1_vector, x0_vector));
    const int32x2_t delta_wide = vget_low_s32(vmovl_s16(delta));
    const int32x2_t fraction_q31 = vdup_n_s32(static_cast<s32>(fraction << 7));
    const int32x2_t interpolated = vqdmulh_s32(delta_wide, fraction_q31);
    const int32x2_t base = vget_low_s32(vmovl_s16(x0_vector));
    const int32x2_t result = vadd_s32(base, interpolated);

    return StoreStereo(vmovn_s32(vcombine_s32(result, vdup_n_s32(0))));
}

} // Anonymous namespace
#endif

/// Here we step over the input in steps of rate, until we consume all of the input.
/// Two adjacent samples are passed to fn each step.
template <typename Function>
static void StepOverSamples(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
                            std::size_t& outputi, Function fn) {
    ASSERT(rate > 0);

    if (input.empty())
        return;

    const u64 step_size = static_cast<u64>(rate * scale_factor);
    u64 fposition = state.fposition;
    std::size_t inputi = 0;

    // Treat the two history samples as a virtual prefix instead of inserting them into the deque.
    // The integer input position never moves backwards, so retain its two-sample window and only
    // touch the deque when that position advances. Upsampling can therefore reuse the same window,
    // while the common one-sample advance needs one sequential iterator load instead of two
    // independent deque block-map lookups.
    std::size_t window_index = 0;
    std::array<s16, 2> x0 = state.xn2;
    std::array<s16, 2> x1 = state.xn1;
    auto next_input = input.begin();

    const auto advance_window = [&] {
        while (window_index < inputi) {
            x0 = x1;
            x1 = *next_input;
            ++next_input;
            ++window_index;
        }
    };

    while (outputi < output.size()) {
        inputi = static_cast<std::size_t>(fposition / scale_factor);

        if (inputi >= input.size()) {
            inputi = input.size();
            advance_window();
            break;
        }

        advance_window();
        u64 fraction = fposition & scale_mask;
        output[outputi++] = fn(fraction, x0, x1);

        fposition += step_size;
    }

    state.xn2 = x0;
    state.xn1 = x1;
    state.fposition = fposition - inputi * scale_factor;

    input.erase(input.begin(), next_input);
}

void None(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
          std::size_t& outputi) {
    StepOverSamples(state, input, rate, output, outputi,
                    [](u64 fraction, const auto& x0, const auto& x1) { return x0; });
}

void Linear(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
            std::size_t& outputi) {
    // At an aligned unity rate every interpolation fraction remains zero, so Linear returns x0
    // exactly. Reuse None's lean copy loop instead of running the NEON interpolation sequence.
    if (rate == 1.0f && (state.fposition & scale_mask) == 0) {
        None(state, input, rate, output, outputi);
        return;
    }

    // Note on accuracy: Some values that this produces are +/- 1 from the actual firmware.
    StepOverSamples(state, input, rate, output, outputi,
                    [](u64 fraction, const auto& x0, const auto& x1) {
#if defined(__aarch64__)
                        return LinearSample(fraction, x0, x1);
#else
                        // This is a saturated subtraction. (Verified by black-box fuzzing.)
                        s64 delta0 = std::clamp<s64>(x1[0] - x0[0], -32768, 32767);
                        s64 delta1 = std::clamp<s64>(x1[1] - x0[1], -32768, 32767);

                        return std::array<s16, 2>{
                            static_cast<s16>(x0[0] + fraction * delta0 / scale_factor),
                            static_cast<s16>(x0[1] + fraction * delta1 / scale_factor),
                        };
#endif
                    });
}

} // namespace AudioCore::AudioInterp
