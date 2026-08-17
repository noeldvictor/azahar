// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "common/arch.h"
#include "common/common_types.h"
#include "video_core/pica/command_list_utils.h"
#include "video_core/pica/regs_internal.h"

#if CITRA_ARCH(arm64)
namespace {

constexpr u32 ExpandWriteMask(u32 parameter_mask) {
    u32 result = 0;
    for (u32 byte = 0; byte < 4; ++byte) {
        if ((parameter_mask & (1u << byte)) != 0) {
            result |= 0xFFu << (byte * 8);
        }
    }
    return result;
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
#endif
