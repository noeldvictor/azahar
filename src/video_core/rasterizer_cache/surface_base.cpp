// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include "common/alignment.h"
#include "video_core/custom_textures/material.h"
#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/texture/texture_decode.h"

namespace VideoCore {

SurfaceBase::SurfaceBase(const SurfaceParams& params, const SurfaceFlagBits& initial_flag_bits)
    : SurfaceParams{params}, flags(initial_flag_bits) {}

SurfaceBase::~SurfaceBase() = default;

bool SurfaceBase::CanFill(const SurfaceParams& dest_surface, SurfaceInterval fill_interval) const {
    if (type == SurfaceType::Fill && IsRegionValid(fill_interval) &&
        boost::icl::first(fill_interval) >= addr &&
        boost::icl::last_next(fill_interval) <= end && // dest_surface is within our fill range
        dest_surface.FromInterval(fill_interval).GetInterval() ==
            fill_interval) { // make sure interval is a rectangle in dest surface
        if (fill_size * 8 != dest_surface.GetFormatBpp()) {
            // Check if bits repeat for our fill_size
            const u32 dest_bytes_per_pixel = std::max(dest_surface.GetFormatBpp() / 8, 1u);
            std::array<u8, 16> fill_test;
            ASSERT(fill_size * dest_bytes_per_pixel <= fill_test.size());

            for (u32 i = 0; i < dest_bytes_per_pixel; ++i) {
                std::memcpy(&fill_test[i * fill_size], &fill_data[0], fill_size);
            }

            for (u32 i = 0; i < fill_size; ++i) {
                if (std::memcmp(&fill_test[dest_bytes_per_pixel * i], &fill_test[0],
                                dest_bytes_per_pixel) != 0) {
                    return false;
                }
            }

            if (dest_surface.GetFormatBpp() == 4 && (fill_test[0] & 0xF) != (fill_test[0] >> 4)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void SurfaceBase::FillMemory(u8* destination, std::size_t start_offset,
                             std::size_t end_offset) const {
    ASSERT(fill_size > 0 && fill_size <= fill_data.size());
    ASSERT(start_offset <= end_offset);
    if (start_offset == end_offset) {
        return;
    }

    const std::size_t fill_bytes = fill_size;
    const bool is_single_value =
        std::all_of(fill_data.begin() + 1, fill_data.begin() + fill_bytes,
                    [value = fill_data.front()](const u8 byte) { return byte == value; });
    if (is_single_value) {
        std::memset(destination + start_offset, fill_data.front(), end_offset - start_offset);
        return;
    }

    // Reach the next pattern boundary without touching bytes before start_offset.
    std::size_t write_offset = start_offset;
    const std::size_t phase = write_offset % fill_bytes;
    if (phase != 0) {
        const std::size_t prefix_bytes = std::min(fill_bytes - phase, end_offset - write_offset);
        std::memcpy(destination + write_offset, fill_data.data() + phase, prefix_bytes);
        write_offset += prefix_bytes;
    }
    if (write_offset == end_offset) {
        return;
    }

    // Seed one pattern, then copy the initialized prefix exponentially. Each memcpy source ends
    // at or before its destination, so the ranges never overlap.
    const std::size_t pattern_start = write_offset;
    const std::size_t seed_bytes = std::min(fill_bytes, end_offset - write_offset);
    std::memcpy(destination + write_offset, fill_data.data(), seed_bytes);
    write_offset += seed_bytes;

    std::size_t initialized_bytes = seed_bytes;
    while (write_offset < end_offset) {
        const std::size_t copy_bytes = std::min(initialized_bytes, end_offset - write_offset);
        std::memcpy(destination + write_offset, destination + pattern_start, copy_bytes);
        write_offset += copy_bytes;
        initialized_bytes += copy_bytes;
    }
}

bool SurfaceBase::CanCopy(const SurfaceParams& dest_surface, SurfaceInterval copy_interval) const {
    const SurfaceParams subrect_params = dest_surface.FromInterval(copy_interval);
    ASSERT(subrect_params.GetInterval() == copy_interval);

    if (CanSubRect(subrect_params)) {
        return true;
    }

    if (CanFill(dest_surface, copy_interval)) {
        return true;
    }

    return false;
}

SurfaceInterval SurfaceBase::GetCopyableInterval(const SurfaceParams& params) const {
    SurfaceInterval result{};
    const u32 tile_align = params.BytesInPixels(params.is_tiled ? 8 * 8 : 1);
    const auto valid_regions =
        SurfaceRegions{params.GetInterval() & GetInterval()} - invalid_regions;

    for (auto& valid_interval : valid_regions) {
        const SurfaceInterval aligned_interval{
            params.addr +
                Common::AlignUp(boost::icl::first(valid_interval) - params.addr, tile_align),
            params.addr +
                Common::AlignDown(boost::icl::last_next(valid_interval) - params.addr, tile_align)};

        if (tile_align > boost::icl::length(valid_interval) ||
            boost::icl::length(aligned_interval) == 0) {
            continue;
        }

        // Get the rectangle within aligned_interval
        const u32 stride_bytes = params.BytesInPixels(params.stride) * (params.is_tiled ? 8 : 1);
        SurfaceInterval rect_interval{
            params.addr +
                Common::AlignUp(boost::icl::first(aligned_interval) - params.addr, stride_bytes),
            params.addr + Common::AlignDown(boost::icl::last_next(aligned_interval) - params.addr,
                                            stride_bytes),
        };

        if (boost::icl::first(rect_interval) > boost::icl::last_next(rect_interval)) {
            // 1 row
            rect_interval = aligned_interval;
        } else if (boost::icl::length(rect_interval) == 0) {
            // 2 rows that do not make a rectangle, return the larger one
            const SurfaceInterval row1{boost::icl::first(aligned_interval),
                                       boost::icl::first(rect_interval)};
            const SurfaceInterval row2{boost::icl::first(rect_interval),
                                       boost::icl::last_next(aligned_interval)};
            rect_interval = (boost::icl::length(row1) > boost::icl::length(row2)) ? row1 : row2;
        }

        if (boost::icl::length(rect_interval) > boost::icl::length(result)) {
            result = rect_interval;
        }
    }
    return result;
}

Extent SurfaceBase::RealExtent(bool scaled) const {
    const bool is_custom = IsCustom();
    u32 real_width = width;
    u32 real_height = height;
    if (is_custom) {
        real_width = material->width;
        real_height = material->height;
    } else if (scaled) {
        real_width = GetScaledWidth();
        real_height = GetScaledHeight();
    }
    return Extent{
        .width = real_width,
        .height = real_height,
    };
}

bool SurfaceBase::HasNormalMap() const noexcept {
    return material && material->Map(MapType::Normal) != nullptr;
}

ClearValue SurfaceBase::MakeClearValue(PAddr copy_addr, PixelFormat dst_format) {
    const SurfaceType dst_type = GetFormatType(dst_format);
    const std::array fill_buffer = MakeFillBuffer(copy_addr);

    ClearValue result{};
    switch (dst_type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
    case SurfaceType::Fill: {
        Pica::Texture::TextureInfo tex_info{};
        tex_info.format = static_cast<Pica::TexturingRegs::TextureFormat>(dst_format);
        const auto color = Pica::Texture::LookupTexture(fill_buffer.data(), 0, 0, tex_info);
        result.color = color / 255.f;
        break;
    }
    case SurfaceType::Depth: {
        u32 depth_uint = 0;
        if (dst_format == PixelFormat::D16) {
            std::memcpy(&depth_uint, fill_buffer.data(), 2);
            result.depth = depth_uint / 65535.0f; // 2^16 - 1
        } else if (dst_format == PixelFormat::D24) {
            std::memcpy(&depth_uint, fill_buffer.data(), 3);
            result.depth = depth_uint / 16777215.0f; // 2^24 - 1
        }
        break;
    }
    case SurfaceType::DepthStencil: {
        u32 clear_value_uint;
        std::memcpy(&clear_value_uint, fill_buffer.data(), sizeof(u32));
        result.depth = (clear_value_uint & 0xFFFFFF) / 16777215.0f; // 2^24 - 1
        result.stencil = (clear_value_uint >> 24);
        break;
    }
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }

    return result;
}

std::array<u8, 4> SurfaceBase::MakeFillBuffer(PAddr copy_addr) {
    const PAddr fill_offset = (copy_addr - addr) % fill_size;
    std::array<u8, 4> fill_buffer;

    u32 fill_buff_pos = fill_offset;
    for (std::size_t i = 0; i < fill_buffer.size(); i++) {
        fill_buffer[i] = fill_data[fill_buff_pos++ % fill_size];
    }

    return fill_buffer;
}

} // namespace VideoCore
