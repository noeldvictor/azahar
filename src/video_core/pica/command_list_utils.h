// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/arch.h"
#include "common/common_types.h"

#if CITRA_ARCH(arm64)
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <arm_neon.h>

namespace Pica::CommandListUtils {

[[nodiscard]] inline bool ArePlainHeaders(uint32x4_t headers, u32 register_count) {
    const uint32x4_t ids = vandq_u32(headers, vdupq_n_u32(0xFFFFu));
    uint32x4_t invalid = vcgeq_u32(ids, vdupq_n_u32(register_count));
    invalid = vorrq_u32(invalid, vtstq_u32(headers, vdupq_n_u32(0x0FF00000u)));
    return vmaxvq_u32(invalid) == 0;
}

// Expand each command header's four parameter-mask bits into four byte-select lanes.
[[nodiscard]] inline uint32x4_t ExpandWriteMasks(uint32x4_t headers) {
    const uint32x4_t nibbles = vandq_u32(vshrq_n_u32(headers, 16), vdupq_n_u32(0xFu));
    const uint32x4_t replicated = vmulq_n_u32(nibbles, 0x01010101u);
    const uint8x16_t byte_bits = vreinterpretq_u8_u32(vdupq_n_u32(0x08040201u));
    return vreinterpretq_u32_u8(vtstq_u8(vreinterpretq_u8_u32(replicated), byte_bits));
}

[[nodiscard]] inline uint32x4_t ApplyWriteMasks(uint32x4_t old_values, uint32x4_t new_values,
                                                uint32x4_t write_masks) {
    return vbslq_u32(write_masks, new_values, old_values);
}

// PICA LUT uploads are circular. Once a table is already dirty, comparisons cannot change its
// state, so split batches into contiguous copies and skip every old-value load. Clean uploads keep
// the original loop so Clang can still auto-vectorize a non-wrapping power-of-two table. Seven
// words is the measured all-cluster copy crossover; smaller batches retain the scalar route.
template <typename Entry>
[[nodiscard]] inline bool UpdateCircularLut(Entry* destination, u32 size, u32 index,
                                            const u32* values, u32 count, bool dirty) {
    static_assert(sizeof(Entry) == sizeof(u32));
    static_assert(std::is_trivially_copyable_v<Entry>);

    if (!dirty || count < 7) {
        for (u32 i = 0; i < count; ++i) {
            Entry& entry = destination[(index + i) % size];
            const u32 previous = entry.raw;
            entry.raw = values[i];
            dirty |= previous != values[i];
        }
        return dirty;
    }

    index %= size;
    while (count != 0) {
        const u32 span = std::min(count, size - index);
        std::memcpy(destination + index, values, span * sizeof(u32));
        values += span;
        count -= span;
        index = 0;
    }
    return true;
}

} // namespace Pica::CommandListUtils
#endif
