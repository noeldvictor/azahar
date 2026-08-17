// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_base.h"

TEST_CASE("Presentation resolves the right eye only for layouts that consume it",
          "[video_core][renderer]") {
    Layout::FramebufferLayout layout{};
    layout.top_screen_enabled = true;
    layout.bottom_screen_enabled = true;
    layout.render_3d_mode = Settings::StereoRenderOption::Off;

    CHECK_FALSE(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::LeftEye));
    CHECK(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::RightEye));

    constexpr std::array stereo_modes = {
        Settings::StereoRenderOption::SideBySide,
        Settings::StereoRenderOption::SideBySideFull,
        Settings::StereoRenderOption::Anaglyph,
        Settings::StereoRenderOption::Interlaced,
        Settings::StereoRenderOption::ReverseInterlaced,
        Settings::StereoRenderOption::CardboardVR,
    };
    for (const auto mode : stereo_modes) {
        layout.render_3d_mode = mode;
        CAPTURE(mode);
        CHECK(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::LeftEye));
        CHECK(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::RightEye));
    }

    layout.top_screen_enabled = false;
    for (const auto mode : stereo_modes) {
        layout.render_3d_mode = mode;
        CAPTURE(mode);
        CHECK_FALSE(
            VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::RightEye));
    }

    layout.additional_screen_enabled = true;
    layout.additional_screen_is_bottom = false;
    CHECK(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::LeftEye));

    layout.additional_screen_is_bottom = true;
    CHECK_FALSE(VideoCore::PresentationNeedsRightEye(layout, Settings::MonoRenderOption::RightEye));
}
