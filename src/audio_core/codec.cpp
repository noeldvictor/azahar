// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include "audio_core/audio_types.h"
#include "audio_core/codec.h"
#include "common/assert.h"
#include "common/common_types.h"

namespace AudioCore::Codec {

namespace {

constexpr int SignedNibble(u8 nibble) {
    // Moving the low nibble into an s8's sign position lets AArch64 Clang lower this to one SBFX
    // instead of an indexed lookup. The input's upper bits are deliberately discarded.
    const u8 shifted = static_cast<u8>(nibble << 4);
    return std::bit_cast<s8>(shifted) / 16;
}

} // Anonymous namespace

StereoBuffer16 DecodeADPCM(const u8* const data, const std::size_t sample_count,
                           const std::array<s16, 16>& adpcm_coeff, ADPCMState& state) {
    // GC-ADPCM with scale factor and variable coefficients.
    // Frames are 8 bytes long containing 14 samples each.
    // Samples are 4 bits (one nibble) long.

    constexpr std::size_t FRAME_LEN = 8;
    constexpr std::size_t SAMPLES_PER_FRAME = 14;
    const std::size_t ret_size =
        sample_count % 2 == 0 ? sample_count : sample_count + 1; // Ensure multiple of two.
    StereoBuffer16 ret(ret_size);

    int yn1 = state.yn1, yn2 = state.yn2;

    const std::size_t NUM_FRAMES =
        (sample_count + (SAMPLES_PER_FRAME - 1)) / SAMPLES_PER_FRAME; // Round up.
    for (std::size_t framei = 0; framei < NUM_FRAMES; framei++) {
        const int frame_header = data[framei * FRAME_LEN];
        const int scale = 1 << (frame_header & 0xF);
        const int idx = (frame_header >> 4) & 0x7;

        // Coefficients are fixed point with 11 bits fractional part.
        const int coef1 = adpcm_coeff[idx * 2 + 0];
        const int coef2 = adpcm_coeff[idx * 2 + 1];

        // Decodes an audio sample. One nibble produces one sample.
        const auto decode_sample = [&](const int nibble) -> s16 {
            const int xn = nibble * scale;
            // We first transform everything into 11 bit fixed point, perform the second order
            // digital filter, then transform back.
            // 0x400 == 0.5 in 11 bit fixed point.
            // Filter: y[n] = x[n] + 0.5 + c1 * y[n-1] + c2 * y[n-2]
            int val = ((xn << 11) + 0x400 + coef1 * yn1 + coef2 * yn2) >> 11;
            // Clamp to output range.
            val = std::clamp(val, -32768, 32767);
            // Advance output feedback.
            yn2 = yn1;
            yn1 = val;
            return (s16)val;
        };

        std::size_t outputi = framei * SAMPLES_PER_FRAME;
        std::size_t datai = framei * FRAME_LEN + 1;
        for (std::size_t i = 0; i < SAMPLES_PER_FRAME && outputi < sample_count; i += 2) {
            const u8 nibbles = data[datai++];
            const s16 sample1 = decode_sample(SignedNibble(nibbles >> 4));
            ret[outputi].fill(sample1);
            outputi++;

            const s16 sample2 = decode_sample(SignedNibble(nibbles));
            ret[outputi].fill(sample2);
            outputi++;
        }
    }

    state.yn1 = static_cast<s16>(yn1);
    state.yn2 = static_cast<s16>(yn2);

    return ret;
}

StereoBuffer16 DecodePCM8(const unsigned num_channels, const u8* const data,
                          const std::size_t sample_count) {
    ASSERT(num_channels == 1 || num_channels == 2);

    const auto decode_sample = [](u8 sample) {
        return static_cast<s16>(static_cast<u16>(sample) << 8);
    };

    StereoBuffer16 ret(sample_count);
    auto output = ret.begin();

    if (num_channels == 1) {
        const u8* input = data;
        for (std::size_t remaining = sample_count; remaining != 0; --remaining, ++output) {
            output->fill(decode_sample(*input++));
        }
    } else {
        const u8* input = data;
        for (std::size_t remaining = sample_count; remaining != 0; --remaining, ++output) {
            (*output)[0] = decode_sample(*input++);
            (*output)[1] = decode_sample(*input++);
        }
    }

    return ret;
}

StereoBuffer16 DecodePCM16(const unsigned num_channels, const u8* const data,
                           const std::size_t sample_count) {
    ASSERT(num_channels == 1 || num_channels == 2);

    StereoBuffer16 ret(sample_count);
    auto output = ret.begin();

    if (num_channels == 1) {
        const u8* input = data;
        for (std::size_t remaining = sample_count; remaining != 0; --remaining, ++output) {
            s16 sample;
            std::memcpy(&sample, input, sizeof(sample));
            input += sizeof(sample);
            output->fill(sample);
        }
    } else {
        const u8* input = data;
        for (std::size_t remaining = sample_count; remaining != 0; --remaining, ++output) {
            std::memcpy(&*output, input, sizeof(*output));
            input += sizeof(*output);
        }
    }

    return ret;
}

StereoBuffer16 DecodePCM16FromSample(const unsigned num_channels, const u8* const data,
                                     const std::size_t sample_count,
                                     const std::size_t first_sample) {
    ASSERT(first_sample <= sample_count);
    if (first_sample == 0) {
        return DecodePCM16(num_channels, data, sample_count);
    }
    const u8* const first_data = data + first_sample * num_channels * sizeof(s16);
    return DecodePCM16(num_channels, first_data, sample_count - first_sample);
}
} // namespace AudioCore::Codec
