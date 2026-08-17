// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/hle/filter.h"

namespace {

using AudioCore::HLE::SourceConfiguration;

class ReferenceFilters {
public:
    void ConfigureSimple(s16 b0_, s16 a1_) {
        simple_b0 = b0_;
        simple_a1 = a1_;
    }

    void ConfigureBiquad(s16 b0_, s16 b1_, s16 b2_, s16 a1_, s16 a2_) {
        biquad_b0 = b0_;
        biquad_b1 = b1_;
        biquad_b2 = b2_;
        biquad_a1 = a1_;
        biquad_a2 = a2_;
    }

    void Process(AudioCore::StereoFrame16& frame, bool simple, bool biquad) {
        if (simple) {
            for (auto& sample : frame) {
                for (std::size_t channel = 0; channel < sample.size(); ++channel) {
                    const s32 value =
                        (simple_b0 * sample[channel] + simple_a1 * simple_y1[channel]) >> 15;
                    sample[channel] = Clamp(value);
                }
                simple_y1 = sample;
            }
        }

        if (biquad) {
            for (auto& sample : frame) {
                const auto input = sample;
                for (std::size_t channel = 0; channel < sample.size(); ++channel) {
                    const s32 value =
                        (biquad_b0 * input[channel] + biquad_b1 * biquad_x1[channel] +
                         biquad_b2 * biquad_x2[channel] + biquad_a1 * biquad_y1[channel] +
                         biquad_a2 * biquad_y2[channel]) >>
                        14;
                    sample[channel] = Clamp(value);
                }
                biquad_x2 = biquad_x1;
                biquad_x1 = input;
                biquad_y2 = biquad_y1;
                biquad_y1 = sample;
            }
        }
    }

private:
    static s16 Clamp(s32 value) {
        return static_cast<s16>(std::clamp(value, -32768, 32767));
    }

    s32 simple_a1{};
    s32 simple_b0{1 << 15};
    std::array<s16, 2> simple_y1{};

    s32 biquad_a1{};
    s32 biquad_a2{};
    s32 biquad_b0{1 << 14};
    s32 biquad_b1{};
    s32 biquad_b2{};
    std::array<s16, 2> biquad_x1{};
    std::array<s16, 2> biquad_x2{};
    std::array<s16, 2> biquad_y1{};
    std::array<s16, 2> biquad_y2{};
};

AudioCore::StereoFrame16 MakeInput(std::size_t phase) {
    constexpr std::array<s16, 12> values{
        -32768, -30001, -16384, -257, -1, 0, 1, 319, 8192, 16384, 30000, 32767,
    };
    AudioCore::StereoFrame16 frame{};
    for (std::size_t sample = 0; sample < frame.size(); ++sample) {
        frame[sample][0] = values[(sample * 5 + phase) % values.size()];
        frame[sample][1] = values[(sample * 7 + phase + 3) % values.size()];
    }
    return frame;
}

SourceConfiguration::Configuration::SimpleFilter MakeSimpleConfig(s16 b0, s16 a1) {
    SourceConfiguration::Configuration::SimpleFilter config{};
    config.b0 = b0;
    config.a1 = a1;
    return config;
}

SourceConfiguration::Configuration::BiquadFilter MakeBiquadConfig(s16 b0, s16 b1, s16 b2, s16 a1,
                                                                  s16 a2) {
    SourceConfiguration::Configuration::BiquadFilter config{};
    config.b0 = b0;
    config.b1 = b1;
    config.b2 = b2;
    config.a1 = a1;
    config.a2 = a2;
    return config;
}

TEST_CASE("HLE source filters match sequential scalar reference", "[audio_core][hle][filter]") {
    constexpr s16 simple_b0 = 24576;
    constexpr s16 simple_a1 = -8192;
    constexpr s16 biquad_b0 = 20000;
    constexpr s16 biquad_b1 = -7000;
    constexpr s16 biquad_b2 = 3000;
    constexpr s16 biquad_a1 = 5000;
    constexpr s16 biquad_a2 = -2000;

    const auto simple_config = MakeSimpleConfig(simple_b0, simple_a1);
    const auto biquad_config =
        MakeBiquadConfig(biquad_b0, biquad_b1, biquad_b2, biquad_a1, biquad_a2);

    SECTION("simple") {
        AudioCore::HLE::SourceFilters filters;
        ReferenceFilters reference;
        filters.Enable(true, false);
        filters.Configure(simple_config);
        reference.ConfigureSimple(simple_b0, simple_a1);

        for (std::size_t frame_number = 0; frame_number < 3; ++frame_number) {
            auto actual = MakeInput(frame_number);
            auto expected = actual;
            filters.ProcessFrame(actual);
            reference.Process(expected, true, false);
            REQUIRE(actual == expected);
        }
    }

    SECTION("biquad") {
        AudioCore::HLE::SourceFilters filters;
        ReferenceFilters reference;
        filters.Enable(false, true);
        filters.Configure(biquad_config);
        reference.ConfigureBiquad(biquad_b0, biquad_b1, biquad_b2, biquad_a1, biquad_a2);

        for (std::size_t frame_number = 0; frame_number < 3; ++frame_number) {
            auto actual = MakeInput(frame_number + 4);
            auto expected = actual;
            filters.ProcessFrame(actual);
            reference.Process(expected, false, true);
            REQUIRE(actual == expected);
        }
    }

    SECTION("simple then biquad") {
        AudioCore::HLE::SourceFilters filters;
        ReferenceFilters reference;
        filters.Enable(true, true);
        filters.Configure(simple_config);
        filters.Configure(biquad_config);
        reference.ConfigureSimple(simple_b0, simple_a1);
        reference.ConfigureBiquad(biquad_b0, biquad_b1, biquad_b2, biquad_a1, biquad_a2);

        for (std::size_t frame_number = 0; frame_number < 3; ++frame_number) {
            auto actual = MakeInput(frame_number + 8);
            auto expected = actual;
            filters.ProcessFrame(actual);
            reference.Process(expected, true, true);
            REQUIRE(actual == expected);
        }
    }
}

TEST_CASE("HLE source filter passthrough preserves history", "[audio_core][hle][filter]") {
    AudioCore::HLE::SourceFilters filters;
    ReferenceFilters reference;
    filters.Enable(true, true);

    auto actual = MakeInput(2);
    auto expected = actual;
    filters.ProcessFrame(actual);
    reference.Process(expected, true, true);
    REQUIRE(actual == expected);

    constexpr s16 simple_b0 = 0;
    constexpr s16 simple_a1 = 16384;
    constexpr s16 biquad_b0 = 0;
    constexpr s16 biquad_b1 = 8192;
    constexpr s16 biquad_b2 = 4096;
    constexpr s16 biquad_a1 = 2048;
    constexpr s16 biquad_a2 = -1024;
    filters.Configure(MakeSimpleConfig(simple_b0, simple_a1));
    filters.Configure(MakeBiquadConfig(biquad_b0, biquad_b1, biquad_b2, biquad_a1, biquad_a2));
    reference.ConfigureSimple(simple_b0, simple_a1);
    reference.ConfigureBiquad(biquad_b0, biquad_b1, biquad_b2, biquad_a1, biquad_a2);

    actual.fill({0, 0});
    expected = actual;
    filters.ProcessFrame(actual);
    reference.Process(expected, true, true);
    REQUIRE(actual == expected);
}

} // namespace
