// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <boost/serialization/array.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/hle/shared_memory.h"

namespace AudioCore::HLE {

using PlanarQuadView32 = std::array<const s32*, 4>;

class Mixers final {
public:
    Mixers() {
        Reset();
    }

    void Reset();

    void Sleep();
    void Wakeup();

    DspStatus Tick(DspConfiguration& config, const IntermediateMixSamples& read_samples,
                   IntermediateMixSamples& write_samples,
                   const std::array<PlanarQuadFrame32, 3>& input);

    StereoFrame16 GetOutput() const {
        return current_frame;
    }

private:
    StereoFrame16 current_frame = {};
    StereoFrame16 backup_frame = {}; // TODO(PabloMK7): Check if we actually need this

    using OutputFormat = DspConfiguration::OutputFormat;

    struct MixerState {
        std::array<float, 3> intermediate_mixer_volume = {};

        std::array<bool, 2> aux_bus_enable = {};
        // Non-native-endian auxiliary returns stage here. Native-endian buses mix directly from
        // the current Tick() buffers; retain all three slots for save-state compatibility.
        std::array<PlanarQuadFrame32, 3> intermediate_mix_buffer = {};

        OutputFormat output_format = OutputFormat::Stereo;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & intermediate_mixer_volume;
            ar & aux_bus_enable;
            // Preserve the historical sample-major archive type and order after changing the live
            // HLE mixer layout to channel-major.
            std::array<QuadFrame32, 3> serialized_mix_buffer{};
            if constexpr (Archive::is_saving::value) {
                for (std::size_t mix = 0; mix < intermediate_mix_buffer.size(); ++mix) {
                    for (std::size_t sample = 0; sample < samples_per_frame; ++sample) {
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            serialized_mix_buffer[mix][sample][channel] =
                                intermediate_mix_buffer[mix][channel][sample];
                        }
                    }
                }
            }
            ar & serialized_mix_buffer;
            if constexpr (Archive::is_loading::value) {
                for (std::size_t mix = 0; mix < intermediate_mix_buffer.size(); ++mix) {
                    for (std::size_t sample = 0; sample < samples_per_frame; ++sample) {
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            intermediate_mix_buffer[mix][channel][sample] =
                                serialized_mix_buffer[mix][sample][channel];
                        }
                    }
                }
            }
            ar & output_format;
        }
    };

    MixerState state;
    MixerState backup_state;

    /// INTERNAL: Update our internal state based on the current config.
    void ParseConfig(DspConfiguration& config);
    /// INTERNAL: Read samples from shared memory that have been modified by the ARM11.
    void AuxReturn(const IntermediateMixSamples& read_samples);
    /// INTERNAL: Write samples to shared memory for the ARM11 to modify.
    void AuxSend(IntermediateMixSamples& write_samples,
                 const std::array<PlanarQuadFrame32, 3>& input);
    /// INTERNAL: Mix current_frame directly from current input or returned auxiliary samples.
    void MixCurrentFrame(const std::array<PlanarQuadFrame32, 3>& input,
                         const IntermediateMixSamples& read_samples);
    /// INTERNAL: Downmix from quadraphonic to stereo based on status.output_format and accumulate
    /// into current_frame, or define it directly for the first audible bus.
    void DownmixAndMixIntoCurrentFrame(float gain, const PlanarQuadView32& samples,
                                       bool accumulate);
    /// INTERNAL: Generate DspStatus based on internal state.
    DspStatus GetCurrentStatus() const;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & current_frame;
        ar & backup_frame;
        ar & state;
        ar & backup_state;
    }
    friend class boost::serialization::access;
};

} // namespace AudioCore::HLE
