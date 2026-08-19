#include <array>
#include <cstdio>
#include <limits>
#include <utility>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/hle/shared_memory.h"
#include "audio_core/hle/source.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/memory.h"
#include "tests/audio_core/merryhime_3ds_audio/merry_audio/merry_audio.h"

namespace AudioCore::HLE {

struct SourceMixTestAccess {
    static void Configure(Source& source, const StereoFrame16& input,
                          const std::array<float, 4>& ramp_start, const std::array<float, 4>& gains,
                          std::size_t mix_id, bool ramp_active, bool enabled = true) {
        source.current_frame = input;
        source.state.enabled = enabled;
        source.state.gain[mix_id] = gains;
        source.state.gain_ramp_start[mix_id] = ramp_start;
        source.state.gain_ramp_active[mix_id] = ramp_active;
    }

    static const std::array<float, 4>& RampStart(const Source& source, std::size_t mix_id) {
        return source.state.gain_ramp_start[mix_id];
    }

    static bool RampActive(const Source& source, std::size_t mix_id) {
        return source.state.gain_ramp_active[mix_id];
    }

    static void ConfigureGeneration(Source& source, const StereoFrame16& previous_frame,
                                    const AudioInterp::StereoBuffer16& input,
                                    const AudioInterp::State& interp_state) {
        source.current_frame = previous_frame;
        source.state.enabled = true;
        source.state.current_buffer = input;
        source.state.interpolation_mode = Source::InterpolationMode::None;
        source.state.rate_multiplier = 1.0f;
        source.state.interp_state = interp_state;
        source.state.current_sample_number = 0;
        source.state.filters.Reset();
    }

    static void GenerateFrame(Source& source) {
        source.GenerateFrame();
    }

    static const StereoFrame16& CurrentFrame(const Source& source) {
        return source.current_frame;
    }

    static bool Enabled(const Source& source) {
        return source.state.enabled;
    }

    static u32 CurrentSampleNumber(const Source& source) {
        return source.state.current_sample_number;
    }

    static void ConfigurePartialPCM16(Source& source, Memory::MemorySystem& memory,
                                      unsigned channels, PAddr physical_address,
                                      u32 current_sample_number) {
        source.SetMemory(memory);
        source.state.format = Source::Format::PCM16;
        source.state.mono_or_stereo =
            channels == 2 ? Source::MonoOrStereo::Stereo : Source::MonoOrStereo::Mono;
        source.state.current_buffer_physical_address = physical_address;
        source.state.current_sample_number = current_sample_number;
        source.state.current_buffer.assign(3, {-12345, 23456});
    }

    static void ParseConfig(Source& source, SourceConfiguration::Configuration& config) {
        const s16_le adpcm_coefficients[16]{};
        source.ParseConfig(config, adpcm_coefficients);
    }

    static const AudioInterp::StereoBuffer16& CurrentBuffer(const Source& source) {
        return source.state.current_buffer;
    }
};

} // namespace AudioCore::HLE

namespace {

void ReferenceSourceMix(AudioCore::PlanarQuadFrame32& dest, const AudioCore::StereoFrame16& input,
                        const std::array<float, 4>& ramp_start, const std::array<float, 4>& gains,
                        bool ramp_active) {
    constexpr float ramp_scale = 1.0f / static_cast<float>(AudioCore::samples_per_frame - 1);
    for (std::size_t channel = 0; channel < dest.size(); ++channel) {
        for (std::size_t sample = 0; sample < AudioCore::samples_per_frame; ++sample) {
            const float progress = static_cast<float>(sample) * ramp_scale;
            const float gain = ramp_active ? ramp_start[channel] +
                                                 (gains[channel] - ramp_start[channel]) * progress
                                           : gains[channel];
            dest[channel][sample] +=
                static_cast<s32>(gain * input[sample][channel & std::size_t{1}]);
        }
    }
}

} // Anonymous namespace

