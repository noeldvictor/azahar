// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/hle/mixers.h"

namespace {

using AudioCore::HLE::DspConfiguration;

s16 ClampToS16(s32 value) {
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}

AudioCore::StereoFrame16 ReferenceMix(
    DspConfiguration::OutputFormat format, const std::array<float, 3>& gains,
    const std::array<AudioCore::QuadFrame32, 3>& input) {
    AudioCore::StereoFrame16 output{};
    for (std::size_t mix = 0; mix < input.size(); ++mix) {
        const float gain = gains[mix];
        for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
            const auto& in = input[mix][sample];
            if (format == DspConfiguration::OutputFormat::Mono) {
                const s16 mono = ClampToS16(static_cast<s32>(
                    (gain * in[0] + gain * in[1] + gain * in[2] + gain * in[3]) / 2));
                output[sample][0] =
                    ClampToS16(static_cast<s32>(output[sample][0]) + mono);
                output[sample][1] =
                    ClampToS16(static_cast<s32>(output[sample][1]) + mono);
            } else {
                const s16 left = ClampToS16(static_cast<s32>(gain * in[0] + gain * in[2]));
                const s16 right = ClampToS16(static_cast<s32>(gain * in[1] + gain * in[3]));
                output[sample][0] =
                    ClampToS16(static_cast<s32>(output[sample][0]) + left);
                output[sample][1] =
                    ClampToS16(static_cast<s32>(output[sample][1]) + right);
            }
        }
    }
    return output;
}

void CheckMix(DspConfiguration::OutputFormat format) {
    constexpr std::array<float, 3> gains{0.5f, 0.25f, 0.125f};
    constexpr std::array<s32, 12> values{
        -131072, -65536, -32769, -32768, -1, 0, 1, 32767, 32768, 65535, 65536, 131072,
    };

    std::array<AudioCore::QuadFrame32, 3> input{};
    for (std::size_t mix = 0; mix < input.size(); ++mix) {
        for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::size_t index = (sample * 5 + channel * 3 + mix * 7) % values.size();
                input[mix][sample][channel] = values[index];
            }
        }
    }

    DspConfiguration config{};
    config.master_volume = gains[0];
    config.aux_return_volume[0] = gains[1];
    config.aux_return_volume[1] = gains[2];
    config.master_volume_dirty.Assign(1);
    config.aux_return_volume_0_dirty.Assign(1);
    config.aux_return_volume_1_dirty.Assign(1);
    config.output_format = format;
    config.output_format_dirty.Assign(1);

    AudioCore::HLE::IntermediateMixSamples read_samples{};
    AudioCore::HLE::IntermediateMixSamples write_samples{};
    AudioCore::HLE::Mixers mixers;
    mixers.Tick(config, read_samples, write_samples, input);

    REQUIRE(mixers.GetOutput() == ReferenceMix(format, gains, input));
}

} // namespace

TEST_CASE("HLE mixer downmix matches scalar saturation", "[audio_core][hle][mixers]") {
    SECTION("Mono") {
        CheckMix(DspConfiguration::OutputFormat::Mono);
    }
    SECTION("Stereo") {
        CheckMix(DspConfiguration::OutputFormat::Stereo);
    }
    SECTION("Surround follows stereo") {
        CheckMix(DspConfiguration::OutputFormat::Surround);
    }
}
