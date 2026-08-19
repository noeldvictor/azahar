// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>

#include "common/arch.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/pica/output_vertex.h"

#if CITRA_ARCH(arm64)
#include <arm_neon.h>
#endif

namespace Pica::VertexCacheUtils {

constexpr u32 MAX_BOUNDED_OUTPUTS = 6;

// Copy the packed prefix produced by ShaderUnit::WriteOutput(). Callers select this bounded path
// once per draw so wider shaders retain the established branch-free full copy inside the loop.
[[gnu::always_inline]] inline void CopyVertexOutputPrefix(AttributeBuffer& destination,
                                                          const AttributeBuffer& source,
                                                          u32 count) {
    DEBUG_ASSERT(count <= MAX_BOUNDED_OUTPUTS);

    // Enter once at the live prefix length, then copy one 16-byte attribute per straight-line
    // step. This avoids both the old unconditional 256-byte transfer and a counted-loop branch.
    switch (count) {
    case 6:
        destination[5] = source[5];
        [[fallthrough]];
    case 5:
        destination[4] = source[4];
        [[fallthrough]];
    case 4:
        destination[3] = source[3];
        [[fallthrough]];
    case 3:
        destination[2] = source[2];
        [[fallthrough]];
    case 2:
        destination[1] = source[1];
        [[fallthrough]];
    case 1:
        destination[0] = source[0];
        [[fallthrough]];
    case 0:
        return;
    default:
        UNREACHABLE();
    }
}

// Return the first matching entry, or count when the vertex is not cached.
[[nodiscard]] inline u32 FindVertex(const u16* ids, u32 count, u16 vertex) {
    u32 index = 0;

#if CITRA_ARCH(arm64)
    constexpr u32 entries_per_band = 16;
    alignas(16) static constexpr std::array<u8, entries_per_band> lane_indices = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    };

    const uint16x8_t needle = vdupq_n_u16(vertex);
    const uint8x16_t lanes = vld1q_u8(lane_indices.data());
    const uint8x16_t no_match = vdupq_n_u8(0xFF);

    for (; index + entries_per_band <= count; index += entries_per_band) {
        const uint16x8_t matches_low = vceqq_u16(vld1q_u16(ids + index), needle);
        const uint16x8_t matches_high = vceqq_u16(vld1q_u16(ids + index + 8), needle);
        const uint8x8_t matches_low_narrow = vmovn_u16(matches_low);
        const uint8x16_t matches = vmovn_high_u16(matches_low_narrow, matches_high);

        // Inactive lanes select 0xFF, so UMINV returns the first matching lane. This also
        // preserves the old scalar loop's behavior if duplicate IDs ever reach the cache.
        const u8 lane = vminvq_u8(vbslq_u8(matches, lanes, no_match));
        if (lane != 0xFF) {
            return index + lane;
        }
    }
#endif

    for (; index < count; ++index) {
        if (ids[index] == vertex) {
            return index;
        }
    }
    return count;
}

} // namespace Pica::VertexCacheUtils