TEST_CASE("HLE partial PCM16 updates decode only the retained suffix", "[audio_core][hle]") {
    constexpr std::size_t SampleCount = 17;
    constexpr std::array<std::pair<u32, u32>, 5> PositionAndLength{{
        {0, 0},
        {0, 17},
        {5, 17},
        {17, 17},
        {13, 7},
    }};

    Core::System system;
    Memory::MemorySystem memory{system};
    u8* const fcram = memory.GetFCRAMPointer(0);

    for (unsigned channels = 1; channels <= 2; ++channels) {
        for (std::size_t sample = 0; sample < SampleCount; ++sample) {
            for (unsigned channel = 0; channel < channels; ++channel) {
                const u16 bits = static_cast<u16>(sample * 40503 + channel * 32771 + 0x1234);
                const std::size_t byte = (sample * channels + channel) * sizeof(s16);
                fcram[byte] = static_cast<u8>(bits);
                fcram[byte + 1] = static_cast<u8>(bits >> 8);
            }
        }

        for (const auto [position, length] : PositionAndLength) {
            CAPTURE(channels, position, length);
            AudioCore::HLE::Source source{0};
            AudioCore::HLE::SourceMixTestAccess::ConfigurePartialPCM16(
                source, memory, channels, Memory::FCRAM_PADDR, position);

            AudioCore::HLE::SourceConfiguration::Configuration config{};
            config.partial_embedded_buffer_dirty.Assign(1);
            config.length = length;
            AudioCore::HLE::SourceMixTestAccess::ParseConfig(source, config);

            const std::size_t first_sample = position <= length ? position : 0;
            const auto expected =
                AudioCore::Codec::DecodePCM16FromSample(channels, fcram, length, first_sample);
            REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentBuffer(source) == expected);
            REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentSampleNumber(source) ==
                    first_sample);
            REQUIRE(config.partial_embedded_buffer_dirty == 0);
        }
    }
}

TEST_CASE("HLE source generation overwrites full frames and silences missing samples",
          "[audio_core][hle]") {
    constexpr std::array<s16, 2> dirty_sample{-12345, 23456};
    constexpr AudioCore::AudioInterp::State interp_state{
        .xn1 = {1234, -2345},
        .xn2 = {-30000, 30000},
        .fposition = 0,
    };

    AudioCore::StereoFrame16 dirty_frame{};
    dirty_frame.fill(dirty_sample);

    const auto make_input = [](std::size_t count) {
        AudioCore::AudioInterp::StereoBuffer16 input;
        for (std::size_t sample = 0; sample < count; ++sample) {
            input.push_back({
                static_cast<s16>((static_cast<s32>(sample) * 7919 % 65536) - 32768),
                static_cast<s16>((static_cast<s32>(sample) * 3571 % 65536) - 32768),
            });
        }
        return input;
    };

    const auto expected_prefix = [&](const AudioCore::AudioInterp::StereoBuffer16& input,
                                     std::size_t produced) {
        AudioCore::StereoFrame16 expected{};
        if (produced != 0) {
            expected[0] = interp_state.xn2;
        }
        if (produced > 1) {
            expected[1] = interp_state.xn1;
        }
        for (std::size_t output = 2; output < produced; ++output) {
            expected[output] = input[output - 2];
        }
        return expected;
    };

    SECTION("complete frame replaces every dirty sample") {
        const auto input = make_input(AudioCore::samples_per_frame);
        const auto expected = expected_prefix(input, AudioCore::samples_per_frame);
        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::ConfigureGeneration(source, dirty_frame, input,
                                                                 interp_state);

        AudioCore::HLE::SourceMixTestAccess::GenerateFrame(source);

        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentFrame(source) == expected);
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::Enabled(source));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentSampleNumber(source) ==
                AudioCore::samples_per_frame);
    }

    SECTION("underrun replaces its prefix and silences its unwritten tail") {
        constexpr std::size_t input_count = 35;
        constexpr std::size_t produced = input_count;
        const auto input = make_input(input_count);
        const auto expected = expected_prefix(input, produced);
        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::ConfigureGeneration(source, dirty_frame, input,
                                                                 interp_state);

        AudioCore::HLE::SourceMixTestAccess::GenerateFrame(source);

        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentFrame(source) == expected);
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::Enabled(source));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentSampleNumber(source) == produced);
    }

    SECTION("empty source replaces the previous frame with silence") {
        const AudioCore::AudioInterp::StereoBuffer16 input;
        const AudioCore::StereoFrame16 expected{};
        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::ConfigureGeneration(source, dirty_frame, input,
                                                                 interp_state);

        AudioCore::HLE::SourceMixTestAccess::GenerateFrame(source);

        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentFrame(source) == expected);
        REQUIRE_FALSE(AudioCore::HLE::SourceMixTestAccess::Enabled(source));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::CurrentSampleNumber(source) == 0);
    }
}

