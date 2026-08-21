// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace AndroidNativeState {

/// True only after the emulation loop has observed a pause request and stopped guest execution.
bool IsEmulationPaused();

/// Changes after every successful game load so one-session state cannot survive a relaunch.
std::uint64_t GetEmulationSessionId();

} // namespace AndroidNativeState
