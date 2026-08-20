// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace AndroidNativeState {

/// True only after the emulation loop has observed a pause request and stopped guest execution.
bool IsEmulationPaused();

} // namespace AndroidNativeState
