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
    const std::array<AudioCore::PlanarQuadFrame32, 3>& input) {
    AudioCore::StereoFrame16 output{};
    for (std::size_t mix = 0; mix < input.size(); ++mix) {
        const float gain = gains[mix];
        for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
            if (format == DspConfiguration::OutputFormat::Mono) {
                const s16 mono = ClampToS16(static_cast<s32>(
                    (gain * input[mix][0][sample] + gain * input[mix][1][sample] +
                     gain * input[mix][2][sample] + gain * input[mix][3][sample]) /
                    2));
                output[sample][0] =
                    ClampToS16(static_cast<s32>(output[sample][0]) + mono);
                output[sample][1] =
                    ClampToS16(static_cast<s32>(output[sample][1]) + mono);
            } else {
                const s16 left = ClampToS16(static_cast<s32>(
                    gain * input[mix][0][sample] + gain * input[mix][2][sample]));
                const s16 right = ClampToS16(static_cast<s32>(
                    gain * input[mix][1][sample] + gain * input[mix][3][sample]));
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

    std::array<AudioCore::PlanarQuadFrame32, 3> input{};
    for (std::size_t mix = 0; mix < input.size(); ++mix) {
        for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::size_t index = (sample * 5 + channel * 3 + mix * 7) % values.size();
                input[mix][channel][sample] = values[index];
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

TEST_CASE("HLE mixer auxiliary buses preserve planar samples", "[audio_core][hle][mixers]") {
    constexpr std::array<float, 3> gains{0.125f, 0.25f, 0.5f};

    std::array<AudioCore::PlanarQuadFrame32, 3> input{};
    std::array<AudioCore::PlanarQuadFrame32, 3> expected_mix{};
    AudioCore::HLE::IntermediateMixSamples read_samples{};
    AudioCore::HLE::IntermediateMixSamples write_samples{};

    for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            input[0][channel][sample] = static_cast<s32>(sample * 13 + channel * 3 - 900);
            input[1][channel][sample] = static_cast<s32>(sample * 17 + channel * 5 - 1200);
            input[2][channel][sample] = static_cast<s32>(sample * 19 + channel * 7 - 1400);

            read_samples.mix1.pcm32[channel][sample] =
                static_cast<s32>(sample * 23 + channel * 11 - 1600);
            read_samples.mix2.pcm32[channel][sample] =
                static_cast<s32>(sample * 29 + channel * 13 - 1800);

            expected_mix[0][channel][sample] = input[0][channel][sample];
            expected_mix[1][channel][sample] = read_samples.mix1.pcm32[channel][sample];
            expected_mix[2][channel][sample] = read_samples.mix2.pcm32[channel][sample];
        }
    }

    DspConfiguration config{};
    config.aux_bus_enable = {1, 1};
    config.aux_bus_enable_0_dirty.Assign(1);
    config.aux_bus_enable_1_dirty.Assign(1);
    config.master_volume = gains[0];
    config.aux_return_volume = {gains[1], gains[2]};
    config.master_volume_dirty.Assign(1);
    config.aux_return_volume_0_dirty.Assign(1);
    config.aux_return_volume_1_dirty.Assign(1);
    config.output_format = DspConfiguration::OutputFormat::Stereo;
    config.output_format_dirty.Assign(1);

    AudioCore::HLE::Mixers mixers;
    mixers.Tick(config, read_samples, write_samples, input);

    REQUIRE(mixers.GetOutput() ==
            ReferenceMix(DspConfiguration::OutputFormat::Stereo, gains, expected_mix));

    for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            REQUIRE(write_samples.mix1.pcm32[channel][sample] == input[1][channel][sample]);
            REQUIRE(write_samples.mix2.pcm32[channel][sample] == input[2][channel][sample]);
        }
    }
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
