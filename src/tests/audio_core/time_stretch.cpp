// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "FIRFilter.h"
#include "TDStretch.h"

namespace {

constexpr std::size_t FirLength = 64;
constexpr unsigned int FirResultShift = 14;

std::array<short, FirLength> DesignAaCoefficients(double cutoff) {
    constexpr double Pi = 3.14159265358979323846;
    std::array<double, FirLength> work{};
    double sum = 0.0;

    for (std::size_t i = 0; i < FirLength; ++i) {
        const double offset = static_cast<double>(i) - static_cast<double>(FirLength / 2);
        const double angle = offset * 2.0 * Pi * cutoff;
        const double sinc = angle != 0.0 ? std::sin(angle) / angle : 1.0;
        const double window = 0.54 + 0.46 * std::cos(2.0 * Pi * offset / FirLength);
        work[i] = window * sinc;
        sum += work[i];
    }

    std::array<short, FirLength> coefficients{};
    const double scale = 16384.0 / sum;
    for (std::size_t i = 0; i < FirLength; ++i) {
        const double scaled = work[i] * scale;
        coefficients[i] = static_cast<short>(scaled + (scaled >= 0.0 ? 0.5 : -0.5));
    }
    return coefficients;
}

short ShiftAndClamp(std::int64_t sum) {
    constexpr std::int64_t Divisor = std::int64_t{1} << FirResultShift;
    const std::int64_t shifted = sum >= 0 ? sum / Divisor : -((-sum + Divisor - 1) / Divisor);
    return static_cast<short>(std::clamp<std::int64_t>(shifted, -32768, 32767));
}

class TDStretchHarness final : public soundtouch::TDStretch {
public:
    int Prepare(int sample_rate, int overlap_ms) {
        setChannels(2);
        setParameters(sample_rate, 40, 15, overlap_ms);
        return overlapLength;
    }

    void SetMidBuffer(const std::vector<short>& samples) {
        std::copy(samples.begin(), samples.end(), pMidBuffer);
    }

    void Overlap(std::vector<short>& output, const std::vector<short>& input) const {
        overlapStereo(output.data(), input.data());
    }
};

} // namespace

static_assert(sizeof(soundtouch::LONG_SAMPLETYPE) == sizeof(std::int32_t));

TEST_CASE("SoundTouch stereo FIR matches a 32-bit scalar reference", "[audio_core][soundtouch]") {
    constexpr std::array<short, 12> values{
        -32768, -32767, -24577, -16385, -1, 0, 1, 8191, 16384, 24576, 32766, 32767,
    };
    constexpr std::array<double, 3> cutoffs{0.2, 0.391755, 0.5};
    constexpr unsigned int input_frames = FirLength + 19;
    constexpr unsigned int output_frames = input_frames - FirLength;
    constexpr short canary = 0x5a5a;

    std::vector<short> input(2 * input_frames);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = values[(i * 5 + i / 7 + 3) % values.size()];
    }

    for (const double cutoff : cutoffs) {
        const auto coefficients = DesignAaCoefficients(cutoff);
        std::int64_t absolute_coefficient_sum = 0;
        for (const short coefficient : coefficients) {
            absolute_coefficient_sum += std::abs(static_cast<int>(coefficient));
        }
        REQUIRE(absolute_coefficient_sum * 32768 <= std::numeric_limits<std::int32_t>::max());

        soundtouch::FIRFilter filter;
        filter.setCoefficients(coefficients.data(), coefficients.size(), FirResultShift);

        std::vector<short> actual(2 * output_frames + 4, canary);
        std::vector<short> expected(2 * output_frames);
        for (unsigned int frame = 0; frame < output_frames; ++frame) {
            for (unsigned int channel = 0; channel < 2; ++channel) {
                std::int64_t sum = 0;
                for (std::size_t tap = 0; tap < FirLength; ++tap) {
                    sum += static_cast<std::int32_t>(input[2 * (frame + tap) + channel]) *
                           coefficients[tap];
                }
                REQUIRE(sum >= std::numeric_limits<std::int32_t>::min());
                REQUIRE(sum <= std::numeric_limits<std::int32_t>::max());
                expected[2 * frame + channel] = ShiftAndClamp(sum);
            }
        }

        CAPTURE(cutoff, absolute_coefficient_sum);
        REQUIRE(filter.evaluate(actual.data() + 2, input.data(), input_frames, 2) == output_frames);
        REQUIRE(actual.front() == canary);
        REQUIRE(actual[1] == canary);
        REQUIRE(actual[actual.size() - 2] == canary);
        REQUIRE(actual.back() == canary);
        REQUIRE(std::equal(expected.begin(), expected.end(), actual.begin() + 2));
    }
}

TEST_CASE("SoundTouch stereo overlap matches scalar integer division", "[audio_core][soundtouch]") {
    constexpr std::array<short, 12> values{
        -32768, -32767, -24577, -16385, -1, 0, 1, 8191, 16384, 24576, 32766, 32767,
    };
    constexpr std::array<std::array<int, 2>, 3> configurations{{
        {8000, 2},
        {44100, 8},
        {48000, 30},
    }};

    for (const auto [sample_rate, overlap_ms] : configurations) {
        TDStretchHarness stretch;
        const int overlap_length = stretch.Prepare(sample_rate, overlap_ms);
        CAPTURE(sample_rate, overlap_ms, overlap_length);

        std::vector<short> input(static_cast<std::size_t>(2 * overlap_length));
        std::vector<short> mid(input.size());
        std::vector<short> expected(input.size());
        std::vector<short> actual(input.size());

        for (std::size_t i = 0; i < input.size(); ++i) {
            input[i] = values[(i * 5 + 1) % values.size()];
            mid[i] = values[(i * 7 + 4) % values.size()];
        }

        for (int frame = 0; frame < overlap_length; ++frame) {
            for (int channel = 0; channel < 2; ++channel) {
                const std::size_t index = static_cast<std::size_t>(2 * frame + channel);
                const std::int32_t numerator =
                    static_cast<std::int32_t>(input[index]) * frame +
                    static_cast<std::int32_t>(mid[index]) * (overlap_length - frame);
                expected[index] = static_cast<short>(numerator / overlap_length);
            }
        }

        stretch.SetMidBuffer(mid);
        stretch.Overlap(actual, input);
        REQUIRE(actual == expected);
    }
}
