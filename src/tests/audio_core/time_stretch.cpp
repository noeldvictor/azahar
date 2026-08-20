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
#include "SoundTouch.h"
#include "TDStretch.h"
#include "cpu_detect.h"

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
    static constexpr std::size_t MaxNormBytes() {
        return sizeof(maxnorm);
    }

    int Prepare(int sample_rate, int overlap_ms) {
        setChannels(2);
        setParameters(sample_rate, 40, 15, overlap_ms);
        return overlapLength;
    }

    void PrepareTempoOnly(int sample_rate, double tempo) {
        setChannels(2);
        setParameters(sample_rate);
        setTempo(tempo);
    }

    int DividerBits() const {
        return overlapDividerBitsNorm;
    }

    int SeekLength() const {
        return seekLength;
    }

    std::uint32_t MaxNorm() const {
        return maxnorm;
    }

    double CrossCorr(const short* mixing, const short* compare, double& norm) {
        return calcCrossCorr(mixing, compare, norm);
    }

    double CrossCorrAccumulate(const short* mixing, const short* compare, double& norm) {
        return calcCrossCorrAccumulate(mixing, compare, norm);
    }

    int SeekFull(const short* mixing) {
        return seekBestOverlapPositionFull(mixing);
    }

    std::uint32_t InputSamples() const {
        return inputBuffer.numSamples();
    }

    void SetMidBuffer(const std::vector<short>& samples) {
        std::copy(samples.begin(), samples.end(), pMidBuffer);
    }

    void Overlap(std::vector<short>& output, const std::vector<short>& input) const {
        overlapStereo(output.data(), input.data());
    }
};

struct CorrelationReference {
    std::int64_t correlation;
    std::uint64_t norm;
};

std::int64_t ArithmeticShiftRight(std::int64_t value, int bits) {
    const std::int64_t divisor = std::int64_t{1} << bits;
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

CorrelationReference CalculateCorrelationReference(const short* mixing, const short* compare,
                                                   int sample_count, int divider_bits) {
    CorrelationReference result{};
    for (int i = 0; i < sample_count; i += 2) {
        const std::int64_t corr_pair =
            static_cast<std::int32_t>(mixing[i]) * static_cast<std::int32_t>(compare[i]) +
            static_cast<std::int32_t>(mixing[i + 1]) *
                static_cast<std::int32_t>(compare[i + 1]);
        const std::int64_t norm_pair =
            static_cast<std::int32_t>(mixing[i]) * static_cast<std::int32_t>(mixing[i]) +
            static_cast<std::int32_t>(mixing[i + 1]) *
                static_cast<std::int32_t>(mixing[i + 1]);
        result.correlation += ArithmeticShiftRight(corr_pair, divider_bits);
        result.norm += static_cast<std::uint64_t>(norm_pair) >> divider_bits;
    }
    return result;
}

std::int64_t CalculateNormalizerDelta(const short* mixing, int sample_count, int divider_bits) {
    std::int64_t delta = 0;
    for (int i = 1; i <= 2; ++i) {
        const std::int64_t sample = mixing[-i];
        delta -= (sample * sample) >> divider_bits;
    }
    for (int i = sample_count - 2; i < sample_count; ++i) {
        const std::int64_t sample = mixing[i];
        delta += (sample * sample) >> divider_bits;
    }
    return delta;
}

template <typename Processor>
void DrainSamples(Processor& processor, std::vector<short>& destination) {
    std::array<short, 2 * 257> buffer{};
    while (processor.numSamples() != 0) {
        const auto received = processor.receiveSamples(buffer.data(), 257);
        destination.insert(destination.end(), buffer.begin(), buffer.begin() + 2 * received);
    }
}

} // namespace

static_assert(sizeof(soundtouch::LONG_SAMPLETYPE) == sizeof(std::int32_t));
static_assert(TDStretchHarness::MaxNormBytes() == sizeof(std::uint32_t));

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

