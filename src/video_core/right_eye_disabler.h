// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <utility>
#include "common/common_types.h"

namespace VideoCore {
class GPU;

class RightEyeDisabler {
public:
    RightEyeDisabler(GPU& gpu) : gpu{gpu} {}

    bool ShouldAllowCmdQueueTrigger(PAddr addr, u32 size);
    bool ShouldAllowDisplayTransfer(PAddr src_address, size_t size);

    void ReportEndFrame();

    void SetEnabled(bool enable) {
        enabled = enable;
    }

    /// Returns whether the just-finished frame deliberately skipped its right eye. The result is
    /// consumed once by presentation so a later frame cannot inherit stale skip state.
    bool ConsumeRightEyeSkippedForPresentation() {
        return std::exchange(right_eye_skipped_for_presentation, false);
    }

private:
    bool enabled = true;
    bool enable_for_frame = true;

    bool top_screen_drawn = false;
    bool top_screen_transfered = false;
    bool top_screen_blocked = false;
    PAddr top_screen_buf = 0;
    PAddr blocked_top_screen_buf = 0;

    bool cmd_queue_trigger_happened = false;
    bool display_tranfer_happened = false;
    bool report_end_frame_pending = false;
    bool right_eye_skipped_for_presentation = false;

    GPU& gpu;
};
} // namespace VideoCore
