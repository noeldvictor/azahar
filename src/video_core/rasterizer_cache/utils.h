// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include "common/math_util.h"
#include "common/vector_math.h"

namespace VideoCore {

struct Offset {
    u32 x = 0;
    u32 y = 0;
};

struct Extent {
    u32 width = 1;
    u32 height = 1;
};

union ClearValue {
    Common::Vec4f color;
    struct {
        float depth;
        u8 stencil;
    };
};

struct TextureClear {
    u32 texture_level;
    u32 texture_layer;
    Common::Rectangle<u32> texture_rect;
    ClearValue value;
};

struct TextureCopy {
    u32 src_level;
    u32 dst_level;
    u32 src_layer;
    u32 dst_layer;
    Offset src_offset;
    Offset dst_offset;
    Extent extent;
};

struct TextureBlit {
    u32 src_level;
    u32 dst_level;
    u32 src_layer;
    u32 dst_layer;
    Common::Rectangle<u32> src_rect;
    Common::Rectangle<u32> dst_rect;
};

struct BufferTextureCopy {
    u32 buffer_offset;
    u32 buffer_size;
    Common::Rectangle<u32> texture_rect;
    u32 texture_level;
};

struct StagingData {
    u32 size;
    u32 offset;
    std::span<u8> mapped;
};

enum class DepthStencilUnpackMode {
    D24Unorm,
    D32Float,
};

class SurfaceParams;
struct FramebufferParams;

u32 MipLevels(u32 width, u32 height, u32 max_level);

/**
 * Splits packed little-endian S8D24 pixels in-place into a four-byte depth plane followed by a
 * one-byte stencil plane. The supplied span must contain exactly five bytes of staging storage per
 * pixel. The first 4 * pixel_count bytes contain contiguous packed pixels and the final
 * pixel_count bytes are scratch space for stencil output.
 *
 * @return Size in bytes of the depth plane.
 */
u32 UnpackDepthStencil(std::span<u8> data, DepthStencilUnpackMode mode);

/**
 * Encodes a linear texture to the expected linear or tiled format.
 *
 * @param surface_info Structure used to query the surface information.
 * @param start_addr The start address of the dest data. Used if tiled.
 * @param end_addr The end address of the dest data. Used if tiled.
 * @param source_tiled The source linear texture data.
 * @param dest_linear The output buffer where the encoded linear or tiled data will be written to.
 * @param convert Whether the pixel format needs to be converted.
 */
void EncodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert = false);

/**
 * Decodes a linear or tiled texture to the expected linear format.
 *
 * @param surface_info Structure used to query the surface information.
 * @param start_addr The start address of the source data. Used if tiled.
 * @param end_addr The end address of the source data. Used if tiled.
 * @param source_tiled The source linear or tiled texture data.
 * @param dest_linear The output buffer where the decoded linear data will be written to.
 * @param convert Whether the pixel format needs to be converted.
 */
void DecodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert = false);

} // namespace VideoCore