TEST_CASE("SoundTouch WSOLA correlation matches bounded 32-bit scalar arithmetic",
          "[audio_core][soundtouch]") {
    constexpr std::array<std::array<int, 2>, 3> configurations{{
        {8000, 2},
        {44100, 8},
        {48000, 30},
    }};
    constexpr int offsets_to_check = 9;
    constexpr int prefix_samples = 2;

    for (const auto [sample_rate, overlap_ms] : configurations) {
        TDStretchHarness stretch;
        const int overlap_length = stretch.Prepare(sample_rate, overlap_ms);
        const int sample_count = 2 * overlap_length;
        const int divider_bits = stretch.DividerBits();
        CAPTURE(sample_rate, overlap_ms, overlap_length, divider_bits);

        std::vector<short> mixing(static_cast<std::size_t>(prefix_samples + sample_count +
                                                           2 * offsets_to_check));
        std::vector<short> compare(static_cast<std::size_t>(sample_count));
        for (std::size_t i = 0; i < mixing.size(); ++i) {
            mixing[i] = static_cast<short>((i * 4051 + i * i * 29 + 7919) % 32001 - 16000);
        }
        for (std::size_t i = 0; i < compare.size(); ++i) {
            compare[i] = static_cast<short>((i * 3253 + i * i * 17 + 1237) % 32001 - 16000);
        }

        const short* const first = mixing.data() + prefix_samples;
        const auto initial =
            CalculateCorrelationReference(first, compare.data(), sample_count, divider_bits);
        REQUIRE(initial.correlation >= std::numeric_limits<std::int32_t>::min());
        REQUIRE(initial.correlation <= std::numeric_limits<std::int32_t>::max());
        REQUIRE(initial.norm <= std::numeric_limits<std::uint32_t>::max());

        double running_norm = 0.0;
        const double initial_actual = stretch.CrossCorr(first, compare.data(), running_norm);
        const double initial_expected =
            static_cast<double>(initial.correlation) / std::sqrt(static_cast<double>(initial.norm));
        REQUIRE(running_norm == static_cast<double>(initial.norm));
        REQUIRE(initial_actual == initial_expected);
        REQUIRE(stretch.MaxNorm() == initial.norm);

        std::int64_t expected_running_norm = static_cast<std::int64_t>(initial.norm);
        for (int offset = 1; offset <= offsets_to_check; ++offset) {
            const short* const position = first + 2 * offset;
            const auto expected =
                CalculateCorrelationReference(position, compare.data(), sample_count, divider_bits);
            REQUIRE(expected.correlation >= std::numeric_limits<std::int32_t>::min());
            REQUIRE(expected.correlation <= std::numeric_limits<std::int32_t>::max());
            const std::int64_t norm_delta =
                CalculateNormalizerDelta(position, sample_count, divider_bits);
            REQUIRE(norm_delta >= std::numeric_limits<std::int32_t>::min());
            REQUIRE(norm_delta <= std::numeric_limits<std::int32_t>::max());
            expected_running_norm += norm_delta;
            REQUIRE(expected_running_norm >= 0);
            REQUIRE(expected_running_norm <= std::numeric_limits<std::uint32_t>::max());

            const double actual =
                stretch.CrossCorrAccumulate(position, compare.data(), running_norm);
            const double expected_value = static_cast<double>(expected.correlation) /
                                          std::sqrt(static_cast<double>(expected_running_norm));
            CAPTURE(offset, expected.correlation, norm_delta, expected_running_norm);
            REQUIRE(running_norm == static_cast<double>(expected_running_norm));
            REQUIRE(actual == expected_value);
        }
    }
}

