// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace VideoCore {

/// A sentenced resource is safe to destroy only after its runtime completion tick advances past
/// the tick observed at sentencing. Equality must retain it because work from that tick may still
/// be queued or in flight.
[[nodiscard]] constexpr bool IsResourceRetirementComplete(u64 completed_tick,
                                                          u64 retirement_tick) noexcept {
    return completed_tick > retirement_tick;
}

} // namespace VideoCore
