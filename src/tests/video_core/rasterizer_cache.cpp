// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/resource_retirement.h"

TEST_CASE("Resource retirement requires strictly newer runtime completion",
          "[video_core][rasterizer_cache]") {
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(0, 0));
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(6, 7));
    CHECK_FALSE(VideoCore::IsResourceRetirementComplete(7, 7));
    CHECK(VideoCore::IsResourceRetirementComplete(8, 7));
}
