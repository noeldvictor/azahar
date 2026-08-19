// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <random>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/arch.h"
#include "common/common_types.h"
#include "video_core/pica/command_list_utils.h"
#include "video_core/pica/regs_internal.h"

#if CITRA_ARCH(arm64)
namespace {

union LutEntry {
    u32 raw;
};

constexpr u32 ExpandWriteMask(u32 parameter_mask) {
    u32 result = 0;
    for (u32 byte = 0; byte < 4; ++byte) {
        if ((parameter_mask & (1u << byte)) != 0) {
            result |= 0xFFu << (byte * 8);
        }
    }
    return result;
}

template <std::size_t Size>
bool UpdateCircularLutScalar(std::array<LutEntry, Size>& destination, u32 index, const u32* values,
                             u32 count, bool dirty) {
    for (u32 i = 0; i < count; ++i) {
        LutEntry& entry = destination[(index + i) % Size];
        const u32 previous = entry.raw;
        entry.raw = values[i];
        dirty |= previous != values[i];
    }
    return dirty;
}

template <std::size_t Size>
void CheckCircularLutCase(u32 index, const std::vector<u32>& values, bool initial_dirty, u32 seed) {
    std::array<LutEntry, Size> expected{};
    std::array<LutEntry, Size> actual{};
    for (u32 i = 0; i < Size; ++i) {
        expected[i].raw = seed + i * 0x9E3779B9u;
        actual[i].raw = expected[i].raw;
    }

    const bool expected_dirty = UpdateCircularLutScalar(
        expected, index, values.data(), static_cast<u32>(values.size()), initial_dirty);
    const bool actual_dirty = Pica::CommandListUtils::UpdateCircularLut(
        actual.data(), static_cast<u32>(actual.size()), index, values.data(),
        static_cast<u32>(values.size()), initial_dirty);

    bool equal = expected_dirty == actual_dirty;
    for (u32 i = 0; i < Size; ++i) {
        equal &= expected[i].raw == actual[i].raw;
    }
    CAPTURE(Size, index, values.size(), initial_dirty, expected_dirty, actual_dirty, seed);
    REQUIRE(equal);
}

template <std::size_t Size>
void CheckUnchangedCircularLutCase(u32 index, u32 count, u32 seed) {
    std::vector<u32> values(count);
    for (u32 i = 0; i < count; ++i) {
        values[i] = seed + ((index + i) % Size) * 0x9E3779B9u;
    }
    CheckCircularLutCase<Size>(index, values, false, seed);
}

} // Anonymous namespace

TEST_CASE("PICA AArch64 command masks match scalar writes", "[video_core][aarch64]") {
    constexpr std::array<u32, 4> old_values = {
        0x00000000u,
        0xFFFFFFFFu,
        0x55AA33CCu,
        0xA55AC33Cu,
    };
    constexpr std::array<u32, 4> new_values = {
        0xFFFFFFFFu,
        0x00000000u,
        0xAA55CC33u,
        0x5AA53CC3u,
    };

    for (u32 packed_masks = 0; packed_masks < (1u << 16); ++packed_masks) {
        std::array<u32, 4> headers{};
        std::array<u32, 4> expected_masks{};
        std::array<u32, 4> expected_values{};
        for (u32 lane = 0; lane < 4; ++lane) {
            const u32 parameter_mask = (packed_masks >> (lane * 4)) & 0xFu;
            headers[lane] = parameter_mask << 16;
            expected_masks[lane] = ExpandWriteMask(parameter_mask);
            expected_values[lane] = (old_values[lane] & ~expected_masks[lane]) |
                                    (new_values[lane] & expected_masks[lane]);
        }

        std::array<u32, 4> actual_masks{};
        std::array<u32, 4> actual_values{};
        const uint32x4_t masks =
            Pica::CommandListUtils::ExpandWriteMasks(vld1q_u32(headers.data()));
        vst1q_u32(actual_masks.data(), masks);
        vst1q_u32(actual_values.data(),
                  Pica::CommandListUtils::ApplyWriteMasks(vld1q_u32(old_values.data()),
                                                          vld1q_u32(new_values.data()), masks));

        if (actual_masks != expected_masks || actual_values != expected_values) {
            CAPTURE(packed_masks, actual_masks, expected_masks, actual_values, expected_values);
            FAIL("AArch64 command-list mask expansion changed scalar register-write semantics");
        }
    }
}

TEST_CASE("PICA AArch64 command preflight matches scalar headers", "[video_core][aarch64]") {
    constexpr std::array<u32, 3> extra_lengths = {0, 1, 0xFF};

    for (u32 cmd_id = 0; cmd_id <= 0xFFFF; ++cmd_id) {
        for (const u32 extra_length : extra_lengths) {
            for (const u32 high_bits : {0u, 0xF0000000u}) {
                std::array<u32, 4> headers = {0xFu << 16, 0xFu << 16, 0xFu << 16, 0xFu << 16};
                const u32 lane = cmd_id & 3;
                headers[lane] = cmd_id | (0xAu << 16) | (extra_length << 20) | high_bits;

                const bool expected = cmd_id < Pica::RegsInternal::NUM_REGS && extra_length == 0;
                const bool actual = Pica::CommandListUtils::ArePlainHeaders(
                    vld1q_u32(headers.data()), Pica::RegsInternal::NUM_REGS);
                if (actual != expected) {
                    CAPTURE(cmd_id, extra_length, high_bits, lane, actual, expected);
                    FAIL("AArch64 command-list preflight changed scalar header semantics");
                }
            }
        }
    }
}

TEST_CASE("PICA AArch64 circular LUT batches match scalar writes", "[video_core][aarch64]") {
    constexpr std::array<u32, 24> counts = {
        0,  1,  2,  3,  4,   5,   6,   7,   8,   15,  16,  17,
        31, 32, 63, 64, 127, 128, 129, 255, 256, 257, 383, 511,
    };
    constexpr std::array<u32, 7> indices = {0, 1, 125, 127, 128, 129, 0xFFFF};

    for (const u32 count : counts) {
        for (const u32 index : indices) {
            for (const bool initial_dirty : {false, true}) {
                std::vector<u32> values(count);
                for (u32 i = 0; i < count; ++i) {
                    values[i] = 0xA5A5A5A5u ^ (i * 0x1020304u) ^ index;
                }
                CheckCircularLutCase<128>(index, values, initial_dirty, 0x10293847u);
                CheckCircularLutCase<256>(index, values, initial_dirty, 0x56473829u);
            }
            CheckUnchangedCircularLutCase<128>(index, count, 0x10293847u);
            CheckUnchangedCircularLutCase<256>(index, count, 0x56473829u);
        }
    }

    std::mt19937 random{0x141A64u};
    for (u32 trial = 0; trial < 10000; ++trial) {
        const u32 count = random() % 512;
        const u32 index = random() & 0xFFFF;
        std::vector<u32> values(count);
        for (u32& value : values) {
            value = random();
        }
        CheckCircularLutCase<128>(index, values, (random() & 1) != 0, random());
        CheckCircularLutCase<256>(index, values, (random() & 1) != 0, random());
    }
}
#endif
