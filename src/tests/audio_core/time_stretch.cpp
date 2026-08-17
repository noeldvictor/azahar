// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "TDStretch.h"

namespace {

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

TEST_CASE("SoundTouch stereo overlap matches scalar integer division",
          "[audio_core][soundtouch]") {
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
