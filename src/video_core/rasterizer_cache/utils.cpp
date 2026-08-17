// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/rasterizer_cache/surface_params.h"
#include "video_core/rasterizer_cache/texture_codec.h"
#include "video_core/rasterizer_cache/utils.h"

namespace VideoCore {

namespace {

#if CITRA_ARCH(arm64)

template <bool depth_float>
inline void StoreDepthA64(u8* dest, uint32x4_t packed) {
    const uint32x4_t depth = vshrq_n_u32(packed, 8);
    if constexpr (depth_float) {
        const float32x4_t normalized =
            vdivq_f32(vcvtq_f32_u32(depth), vdupq_n_f32(16777215.0f));
        vst1q_u8(dest, vreinterpretq_u8_f32(normalized));
    } else {
        vst1q_u8(dest, vreinterpretq_u8_u32(depth));
    }
}

#endif

template <bool depth_float>
u32 UnpackDepthStencilImpl(std::span<u8> data) {
    ASSERT(data.size() % 5 == 0);
    if (data.empty()) {
        return 0;
    }

    const u32 pixel_count = static_cast<u32>(data.size() / 5);
    const u32 depth_size = pixel_count * sizeof(u32);
    u8* const depth_plane = data.data();
    u8* const stencil_plane = depth_plane + depth_size;
    u32 pixel = 0;

#if CITRA_ARCH(arm64)
    // Load the complete 16-pixel source band before overwriting its in-place depth output. A
    // narrowing tree gathers the low stencil byte of each u32 without LD4 or a table constant;
    // current AArch64 Clang folds the tree to three exact UZP1 operations.
    for (; pixel + 16 <= pixel_count; pixel += 16) {
        u8* const depth_output = depth_plane + pixel * sizeof(u32);
        const uint32x4_t packed_0 =
            vreinterpretq_u32_u8(vld1q_u8(depth_output + 0 * sizeof(uint32x4_t)));
        const uint32x4_t packed_1 =
            vreinterpretq_u32_u8(vld1q_u8(depth_output + 1 * sizeof(uint32x4_t)));
        const uint32x4_t packed_2 =
            vreinterpretq_u32_u8(vld1q_u8(depth_output + 2 * sizeof(uint32x4_t)));
        const uint32x4_t packed_3 =
            vreinterpretq_u32_u8(vld1q_u8(depth_output + 3 * sizeof(uint32x4_t)));

        StoreDepthA64<depth_float>(depth_output + 0 * sizeof(uint32x4_t), packed_0);
        StoreDepthA64<depth_float>(depth_output + 1 * sizeof(uint32x4_t), packed_1);
        StoreDepthA64<depth_float>(depth_output + 2 * sizeof(uint32x4_t), packed_2);
        StoreDepthA64<depth_float>(depth_output + 3 * sizeof(uint32x4_t), packed_3);

        const uint16x8_t stencil_01 =
            vmovn_high_u32(vmovn_u32(packed_0), packed_1);
        const uint16x8_t stencil_23 =
            vmovn_high_u32(vmovn_u32(packed_2), packed_3);
        const uint8x16_t stencil =
            vmovn_high_u16(vmovn_u16(stencil_01), stencil_23);
        vst1q_u8(stencil_plane + pixel, stencil);
    }
#endif

    for (; pixel < pixel_count; ++pixel) {
        u8* const depth_output = depth_plane + pixel * sizeof(u32);
        const u32 packed = MakeInt<u32>(depth_output);
        const u32 depth = packed >> 8;
        stencil_plane[pixel] = static_cast<u8>(packed);
        if constexpr (depth_float) {
            const float normalized = static_cast<float>(depth) / 16777215.0f;
            std::memcpy(depth_output, &normalized, sizeof(normalized));
        } else {
            std::memcpy(depth_output, &depth, sizeof(depth));
        }
    }

    return depth_size;
}

} // Anonymous namespace

u32 MipLevels(u32 width, u32 height, u32 max_level) {
    u32 levels = 1;
    while (width > 8 && height > 8) {
        levels++;
        width >>= 1;
        height >>= 1;
    }

    return std::min(levels, max_level + 1);
}

u32 UnpackDepthStencil(std::span<u8> data, DepthStencilUnpackMode mode) {
    switch (mode) {
    case DepthStencilUnpackMode::D24Unorm:
        return UnpackDepthStencilImpl<false>(data);
    case DepthStencilUnpackMode::D32Float:
        return UnpackDepthStencilImpl<true>(data);
    }
    UNREACHABLE();
}

void EncodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert) {
    const PixelFormat format = surface_info.pixel_format;
    const u32 func_index = static_cast<u32>(format);

    if (surface_info.is_tiled) {
        const MortonFunc SwizzleImpl =
            (convert ? SWIZZLE_TABLE_CONVERTED : SWIZZLE_TABLE)[func_index];
        if (SwizzleImpl) {
            SwizzleImpl(surface_info.width, surface_info.height, start_addr - surface_info.addr,
                        end_addr - surface_info.addr, source, dest);
            return;
        }
    } else {
        const LinearFunc LinearEncodeImpl =
            (convert ? LINEAR_ENCODE_TABLE_CONVERTED : LINEAR_ENCODE_TABLE)[func_index];
        if (LinearEncodeImpl) {
            LinearEncodeImpl(source, dest);
            return;
        }
    }

    LOG_ERROR(HW_GPU, "Unimplemented texture encode function for pixel format = {}, tiled = {}",
              func_index, surface_info.is_tiled);
    UNIMPLEMENTED();
}

void DecodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert) {
    const PixelFormat format = surface_info.pixel_format;
    const u32 func_index = static_cast<u32>(format);

    if (surface_info.is_tiled) {
        const MortonFunc UnswizzleImpl =
            (convert ? UNSWIZZLE_TABLE_CONVERTED : UNSWIZZLE_TABLE)[func_index];
        if (UnswizzleImpl) {
            UnswizzleImpl(surface_info.width, surface_info.height, start_addr - surface_info.addr,
                          end_addr - surface_info.addr, dest, source);
            return;
        }
    } else {
        const LinearFunc LinearDecodeImpl =
            (convert ? LINEAR_DECODE_TABLE_CONVERTED : LINEAR_DECODE_TABLE)[func_index];
        if (LinearDecodeImpl) {
            LinearDecodeImpl(source, dest);
            return;
        }
    }

    LOG_ERROR(HW_GPU, "Unimplemented texture decode function for pixel format = {}, tiled = {}",
              func_index, surface_info.is_tiled);
    UNIMPLEMENTED();
}

} // namespace VideoCore
