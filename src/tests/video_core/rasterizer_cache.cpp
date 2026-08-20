// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/dirty_regions.h"
#include "video_core/rasterizer_cache/framebuffer_base.h"
#include "video_core/rasterizer_cache/resource_retirement.h"
#include "video_core/rasterizer_cache/surface_selection_cache.h"

TEST_CASE("Resource retirement requires strictly newer runtime completion",
          "[video_core][rasterizer_cache]") {
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(0, 0));
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(6, 7));
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(7, 7));
    CHECK(VideoCore::IsResourceRetirementComplete(8, 7));
}

TEST_CASE("Dirty region ownership detects semantic no-op updates",
          "[video_core][rasterizer_cache]") {
    using namespace VideoCore;

    constexpr SurfaceId owner{7};
    constexpr SurfaceId other_owner{8};
    DirtyRegionMap regions;
    regions.set({SurfaceInterval{0x1000, 0x2000}, owner});

    CHECK(IsRegionOwnedBy(regions, SurfaceInterval{0x1000, 0x2000}, owner));
    CHECK(IsRegionOwnedBy(regions, SurfaceInterval{0x1400, 0x1800}, owner));
    CHECK_FALSE(IsRegionOwnedBy(regions, SurfaceInterval{0x1000, 0x2000}, other_owner));
    CHECK_FALSE(IsRegionOwnedBy(regions, SurfaceInterval{0x0800, 0x1800}, owner));

    regions.set({SurfaceInterval{0x1800, 0x1c00}, other_owner});
    CHECK_FALSE(IsRegionOwnedBy(regions, SurfaceInterval{0x1400, 0x2000}, owner));
}

TEST_CASE("Framebuffer surface selection cache matches active attachments and generation",
          "[video_core][rasterizer_cache]") {
    using namespace VideoCore;

    const SurfaceParams color{
        .addr = 0x1000,
        .end = 0x2000,
        .size = 0x1000,
        .width = 400,
        .height = 240,
        .stride = 400,
        .res_scale = 2,
        .pixel_format = PixelFormat::RGBA8,
        .type = SurfaceType::Color,
    };
    const SurfaceParams depth{
        .addr = 0x3000,
        .end = 0x4000,
        .size = 0x1000,
        .width = 400,
        .height = 240,
        .stride = 400,
        .res_scale = 2,
        .pixel_format = PixelFormat::D24S8,
        .type = SurfaceType::DepthStencil,
    };
    const FramebufferSurfaceCache cache{
        .color_params = color,
        .depth_params = depth,
        .color_id = SurfaceId{7},
        .depth_id = SurfaceId{8},
        .generation = 11,
        .using_color = true,
        .using_depth = true,
        .valid = true,
    };

    CHECK(cache.Matches(color, depth, true, true, 11));
    CHECK_FALSE(cache.Matches(color, depth, true, true, 12));
    CHECK_FALSE(cache.Matches(color, depth, true, false, 11));

    auto changed_color = color;
    changed_color.addr += 0x1000;
    CHECK_FALSE(cache.Matches(changed_color, depth, true, true, 11));

    // SurfaceParams equality omits resolution scale, but selection-cache equality must not.
    changed_color = color;
    changed_color.res_scale = 3;
    CHECK(color == changed_color);
    CHECK_FALSE(cache.Matches(changed_color, depth, true, true, 11));
}

TEST_CASE("Framebuffer surface selection cache ignores inactive attachment parameters",
          "[video_core][rasterizer_cache]") {
    using namespace VideoCore;

    const SurfaceParams color{.addr = 0x1000, .end = 0x2000, .res_scale = 2};
    const SurfaceParams depth{.addr = 0x3000, .end = 0x4000, .res_scale = 2};
    const FramebufferSurfaceCache cache{
        .color_params = color,
        .depth_params = depth,
        .generation = 4,
        .using_color = true,
        .using_depth = false,
        .valid = true,
    };

    auto unused_depth = depth;
    unused_depth.addr = 0x9000;
    unused_depth.res_scale = 4;
    CHECK(cache.Matches(color, unused_depth, true, false, 4));

    auto active_color = color;
    active_color.addr = 0x5000;
    CHECK_FALSE(cache.Matches(active_color, unused_depth, true, false, 4));

    FramebufferSurfaceCache invalid_cache = cache;
    invalid_cache.valid = false;
    CHECK_FALSE(invalid_cache.Matches(color, unused_depth, true, false, 4));
}

TEST_CASE("Texture surface selection cache matches parameters, scale, and topology generation",
          "[video_core][rasterizer_cache]") {
    using namespace VideoCore;

    const SurfaceParams params{
        .addr = 0x1000,
        .end = 0x5000,
        .size = 0x4000,
        .width = 128,
        .height = 128,
        .stride = 128,
        .levels = 4,
        .res_scale = 2,
        .is_tiled = true,
        .pixel_format = PixelFormat::RGBA8,
        .type = SurfaceType::Color,
    };
    const TextureSurfaceCacheEntry entry{
        .surface_params = params,
        .surface_id = SurfaceId{9},
        .generation = 17,
        .valid = true,
    };

    CHECK(entry.Matches(params, 17));
    CHECK_FALSE(entry.Matches(params, 18));

    auto changed_params = params;
    changed_params.addr += 0x1000;
    CHECK_FALSE(entry.Matches(changed_params, 17));

    changed_params = params;
    changed_params.res_scale = 3;
    CHECK(params == changed_params);
    CHECK_FALSE(entry.Matches(changed_params, 17));

    TextureSurfaceCacheEntry invalid_entry = entry;
    invalid_entry.valid = false;
    CHECK_FALSE(invalid_entry.Matches(params, 17));
}
