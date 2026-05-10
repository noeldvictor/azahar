# Thor Optimization Notes

These notes are for AYN Thor Base/Pro/Max only. The assumed target is Snapdragon 8 Gen 2 with Adreno 740, active cooling, LPDDR5X memory, and UFS4 storage. Thor Lite uses Snapdragon 865 / Adreno 650 and should be treated as a separate target.

## Current Baseline

- Android builds are `arm64-v8a` only.
- Android defaults prefer Vulkan when Vulkan is enabled (`src/common/settings.h`).
- CPU JIT, hardware shaders, shader JIT, disk shader cache, async filesystem operations, and async custom texture loading are already enabled by default.
- Internal resolution defaults to 1x. Game profiles may cap or override this for stability.
- Adreno custom driver loading is already wired through `libadrenotools` and `GpuDriverHelper`.
- E.X. Troopers (`0004000000053700`) currently has a hardcoded Android launch profile and matching manifest.

## High-Value Optimization Places

1. Hidden secondary surface render cost

   Do not confuse the 3DS bottom screen with the Android secondary display surface. The 3DS bottom screen is normally needed, especially on Thor dual-screen layouts.

   `EmulationActivity` always creates `SecondaryDisplay`, and `SecondaryDisplay` falls back to a hidden 1920x1080 virtual display when the configured secondary layout is `None` or no real external display is selected. With `secondary_display_layout = None`, `AndroidSecondaryLayout()` falls back to a top-screen-only layout, so the renderer can spend GPU time drawing a duplicate hidden top screen. The Vulkan and OpenGL renderers render the secondary window whenever it exists.

   Candidate fix: keep rendering the bottom screen when a real Thor dual-screen layout is active, but do not create or render the hidden secondary surface while `secondary_display_layout = None`. If a secondary layout is selected but no real second display is available, make that failure visible instead of silently rendering offscreen.

2. Data-driven Thor game profiles

   `ApplyAndroidGameProfile()` currently hardcodes E.X. Troopers. Move this toward a small data-driven loader or generated map from `src/android/app/src/main/assets/game_profiles/*.ini` so per-title settings can be added without expanding native `if` blocks.

   Useful profile knobs: resolution cap, custom texture disable/preload disable, shader settings, frame limit, GPU timing simulation, render-thread delay, and title-specific compatibility hacks.

3. Adreno 740 Vulkan driver flow

   Custom GPU driver support exists, but the fork does not yet guide or enforce a known-good Adreno 740 driver path. Keep this conservative: expose current driver metadata clearly, document tested driver/firmware combos, and avoid silently swapping drivers.

4. Shader stutter testing

   `async_shader_compilation` defaults to off. On Adreno 740/Vulkan it is worth A/B testing per title, especially for games with shader compilation hitching. Do not flip it globally until visual correctness is checked.

5. Resolution and texture guardrails

   The Thor 8 Gen 2 can handle more than native resolution in many titles, but 3x+ can still be a bad default for heavy games or dual-screen presentation. Keep default 1x, cap problem titles at 2x, and avoid preload/custom textures unless a title is proven stable.

6. Compatibility-cost toggles

   `simulate_3ds_gpu_timings` improves correctness but can cost performance in some games. `delay_game_render_thread_us` is available for dynamic-framerate edge cases. These should be per-title profile toggles, not global Thor defaults.

## Benchmark Checklist

- Test with the release-style Thor APK: `:app:assembleVanillaRelWithDebInfoLite`.
- Use the same Thor Control Center performance mode, fan mode, brightness, and driver before comparing.
- Capture FPS, frametime stability, speed percentage, battery temperature, and whether audio crackles.
- Run one cold-cache pass and one warm-cache pass.
- Record the title ID, region, ROM revision, cheat preset, renderer, internal resolution, secondary display layout, and GPU driver.
