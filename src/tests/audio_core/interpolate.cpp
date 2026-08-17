// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/interpolate.h"

namespace {

constexpr u64 ScaleFactor = 1 << 24;
constexpr u64 ScaleMask = ScaleFactor - 1;

template <typename Function>
void ReferenceStepOverSamples(AudioCore::AudioInterp::State& state,
                              AudioCore::AudioInterp::StereoBuffer16& input, float rate,
                              AudioCore::StereoFrame16& output, std::size_t& output_index,
                              Function fn) {
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
        output[output_index++] = fn(fraction, x0, x1);
        position += step_size;
    }

    state.xn2 = input[input_index];
    state.xn1 = input[input_index + 1];
    state.fposition = position - input_index * ScaleFactor;
    input.erase(input.begin(), std::next(input.begin(), input_index + 2));
}

void ReferenceNone(AudioCore::AudioInterp::State& state,
                   AudioCore::AudioInterp::StereoBuffer16& input, float rate,
                   AudioCore::StereoFrame16& output, std::size_t& output_index) {
    ReferenceStepOverSamples(state, input, rate, output, output_index,
                             [](u64 fraction, const auto& x0, const auto& x1) { return x0; });
}

void ReferenceLinear(AudioCore::AudioInterp::State& state,
                     AudioCore::AudioInterp::StereoBuffer16& input, float rate,
                     AudioCore::StereoFrame16& output, std::size_t& output_index) {
    ReferenceStepOverSamples(
        state, input, rate, output, output_index, [](u64 fraction, const auto& x0, const auto& x1) {
            std::array<s16, 2> sample{};
            for (std::size_t channel = 0; channel < sample.size(); ++channel) {
                const s64 delta = std::clamp<s64>(x1[channel] - x0[channel], -32768, 32767);
                sample[channel] = static_cast<s16>(x0[channel] + fraction * delta / ScaleFactor);
            }
            return sample;
        });
}

using InterpolateFunction = void (*)(AudioCore::AudioInterp::State&,
                                     AudioCore::AudioInterp::StereoBuffer16&, float,
                                     AudioCore::StereoFrame16&, std::size_t&);

struct InterpolationMode {
    const char* name;
    InterpolateFunction actual;
    InterpolateFunction reference;
};

constexpr std::array InterpolationModes{
    InterpolationMode{"None", AudioCore::AudioInterp::None, ReferenceNone},
    InterpolationMode{"Linear", AudioCore::AudioInterp::Linear, ReferenceLinear},
};

AudioCore::AudioInterp::StereoBuffer16 MakeInput(std::size_t phase, std::size_t count = 512) {
    constexpr std::array<s16, 14> Values{
        -32768, 32767, -32767, 32766, -30000, 30000, -16384, 16384, -257, 319, -1, 0, 1, 8192,
    };
    AudioCore::AudioInterp::StereoBuffer16 input;
    for (std::size_t sample = 0; sample < count; ++sample) {
        input.push_back({Values[(sample * 5 + phase) % Values.size()],
                         Values[(sample * 9 + phase + 3) % Values.size()]});
    }
    return input;
}

void RequireMatchingResult(const AudioCore::AudioInterp::State& actual_state,
                           const AudioCore::AudioInterp::State& expected_state,
                           const AudioCore::AudioInterp::StereoBuffer16& actual_input,
                           const AudioCore::AudioInterp::StereoBuffer16& expected_input,
                           const AudioCore::StereoFrame16& actual_output,
                           const AudioCore::StereoFrame16& expected_output,
                           std::size_t actual_output_index, std::size_t expected_output_index) {
    REQUIRE(actual_output == expected_output);
    REQUIRE(actual_output_index == expected_output_index);
    REQUIRE(actual_input == expected_input);
    REQUIRE(actual_state.xn1 == expected_state.xn1);
    REQUIRE(actual_state.xn2 == expected_state.xn2);
    REQUIRE(actual_state.fposition == expected_state.fposition);
}

