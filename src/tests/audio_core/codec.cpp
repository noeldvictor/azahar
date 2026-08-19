// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <bit>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/codec.h"

namespace {

AudioCore::StereoBuffer16 ReferenceDecodeADPCM(const u8* data, std::size_t sample_count,
                                               const std::array<s16, 16>& coefficients,
                                               AudioCore::Codec::ADPCMState& state) {
    constexpr std::size_t FrameLength = 8;
    constexpr std::size_t SamplesPerFrame = 14;
    constexpr std::array<int, 16> SignedNibbles{
        0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1,
    };

    AudioCore::StereoBuffer16 output(sample_count + sample_count % 2);
    int yn1 = state.yn1;
    int yn2 = state.yn2;
    const std::size_t frame_count = (sample_count + SamplesPerFrame - 1) / SamplesPerFrame;

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const u8 header = data[frame * FrameLength];
        const int scale = 1 << (header & 0xf);
        const std::size_t coefficient_index = (header >> 4) & 0x7;
        const int coefficient_1 = coefficients[coefficient_index * 2];
        const int coefficient_2 = coefficients[coefficient_index * 2 + 1];

        const auto decode = [&](int nibble) {
            const int fixed_sample = SignedNibbles[nibble] * scale;
            int value =
                ((fixed_sample << 11) + 0x400 + coefficient_1 * yn1 + coefficient_2 * yn2) >> 11;
            value = std::clamp(value, -32768, 32767);
            yn2 = yn1;
            yn1 = value;
            return static_cast<s16>(value);
        };

        std::size_t output_index = frame * SamplesPerFrame;
        std::size_t data_index = frame * FrameLength + 1;
        for (std::size_t sample = 0; sample < SamplesPerFrame && output_index < sample_count;
             sample += 2) {
            output[output_index++].fill(decode(data[data_index] >> 4));
            output[output_index++].fill(decode(data[data_index] & 0xf));
            ++data_index;
        }
    }

    state.yn1 = static_cast<s16>(yn1);
    state.yn2 = static_cast<s16>(yn2);
    return output;
}

std::array<u8, 72> MakeADPCMData(std::size_t phase) {
    std::array<u8, 72> data{};
    for (std::size_t frame = 0; frame < data.size() / 8; ++frame) {
        data[frame * 8] = static_cast<u8>(((frame + phase) % 8) << 4 | ((frame * 5 + phase) % 16));
        for (std::size_t byte = 1; byte < 8; ++byte) {
            const u8 high = static_cast<u8>((frame * 7 + byte * 3 + phase) % 16);
            const u8 low = static_cast<u8>((frame * 11 + byte * 5 + phase + 1) % 16);
            data[frame * 8 + byte] = static_cast<u8>((high << 4) | low);
        }
    }
    return data;
}

TEST_CASE("GC-ADPCM bitfield nibbles match the table reference", "[audio_core][codec]") {
    constexpr std::array<s16, 16> Coefficients{
        0, 0, 2048, 0, 0, 2048, 1024, 1024, -1024, 512, 512, -512, 1536, -768, -1536, 768,
    };
    constexpr std::array<std::size_t, 12> SampleCounts{
        0, 1, 2, 3, 13, 14, 15, 27, 28, 29, 111, 126,
    };
    constexpr std::array<AudioCore::Codec::ADPCMState, 4> InitialStates{
        AudioCore::Codec::ADPCMState{0, 0},
        AudioCore::Codec::ADPCMState{32767, -32768},
        AudioCore::Codec::ADPCMState{-30000, 30000},
        AudioCore::Codec::ADPCMState{1234, -5678},
    };

    for (std::size_t phase = 0; phase < 16; ++phase) {
        const auto data = MakeADPCMData(phase);
        for (const std::size_t sample_count : SampleCounts) {
            for (const auto initial_state : InitialStates) {
                CAPTURE(phase, sample_count, initial_state.yn1, initial_state.yn2);
                auto actual_state = initial_state;
                auto expected_state = initial_state;
                const auto actual = AudioCore::Codec::DecodeADPCM(data.data(), sample_count,
                                                                  Coefficients, actual_state);
                const auto expected =
                    ReferenceDecodeADPCM(data.data(), sample_count, Coefficients, expected_state);

                REQUIRE(actual == expected);
                REQUIRE(actual_state.yn1 == expected_state.yn1);
                REQUIRE(actual_state.yn2 == expected_state.yn2);
            }
        }
    }
}

TEST_CASE("PCM decoding preserves samples across deque blocks", "[audio_core][codec]") {
    constexpr std::array<std::size_t, 8> SampleCounts{0, 1, 7, 159, 1023, 1024, 1025, 2049};

    for (unsigned channels = 1; channels <= 2; ++channels) {
        for (const std::size_t sample_count : SampleCounts) {
            CAPTURE(channels, sample_count);

            std::vector<u8> pcm8(sample_count * channels);
            std::vector<u8> pcm16(sample_count * channels * sizeof(s16));
            for (std::size_t sample = 0; sample < sample_count; ++sample) {
                for (unsigned channel = 0; channel < channels; ++channel) {
                    const std::size_t input_index = sample * channels + channel;
                    pcm8[input_index] = static_cast<u8>(sample * 37 + channel * 113 + sample_count);

                    const u16 pcm16_bits =
                        static_cast<u16>(sample * 40503 + channel * 32771 + sample_count * 17);
                    pcm16[input_index * sizeof(s16)] = static_cast<u8>(pcm16_bits);
                    pcm16[input_index * sizeof(s16) + 1] = static_cast<u8>(pcm16_bits >> 8);
                }
            }

            const auto decoded8 = AudioCore::Codec::DecodePCM8(channels, pcm8.data(), sample_count);
            const auto decoded16 =
                AudioCore::Codec::DecodePCM16(channels, pcm16.data(), sample_count);

            REQUIRE(decoded8.size() == sample_count);
            REQUIRE(decoded16.size() == sample_count);
            for (std::size_t sample = 0; sample < sample_count; ++sample) {
                for (unsigned output_channel = 0; output_channel < 2; ++output_channel) {
                    const unsigned input_channel = channels == 1 ? 0 : output_channel;
                    const std::size_t input_index = sample * channels + input_channel;
                    const s16 expected8 =
                        static_cast<s16>(static_cast<u16>(pcm8[input_index]) << 8);
                    const u16 expected16_bits =
                        static_cast<u16>(pcm16[input_index * sizeof(s16)]) |
                        static_cast<u16>(pcm16[input_index * sizeof(s16) + 1] << 8);

                    REQUIRE(decoded8[sample][output_channel] == expected8);
                    REQUIRE(decoded16[sample][output_channel] ==
                            std::bit_cast<s16>(expected16_bits));
                }
            }

            for (const std::size_t first_sample :
                 std::array{std::size_t{0}, sample_count / 2, sample_count}) {
                CAPTURE(first_sample);
                const auto suffix = AudioCore::Codec::DecodePCM16FromSample(
                    channels, pcm16.data(), sample_count, first_sample);
                REQUIRE(suffix.size() == sample_count - first_sample);
                REQUIRE(std::equal(suffix.begin(), suffix.end(), decoded16.begin() + first_sample));
            }
        }
    }
}

} // namespace