TEST_CASE("SoundTouch WSOLA full search matches an independent scalar reference",
          "[audio_core][soundtouch]") {
    constexpr std::array<std::array<int, 2>, 3> configurations{{
        {8000, 2},
        {44100, 8},
        {48000, 30},
    }};
    constexpr int prefix_samples = 2;

    for (const auto [sample_rate, overlap_ms] : configurations) {
        TDStretchHarness stretch;
        const int overlap_length = stretch.Prepare(sample_rate, overlap_ms);
        const int sample_count = 2 * overlap_length;
        const int seek_length = stretch.SeekLength();
        const int divider_bits = stretch.DividerBits();
        CAPTURE(sample_rate, overlap_ms, overlap_length, seek_length, divider_bits);

        std::vector<short> mixing(
            static_cast<std::size_t>(prefix_samples + sample_count + 2 * seek_length));
        std::vector<short> compare(static_cast<std::size_t>(sample_count));
        for (std::size_t i = 0; i < mixing.size(); ++i) {
            mixing[i] = static_cast<short>((i * 4051 + i * i * 29 + 7919) % 32001 - 16000);
        }
        for (std::size_t i = 0; i < compare.size(); ++i) {
            compare[i] = static_cast<short>((i * 3253 + i * i * 17 + 1237) % 32001 - 16000);
        }

        const short* const first = mixing.data() + prefix_samples;
        const auto initial =
            CalculateCorrelationReference(first, compare.data(), sample_count, divider_bits);
        std::int64_t running_norm = static_cast<std::int64_t>(initial.norm);
        double best_correlation =
            (static_cast<double>(initial.correlation) /
                 std::sqrt(static_cast<double>(std::max<std::int64_t>(running_norm, 1))) +
             0.1) *
            0.75;
        int expected_offset = 0;

        for (int offset = 1; offset < seek_length; ++offset) {
            const short* const position = first + 2 * offset;
            const auto expected =
                CalculateCorrelationReference(position, compare.data(), sample_count, divider_bits);
            running_norm += CalculateNormalizerDelta(position, sample_count, divider_bits);
            REQUIRE(running_norm >= 0);
            REQUIRE(running_norm <= std::numeric_limits<std::uint32_t>::max());

            double correlation =
                static_cast<double>(expected.correlation) /
                std::sqrt(static_cast<double>(std::max<std::int64_t>(running_norm, 1)));
            const double distance =
                static_cast<double>(2 * offset - seek_length) / static_cast<double>(seek_length);
            correlation = (correlation + 0.1) * (1.0 - 0.25 * distance * distance);
            if (correlation > best_correlation) {
                best_correlation = correlation;
                expected_offset = offset;
            }
        }

        stretch.SetMidBuffer(compare);
        REQUIRE(stretch.SeekFull(first) == expected_offset);
    }
}

TEST_CASE("SoundTouch pure-tempo bypass matches the TDStretch stage byte-for-byte",
          "[audio_core][soundtouch]") {
    constexpr int sample_rate = 48000;
    constexpr std::array<double, 3> tempos{0.72, 0.93, 1.08};
    constexpr std::array<int, 7> chunk_sizes{1, 17, 159, 160, 257, 511, 1024};
    constexpr int input_frames = 24000;

    std::vector<short> input(2 * input_frames);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const int word = static_cast<int>((i * 4051 + i * i * 29 + 7919) % 65536);
        input[i] = static_cast<short>(word - 32768);
    }

    for (const double tempo : tempos) {
        // Make the host test use the same generic TDStretch implementation as Android AArch64.
        disableExtensions(~uint{0});
        soundtouch::SoundTouch actual;
        disableExtensions(0);

        actual.setChannels(2);
        actual.setSampleRate(sample_rate);
        actual.setPitch(1.0);
        actual.setRate(1.0);
        actual.setTempo(tempo);
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 0);
        const int transposer_latency = actual.getSetting(SETTING_INITIAL_LATENCY);
        REQUIRE(actual.setSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY, 1));
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 1);
        REQUIRE(actual.getSetting(SETTING_INITIAL_LATENCY) < transposer_latency);

        TDStretchHarness reference;
        reference.PrepareTempoOnly(sample_rate, tempo);

        std::vector<short> actual_output;
        std::vector<short> reference_output;
        int consumed = 0;
        int chunk = 0;
        while (consumed < input_frames) {
            const int frames =
                std::min(chunk_sizes[chunk % chunk_sizes.size()], input_frames - consumed);
            actual.putSamples(input.data() + 2 * consumed, frames);
            reference.putSamples(input.data() + 2 * consumed, frames);
            DrainSamples(actual, actual_output);
            DrainSamples(reference, reference_output);
            REQUIRE(actual_output == reference_output);
            consumed += frames;
            ++chunk;
        }

        CAPTURE(tempo, actual_output.size(), actual.numUnprocessedSamples());
        REQUIRE(!actual_output.empty());
        REQUIRE(actual.numUnprocessedSamples() == reference.InputSamples());
        REQUIRE_FALSE(actual.setSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY, 0));
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 1);

        const std::size_t output_before_flush = actual_output.size();
        actual.flush();
        DrainSamples(actual, actual_output);
        REQUIRE(actual_output.size() >= output_before_flush);
        REQUIRE(actual.numUnprocessedSamples() == 0);
        actual.clear();
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 1);
        REQUIRE(actual.setSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY, 0));
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 0);
        REQUIRE(actual.setSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY, 1));

        actual.setRate(1.01);
        REQUIRE(actual.getSetting(SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY) == 0);
    }
}
