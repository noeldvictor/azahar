// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/rasterizer_cache/slot_id.h"
#include "video_core/rasterizer_cache/surface_params.h"

namespace VideoCore {

struct TextureSurfaceCacheEntry {
    [[nodiscard]] bool Matches(const SurfaceParams& params,
                               u64 current_generation) const noexcept {
        return valid && generation == current_generation && surface_params == params &&
               surface_params.res_scale == params.res_scale;
    }

    SurfaceParams surface_params;
    SurfaceId surface_id{};
    u64 generation{};
    bool valid{};
};

} // namespace VideoCore