TEST_CASE("HLE source mixing preserves exact steady and ramped output", "[audio_core][hle]") {
    constexpr std::size_t mix_id = 1;
    constexpr std::array<float, 4> ramp_start{-0.75f, 0.5f, 1.25f, -0.125f};
    constexpr std::array<float, 4> gains{0.0f, 0.25f, -0.5f, 1.75f};
    constexpr std::array<float, 4> zero_gains{-0.0f, 0.0f, -0.0f, 0.0f};
    constexpr std::array<float, 4> front_ramp_start{-0.25f, 0.5f, 0.0f, -0.0f};
    constexpr std::array<float, 4> front_gains{0.75f, -0.5f, -0.0f, 0.0f};

    AudioCore::StereoFrame16 input{};
    for (std::size_t sample = 0; sample < input.size(); ++sample) {
        input[sample][0] = static_cast<s16>((static_cast<s32>(sample) * 7919 % 65536) - 32768);
        input[sample][1] = static_cast<s16>((static_cast<s32>(sample) * 3571 % 65536) - 32768);
    }
    input[0] = {std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()};
    input[1] = {std::numeric_limits<s16>::max(), std::numeric_limits<s16>::min()};
    input[2] = {0, 1};
    input[3] = {-1, 0};

    struct GuardedMix {
        std::array<u32, 8> prefix;
        std::array<AudioCore::PlanarQuadFrame32, 3> output;
        std::array<u32, 8> suffix;
    };

    const auto run_case = [&](bool ramp_active, const std::array<float, 4>& case_ramp_start,
                              const std::array<float, 4>& case_gains,
                              bool first_contribution = false) {
        GuardedMix actual{};
        actual.prefix.fill(0x13579BDF);
        actual.suffix.fill(0x2468ACE0);
        for (std::size_t mix = 0; mix < actual.output.size(); ++mix) {
            for (std::size_t channel = 0; channel < actual.output[mix].size(); ++channel) {
                for (std::size_t sample = 0; sample < actual.output[mix][channel].size();
                     ++sample) {
                    actual.output[mix][channel][sample] =
                        static_cast<s32>((mix * 524287 + channel * 131071 + sample * 8191) %
                                         200001) -
                        100000;
                }
            }
        }
        const GuardedMix initial = actual;
        auto expected = actual.output;
        if (first_contribution) {
            expected[mix_id] = {};
        }
        ReferenceSourceMix(expected[mix_id], input, case_ramp_start, case_gains, ramp_active);

        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::Configure(source, input, case_ramp_start, case_gains,
                                                       mix_id, ramp_active);
        if (first_contribution) {
            REQUIRE(source.MixIntoFirst(actual.output) == (u32{1} << mix_id));
        } else {
            source.MixInto(actual.output);
        }

        REQUIRE(actual.output == expected);
        REQUIRE(actual.prefix == initial.prefix);
        REQUIRE(actual.suffix == initial.suffix);
        REQUIRE_FALSE(AudioCore::HLE::SourceMixTestAccess::RampActive(source, mix_id));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::RampStart(source, mix_id) ==
                (ramp_active ? case_gains : case_ramp_start));
    };

    SECTION("steady gains") {
        run_case(false, ramp_start, gains);
    }
    SECTION("ramped gains") {
        run_case(true, ramp_start, gains);
    }
    SECTION("steady front-stereo gains leave rear destinations untouched") {
        run_case(false, front_ramp_start, front_gains);
    }
    SECTION("ramped front-stereo gains leave rear destinations untouched") {
        run_case(true, front_ramp_start, front_gains);
    }
    SECTION("first steady full contribution defines the complete bus") {
        run_case(false, ramp_start, gains, true);
    }
    SECTION("first ramped full contribution defines the complete bus") {
        run_case(true, ramp_start, gains, true);
    }
    SECTION("first steady front contribution defines the bus and clears rear channels") {
        run_case(false, front_ramp_start, front_gains, true);
    }
    SECTION("first ramped front contribution defines the bus and clears rear channels") {
        run_case(true, front_ramp_start, front_gains, true);
    }
    SECTION("first source defines every audible bus in one pass") {
        std::array<AudioCore::PlanarQuadFrame32, 3> actual{};
        for (std::size_t mix = 0; mix < actual.size(); ++mix) {
            for (std::size_t channel = 0; channel < actual[mix].size(); ++channel) {
                actual[mix][channel].fill(static_cast<s32>(mix * 101 + channel * 17) - 23);
            }
        }
        auto expected = actual;
        expected[0] = {};
        expected[2] = {};
        ReferenceSourceMix(expected[0], input, ramp_start, gains, false);
        ReferenceSourceMix(expected[2], input, front_ramp_start, front_gains, true);

        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::Configure(source, input, ramp_start, gains, 0, false);
        AudioCore::HLE::SourceMixTestAccess::Configure(source, input, front_ramp_start, front_gains,
                                                       2, true);

        REQUIRE(source.MixIntoFirst(actual) == 0b101);
        REQUIRE(actual == expected);
        REQUIRE_FALSE(AudioCore::HLE::SourceMixTestAccess::RampActive(source, 2));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::RampStart(source, 2) == front_gains);
    }
    SECTION("steady signed-zero gains leave every destination untouched") {
        run_case(false, ramp_start, zero_gains);
    }
    SECTION("zero-to-zero ramp leaves every destination untouched") {
        run_case(true, zero_gains, zero_gains);
    }
    SECTION("silent first contribution leaves the bus pending and untouched") {
        std::array<AudioCore::PlanarQuadFrame32, 3> actual{};
        for (std::size_t mix = 0; mix < actual.size(); ++mix) {
            for (std::size_t channel = 0; channel < actual[mix].size(); ++channel) {
                actual[mix][channel].fill(static_cast<s32>(mix * 101 + channel * 17) - 23);
            }
        }
        const auto expected = actual;
        AudioCore::HLE::Source source{0};
        AudioCore::HLE::SourceMixTestAccess::Configure(source, input, zero_gains, zero_gains,
                                                       mix_id, true);
        REQUIRE(source.MixIntoFirst(actual) == 0);

        REQUIRE(actual == expected);
        REQUIRE_FALSE(AudioCore::HLE::SourceMixTestAccess::RampActive(source, mix_id));
        REQUIRE(AudioCore::HLE::SourceMixTestAccess::RampStart(source, mix_id) == zero_gains);
    }
    SECTION("nonzero-to-zero ramp is still mixed") {
        run_case(true, ramp_start, zero_gains);
    }
    SECTION("zero-to-nonzero ramp is still mixed") {
        run_case(true, zero_gains, gains);
    }

    SECTION("disabled source only advances ramp state") {
        constexpr std::array<std::array<float, 4>, 3> disabled_ramp_starts{{
            {-0.5f, 0.25f, 1.5f, -1.0f},
            {0.75f, -0.125f, 0.0f, 2.0f},
            {-2.0f, 1.0f, 0.5f, -0.25f},
        }};
        constexpr std::array<std::array<float, 4>, 3> disabled_gains{{
            {0.125f, -0.75f, 0.0f, 1.25f},
            {-1.5f, 0.5f, 0.25f, -0.0f},
            {2.0f, -0.25f, -0.5f, 0.75f},
        }};
        std::array<AudioCore::PlanarQuadFrame32, 3> actual{};
        for (std::size_t mix = 0; mix < actual.size(); ++mix) {
            for (std::size_t channel = 0; channel < actual[mix].size(); ++channel) {
                actual[mix][channel].fill(static_cast<s32>(mix * 101 + channel * 17) - 23);
            }
        }
        const auto expected = actual;
        AudioCore::HLE::Source source{0};
        for (std::size_t mix = 0; mix < actual.size(); ++mix) {
            AudioCore::HLE::SourceMixTestAccess::Configure(source, input, disabled_ramp_starts[mix],
                                                           disabled_gains[mix], mix, true, false);
        }
        REQUIRE(source.MixIntoFirst(actual) == 0);

        REQUIRE(actual == expected);
        for (std::size_t mix = 0; mix < actual.size(); ++mix) {
            REQUIRE_FALSE(AudioCore::HLE::SourceMixTestAccess::RampActive(source, mix));
            REQUIRE(AudioCore::HLE::SourceMixTestAccess::RampStart(source, mix) ==
                    disabled_gains[mix]);
        }
    }
}