TEST_CASE("Interpolation modes match the scalar DSP reference", "[audio_core][interpolate]") {
    constexpr std::array<float, 6> Rates{0.25f, 0.5f, 0.9999f, 1.0f, 1.25f, 2.75f};
    constexpr std::array<u64, 5> InitialFractions{0, 1, ScaleFactor / 2, ScaleFactor - 2,
                                                  ScaleFactor - 1};

    for (const auto& mode : InterpolationModes) {
        for (std::size_t rate_index = 0; rate_index < Rates.size(); ++rate_index) {
            for (std::size_t fraction_index = 0; fraction_index < InitialFractions.size();
                 ++fraction_index) {
                CAPTURE(mode.name, rate_index, fraction_index);
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

                mode.actual(actual_state, actual_input, Rates[rate_index], actual_output,
                            actual_output_index);
                mode.reference(expected_state, expected_input, Rates[rate_index], expected_output,
                               expected_output_index);

                RequireMatchingResult(actual_state, expected_state, actual_input, expected_input,
                                      actual_output, expected_output, actual_output_index,
                                      expected_output_index);
            }
        }
    }
}

TEST_CASE("Interpolation preserves tiny and already-full buffers", "[audio_core][interpolate]") {
    constexpr std::array<float, 3> Rates{0.25f, 1.0f, 2.75f};
    constexpr std::array<std::size_t, 3> OutputIndices{0, 159, 160};

    for (const auto& mode : InterpolationModes) {
        for (std::size_t input_size = 0; input_size <= 3; ++input_size) {
            for (const float rate : Rates) {
                for (const std::size_t initial_output_index : OutputIndices) {
                    CAPTURE(mode.name, input_size, rate, initial_output_index);
                    auto actual_input = MakeInput(input_size + initial_output_index, input_size);
                    auto expected_input = actual_input;
                    AudioCore::AudioInterp::State actual_state{
                        .xn1 = {12345, -23456},
                        .xn2 = {-30000, 30000},
                        .fposition = ScaleFactor - 1,
                    };
                    auto expected_state = actual_state;
                    AudioCore::StereoFrame16 actual_output{};
                    actual_output.fill({-1111, 2222});
                    auto expected_output = actual_output;
                    std::size_t actual_output_index = initial_output_index;
                    std::size_t expected_output_index = initial_output_index;

                    mode.actual(actual_state, actual_input, rate, actual_output,
                                actual_output_index);
                    mode.reference(expected_state, expected_input, rate, expected_output,
                                   expected_output_index);

                    RequireMatchingResult(actual_state, expected_state, actual_input,
                                          expected_input, actual_output, expected_output,
                                          actual_output_index, expected_output_index);
                }
            }
        }
    }
}

TEST_CASE("Interpolation state remains exact across consecutive calls",
          "[audio_core][interpolate]") {
    constexpr std::array<float, 4> Rates{0.25f, 0.9999f, 1.0f, 2.75f};

    for (const auto& mode : InterpolationModes) {
        for (std::size_t rate_index = 0; rate_index < Rates.size(); ++rate_index) {
            CAPTURE(mode.name, rate_index);
            auto actual_input = MakeInput(rate_index + 7);
            auto expected_input = actual_input;
            AudioCore::AudioInterp::State actual_state{
                .xn1 = {32767, -32768},
                .xn2 = {-32768, 32767},
                .fposition = ScaleFactor / 2,
            };
            auto expected_state = actual_state;

            for (std::size_t call = 0; call < 4; ++call) {
                CAPTURE(call);
                AudioCore::StereoFrame16 actual_output{};
                actual_output.fill({-5432, 6789});
                auto expected_output = actual_output;
                std::size_t actual_output_index = call % 11;
                std::size_t expected_output_index = actual_output_index;

                mode.actual(actual_state, actual_input, Rates[rate_index], actual_output,
                            actual_output_index);
                mode.reference(expected_state, expected_input, Rates[rate_index], expected_output,
                               expected_output_index);

                RequireMatchingResult(actual_state, expected_state, actual_input, expected_input,
                                      actual_output, expected_output, actual_output_index,
                                      expected_output_index);
            }
        }
    }
}

} // namespace
