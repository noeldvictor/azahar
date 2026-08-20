// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/dirty_regions.h"
#include "video_core/rasterizer_cache/resource_retirement.h"

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