TEST_CASE_METHOD(MerryAudio::MerryAudioFixture, "Verify SourceStatus::Status::last_buffer_id 1",
                 "[audio_core][hle]") {
    //  World's worst triangle wave generator.
    //  Generates PCM16.
    auto fillBuffer = [this](u32* audio_buffer, size_t size, unsigned freq) {
        for (size_t i = 0; i < size; i++) {
            u32 data = (i % freq) * 256;
            audio_buffer[i] = (data << 16) | (data & 0xFFFF);
        }

        DSP_FlushDataCache(audio_buffer, size);
    };

    constexpr size_t NUM_SAMPLES = 160 * 1;
    u32* audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer, NUM_SAMPLES, 160);
    u32* audio_buffer2 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer2, NUM_SAMPLES, 80);
    u32* audio_buffer3 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer3, NUM_SAMPLES, 40);

    MerryAudio::AudioState state;
    {
        std::vector<u8> dspfirm;
        SECTION("HLE") {
            // The test case assumes HLE AudioCore doesn't require a valid firmware
            InitDspCore(Settings::AudioEmulation::HLE);
            dspfirm = {0};
        }
        SECTION("LLE Sanity") {
            InitDspCore(Settings::AudioEmulation::LLE);
            dspfirm = loadDspFirmFromFile();
        }
        if (!dspfirm.size()) {
            SKIP("Couldn't load firmware\n");
            return;
        }
        auto ret = audioInit(dspfirm);
        if (!ret) {
            INFO("Couldn't init audio\n");
            goto end;
        }
        state = *ret;
    }

    state.waitForSync();
    initSharedMem(state);
    state.notifyDsp();

    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();

    {
        u16 buffer_id = 0;
        size_t next_queue_position = 0;

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address =
            osConvertVirtToPhys(audio_buffer3);
        state.write().source_configurations->config[0].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].mono_or_stereo.Assign(
            AudioCore::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo);
        state.write().source_configurations->config[0].format.Assign(
            AudioCore::HLE::SourceConfiguration::Configuration::Format::PCM16);
        state.write().source_configurations->config[0].fade_in.Assign(false);
        state.write().source_configurations->config[0].adpcm_dirty.Assign(false);
        state.write().source_configurations->config[0].is_looping.Assign(false);
        state.write().source_configurations->config[0].buffer_id = ++buffer_id;
        state.write().source_configurations->config[0].partial_reset_flag.Assign(true);
        state.write().source_configurations->config[0].play_position_dirty.Assign(true);
        state.write().source_configurations->config[0].embedded_buffer_dirty.Assign(true);

        state.write()
            .source_configurations->config[0]
            .buffers[next_queue_position]
            .physical_address = osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
        state.write().source_configurations->config[0].buffers[next_queue_position].length =
            NUM_SAMPLES;
        state.write().source_configurations->config[0].buffers[next_queue_position].adpcm_dirty =
            false;
        state.write().source_configurations->config[0].buffers[next_queue_position].is_looping =
            false;
        state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id =
            ++buffer_id;
        state.write().source_configurations->config[0].buffers_dirty |= 1 << next_queue_position;
        next_queue_position = (next_queue_position + 1) % 4;
        state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
        state.write().source_configurations->config[0].enable = true;
        state.write().source_configurations->config[0].enable_dirty.Assign(true);

        state.notifyDsp();

        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            if (!state.read().source_statuses->status[0].is_enabled) {
                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty.Assign(true);
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                    state.read().source_statuses->status[0].current_buffer_id == 0) {
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .physical_address =
                        osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .length = NUM_SAMPLES;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .adpcm_dirty = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .is_looping = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .buffer_id = ++buffer_id;
                    state.write().source_configurations->config[0].buffers_dirty |=
                        1 << next_queue_position;
                    next_queue_position = (next_queue_position + 1) % 4;
                    state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
                }
            }

            state.notifyDsp();
        }

        // current_buffer_id should be 0 if the queue is not empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

        // Let the queue finish playing
        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            state.notifyDsp();
        }

        // TODO: There seems to be some nuances with how the LLE firmware runs the buffer queue,
        // that differs from the HLE implementation
        // REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 5);

        // current_buffer_id should be equal to buffer_id once the queue is empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);
    }

