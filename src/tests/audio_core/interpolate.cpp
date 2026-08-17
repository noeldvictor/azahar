// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/interpolate.h"

namespace {

constexpr u64 ScaleFactor = 1 << 24;
constexpr u64 ScaleMask = ScaleFactor - 1;

void ReferenceLinear(AudioCore::AudioInterp::State& state,
                     AudioCore::AudioInterp::StereoBuffer16& input, float rate,
                     AudioCore::StereoFrame16& output, std::size_t& output_index) {
    if (input.empty()) {
        return;
    }

    input.insert(input.begin(), {state.xn2, state.xn1});

    const u64 step_size = static_cast<u64>(rate * ScaleFactor);
    u64 position = state.fposition;
    std::size_t input_index = 0;

    while (output_index < output.size()) {
        input_index = static_cast<std::size_t>(position / ScaleFactor);
        if (input_index + 2 >= input.size()) {
            input_index = input.size() - 2;
            break;
        }

        const u64 fraction = position & ScaleMask;
        const auto& x0 = input[input_index];
        const auto& x1 = input[input_index + 1];
        std::array<s16, 2> sample{};
        for (std::size_t channel = 0; channel < sample.size(); ++channel) {
            const s64 delta = std::clamp<s64>(x1[channel] - x0[channel], -32768, 32767);
            sample[channel] = static_cast<s16>(x0[channel] + fraction * delta / ScaleFactor);
        }
        output[output_index++] = sample;
        position += step_size;
    }

    state.xn2 = input[input_index];
    state.xn1 = input[input_index + 1];
    state.fposition = position - input_index * ScaleFactor;
    input.erase(input.begin(), std::next(input.begin(), input_index + 2));
}

AudioCore::AudioInterp::StereoBuffer16 MakeInput(std::size_t phase) {
    constexpr std::array<s16, 14> Values{
        -32768, 32767, -32767, 32766, -30000, 30000, -16384, 16384, -257, 319, -1, 0, 1, 8192,
    };
    AudioCore::AudioInterp::StereoBuffer16 input;
    for (std::size_t sample = 0; sample < 512; ++sample) {
        input.push_back({Values[(sample * 5 + phase) % Values.size()],
                         Values[(sample * 9 + phase + 3) % Values.size()]});
    }
    return input;
}

TEST_CASE("Linear interpolation matches the scalar DSP reference", "[audio_core][interpolate]") {
    constexpr std::array<float, 6> Rates{0.25f, 0.5f, 0.9999f, 1.0f, 1.25f, 2.75f};
    constexpr std::array<u64, 5> InitialFractions{0, 1, ScaleFactor / 2, ScaleFactor - 2,
                                                  ScaleFactor - 1};

    for (std::size_t rate_index = 0; rate_index < Rates.size(); ++rate_index) {
        for (std::size_t fraction_index = 0; fraction_index < InitialFractions.size();
             ++fraction_index) {
            auto actual_input = MakeInput(rate_index + fraction_index);
            auto expected_input = actual_input;

            AudioCore::AudioInterp::State actual_state{
                .xn1 = {32767, -32768},
                .xn2 = {-32768, 32767},
                .fposition = InitialFractions[fraction_index],
            };
            auto expected_state = actual_state;

            AudioCore::StereoFrame16 actual_output{};
            actual_output.fill({-12345, 23456});
            auto expected_output = actual_output;
            std::size_t actual_output_index = (rate_index + fraction_index) % 19;
            std::size_t expected_output_index = actual_output_index;

            AudioCore::AudioInterp::Linear(actual_state, actual_input, Rates[rate_index],
                                           actual_output, actual_output_index);
            ReferenceLinear(expected_state, expected_input, Rates[rate_index], expected_output,
                            expected_output_index);

            REQUIRE(actual_output == expected_output);
            REQUIRE(actual_output_index == expected_output_index);
            REQUIRE(actual_input == expected_input);
            REQUIRE(actual_state.xn1 == expected_state.xn1);
            REQUIRE(actual_state.xn2 == expected_state.xn2);
            REQUIRE(actual_state.fposition == expected_state.fposition);
        }
    }
}

} // namespace
