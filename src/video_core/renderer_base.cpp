// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/tracer/recorder.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/renderer_base.h"

namespace VideoCore {

RendererBase::RendererBase(Core::System& system_, Frontend::EmuWindow& window,
                           Frontend::EmuWindow* secondary_window_)
    : system{system_}, render_window{window}, secondary_window{secondary_window_} {}

RendererBase::~RendererBase() = default;

u32 RendererBase::GetResolutionScaleFactor() {
    const auto graphics_api = Settings::GetWorkingGraphicsAPI();
    if (graphics_api == Settings::GraphicsAPI::Software) {
        // Software renderer always render at native resolution
        return 1;
    }

    const u32 scale_factor = Settings::values.resolution_factor.GetValue();
    return scale_factor != 0 ? scale_factor
                             : render_window.GetFramebufferLayout().GetScalingRatio();
}

void RendererBase::UpdateCurrentFramebufferLayout(bool is_portrait_mode) {
    const auto update_layout = [is_portrait_mode](Frontend::EmuWindow& window) {
        const Layout::FramebufferLayout& layout = window.GetFramebufferLayout();
        window.UpdateCurrentFramebufferLayout(layout.width, layout.height, is_portrait_mode);
    };
    update_layout(render_window);
    if (secondary_window != nullptr) {
        update_layout(*secondary_window);
    }
}

void RendererBase::EndFrame() {
    current_frame++;

    system.perf_stats->EndSystemFrame();

    render_window.PollEvents();

    system.frame_limiter.DoFrameLimiting(system.CoreTiming().GetGlobalTimeUs());
    system.perf_stats->BeginSystemFrame();
}

bool RendererBase::ShouldPresentFrame() {
#ifdef ANDROID
    constexpr double normal_speed = 100.0;
    constexpr double normal_refresh_rate = 60.0;
    const auto now = std::chrono::steady_clock::now();
    const double frame_limit = Settings::GetFrameLimit();
    if (!Settings::values.eco_turbo.GetValue() || frame_limit <= normal_speed) {
        eco_turbo_budget_update = now;
        eco_turbo_present_budget = 1.0;
        return true;
    }

    const std::chrono::duration<double> elapsed = now - eco_turbo_budget_update;
    eco_turbo_budget_update = now;
    // Refill from real elapsed time so a scene producing at most 60 frames per second never loses
    // a frame, even when its requested turbo limit is much higher than its achieved speed.
    eco_turbo_present_budget =
        std::min(1.0, eco_turbo_present_budget + elapsed.count() * normal_refresh_rate);
    if (eco_turbo_present_budget < 1.0) {
        return false;
    }
    eco_turbo_present_budget -= 1.0;
#endif
    return true;
}

bool RendererBase::IsScreenshotPending() const {
    return settings.screenshot_requested;
}

void RendererBase::RequestScreenshot(void* data, std::function<void(bool)> callback,
                                     const Layout::FramebufferLayout& layout) {
    if (settings.screenshot_requested) {
        LOG_ERROR(Render, "A screenshot is already requested or in progress, ignoring the request");
        return;
    }
    settings.screenshot_bits = data;
    settings.screenshot_complete_callback = callback;
    settings.screenshot_framebuffer_layout = layout;
    settings.screenshot_requested = true;
}
} // namespace VideoCore