end:
    audioExit(state);
}

TEST_CASE_METHOD(MerryAudio::MerryAudioFixture, "Verify SourceStatus::Status::last_buffer_id 2",
                 "[audio_core][hle]") {
    //  World's worst triangle wave generator.
    //  Generates PCM16.
    auto fillBuffer = [this](u32* audio_buffer, size_t size, unsigned freq) {
        for (size_t i = 0; i < size; i++) {
            u32 data = (i % freq) * 256;
            audio_buffer[i] = (data << 16) | (data & 0xFFFF);
        }

        DSP_FlushDataCache(audio_buffer, size);
    };

    constexpr size_t NUM_SAMPLES = 160 * 1;
    u32* audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer, NUM_SAMPLES, 160);
    u32* audio_buffer2 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer2, NUM_SAMPLES, 80);
    u32* audio_buffer3 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer3, NUM_SAMPLES, 40);

    MerryAudio::AudioState state;
    {
        std::vector<u8> dspfirm;
        SECTION("HLE") {
            // The test case assumes HLE AudioCore doesn't require a valid firmware
            InitDspCore(Settings::AudioEmulation::HLE);
            dspfirm = {0};
        }
        SECTION("LLE Sanity") {
            InitDspCore(Settings::AudioEmulation::LLE);
            dspfirm = loadDspFirmFromFile();
        }
        if (!dspfirm.size()) {
            SKIP("Couldn't load firmware\n");
            return;
        }
        auto ret = audioInit(dspfirm);
        if (!ret) {
            INFO("Couldn't init audio\n");
            goto end;
        }
        state = *ret;
    }

    state.waitForSync();
    initSharedMem(state);
    state.notifyDsp();

    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();

    {
        u16 buffer_id = 0;
        size_t next_queue_position = 0;

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address =
            osConvertVirtToPhys(audio_buffer3);
        state.write().source_configurations->config[0].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].mono_or_stereo.Assign(
            AudioCore::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo);
        state.write().source_configurations->config[0].format.Assign(
            AudioCore::HLE::SourceConfiguration::Configuration::Format::PCM16);
        state.write().source_configurations->config[0].fade_in.Assign(false);
        state.write().source_configurations->config[0].adpcm_dirty.Assign(false);
        state.write().source_configurations->config[0].is_looping.Assign(false);
        state.write().source_configurations->config[0].buffer_id = ++buffer_id;
        state.write().source_configurations->config[0].partial_reset_flag.Assign(true);
        state.write().source_configurations->config[0].play_position_dirty.Assign(true);
        state.write().source_configurations->config[0].embedded_buffer_dirty.Assign(true);

        state.write()
            .source_configurations->config[0]
            .buffers[next_queue_position]
            .physical_address = osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
        state.write().source_configurations->config[0].buffers[next_queue_position].length =
            NUM_SAMPLES;
        state.write().source_configurations->config[0].buffers[next_queue_position].adpcm_dirty =
            false;
        state.write().source_configurations->config[0].buffers[next_queue_position].is_looping =
            false;
        state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id =
            ++buffer_id;
        state.write().source_configurations->config[0].buffers_dirty |= 1 << next_queue_position;
        next_queue_position = (next_queue_position + 1) % 4;
        state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
        state.write().source_configurations->config[0].enable = true;
        state.write().source_configurations->config[0].enable_dirty.Assign(true);

        state.notifyDsp();

        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            if (!state.read().source_statuses->status[0].is_enabled) {
                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty.Assign(true);
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                    state.read().source_statuses->status[0].current_buffer_id == 0) {
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .physical_address =
                        osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .length = NUM_SAMPLES;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .adpcm_dirty = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .is_looping = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .buffer_id = ++buffer_id;
                    state.write().source_configurations->config[0].buffers_dirty |=
                        1 << next_queue_position;
                    next_queue_position = (next_queue_position + 1) % 4;
                    state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
                }
            }

            state.notifyDsp();
        }

        // current_buffer_id should be 0 if the queue is not empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

        // Let the queue finish playing
        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            state.notifyDsp();
        }

        // TODO: There seems to be some nuances with how the LLE firmware runs the buffer queue,
        // that differs from the HLE implementation
        // REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 5);

        // current_buffer_id should be equal to buffer_id once the queue is empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);

        // Restart Playing
        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            if (!state.read().source_statuses->status[0].is_enabled) {
                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty.Assign(true);
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                    state.read().source_statuses->status[0].current_buffer_id == 0) {
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .physical_address =
                        osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .length = NUM_SAMPLES;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .adpcm_dirty = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .is_looping = false;
                    state.write()
                        .source_configurations->config[0]
                        .buffers[next_queue_position]
                        .buffer_id = ++buffer_id;
                    state.write().source_configurations->config[0].buffers_dirty |=
                        1 << next_queue_position;
                    next_queue_position = (next_queue_position + 1) % 4;
                    state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
                }
            }

            state.notifyDsp();
        }

        // current_buffer_id should be 0 if the queue is not empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

        // Let the queue finish playing
        for (size_t frame_count = 0; frame_count < 10; frame_count++) {
            state.waitForSync();
            state.notifyDsp();
        }

        // current_buffer_id should be equal to buffer_id once the queue is empty
        REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);
    }

end:
    audioExit(state);
}
