// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <functional>
#include <boost/icl/interval_map.hpp>

#include "video_core/rasterizer_cache/slot_id.h"
#include "video_core/rasterizer_cache/surface_params.h"

namespace VideoCore {

using DirtyRegionMap = boost::icl::interval_map<PAddr, SurfaceId, boost::icl::partial_absorber,
                                                std::less, boost::icl::inplace_plus,
                                                boost::icl::inter_section, SurfaceInterval>;

[[nodiscard]] inline bool IsRegionOwnedBy(const DirtyRegionMap& regions,
                                          const SurfaceInterval& interval,
                                          SurfaceId owner) noexcept {
    return boost::icl::contains(regions, DirtyRegionMap::segment_type{interval, owner});
}

} // namespace VideoCore
