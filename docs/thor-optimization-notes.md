# Thor Optimization Notes

These notes are for AYN Thor Base/Pro/Max only. The assumed target is Snapdragon 8 Gen 2 with Adreno 740, active cooling, LPDDR5X memory, and UFS4 storage. Thor Lite uses Snapdragon 865 / Adreno 650 and should be treated as a separate target.

## Current Baseline

- Android builds are `arm64-v8a` only.
- Android defaults prefer Vulkan when Vulkan is enabled (`src/common/settings.h`).
- CPU JIT, hardware shaders, shader JIT, disk shader cache, async filesystem operations, and async custom texture loading are already enabled by default.
- Internal resolution defaults to 1x. Game profiles may cap or override this for stability.
- Adreno custom driver loading is already wired through `libadrenotools` and `GpuDriverHelper`.
- E.X. Troopers (`0004000000053700`) currently has a hardcoded Android launch profile and matching manifest.
- Thor dual-display mode is fixed to 3DS top screen on the primary panel and 3DS bottom screen on the secondary panel. The old hidden virtual secondary display fallback is removed, so secondary rendering only starts when Android exposes a real second display.
- The Thor GPU Driver Manager now has a guided picker. It queries K11MCH1 AdrenoToolsDrivers releases, presents the newest generic Turnip ZIP as the recommended first pick, lists recent generic Turnip rollback builds with visible download buttons, also lists Qualcomm and Turnip variant troubleshooting choices when available, validates driver metadata, stores the ZIP under `gpu_drivers`, installs immediately, and still keeps manual ZIP and system-driver fallback paths.

## 2026-08-16 Upstream and RPCS3 ARM64 Review

- Merged 37 commits from `upstream/master` (`d81195bdc` through `b34de55b5`) in merge commit `abb63f2c3`.
- Mirrored the directly relevant sibling-project research under [`research/`](research/README.md), including the chapter-by-chapter notes for Whatcookie's "PS3 emulation is fast on ARM now" video and the follow-up "what didn't make the cut" article.
- Kept the new PICA command-list lookup, batching, and four-command vectorizable path from `c688076ac`. This is directly relevant to ARM host CPU time and energy because it reduces command-dispatch overhead without changing guest semantics.
- Kept the shader output register-banking work from `74d38ddcc`, including its A64 shader-JIT changes.
- Corrected the resource-tick comparison introduced by `b34de55b5`. A sentenced surface is retained while the runtime's completed tick is equal to or older than its retirement tick and is deleted only after the completed tick advances beyond it. The upstream comparison did the opposite and could retain sentenced surfaces indefinitely.

### RPCS3 ideas deliberately not copied

- **ISB-based spin waiting:** Azahar has no equivalent hot emulator/render spin loop. Dynarmic's ARM64 lock already uses `SEVL`/`WFE`, which avoids continuously burning a core. Replacing that without a Thor profile would risk higher power use.
- **Single-instruction `FMAX`/`FMIN`:** PICA's asymmetric NaN behavior differs from the PPC operation RPCS3 optimized. Azahar's A64 shader JIT intentionally uses `FCMGT` plus `BIF` to preserve PICA results.
- **SPU checksum, SHUFB, SHA3, and dot-product paths:** these target PS3 SPU/PPC workloads and have no direct 3DS guest equivalent. RPCS3's recent SHUFB change was also reverted, so it is not a stable source to port.
- **A dedicated Vulkan garbage-collection thread:** Azahar already has a Vulkan scheduler, presentation thread, master-semaphore completion waiter, and shader/pipeline workers. Another wake-producing thread is not justified until a Thor trace shows render-thread GC stalls; if needed, GC should first be attached to the existing completion path.
- **A global Cortex-X3 `-mcpu` setting:** Snapdragon 8 Gen 2 is a mixed X3/A715/A710/A510 system, and shipping Thor hardware does not expose every optional architecture feature such as SVE/SVE2. Runtime capability gates are safer than architecture-wide compiler assumptions.

### Next Thor measurements

- Compare the pre-merge baseline and this build in the same fixed title/scene, performance mode, fan mode, brightness, renderer, resolution, and GPU driver.
- Record average FPS, 1% low or frametime distribution, battery power, battery temperature, and thermal slope over a long enough run to reach steady state.
- Use Snapdragon Profiler to check render-pass binning cost and whether dependencies or barriers break concurrent binning. Qualcomm's guidance treats memory writes/resolves and unnecessary CPU-core wakeups as power costs.
- Compare the system Vulkan driver with the current stable Turnip option using both cold and warm caches. Do not claim a driver win until visual output and stability match.

## High-Value Optimization Places

1. Data-driven Thor game profiles

   `ApplyAndroidGameProfile()` currently hardcodes E.X. Troopers. Move this toward a small data-driven loader or generated map from `src/android/app/src/main/assets/game_profiles/*.ini` so per-title settings can be added without expanding native `if` blocks.

   Useful profile knobs: resolution cap, custom texture disable/preload disable, shader settings, frame limit, GPU timing simulation, render-thread delay, and title-specific compatibility hacks.

2. Adreno 740 Vulkan driver testing

   The fork can now fetch and install recent generic Turnip builds, Turnip variants, and a Qualcomm fallback, but this is not the same as a fully tested Thor driver matrix. Keep tracking which package works best for 3DS workloads on Thor Base/Pro/Max, and avoid silently forcing a driver without user action.

3. Shader stutter testing

   `async_shader_compilation` defaults to off. On Adreno 740/Vulkan it is worth A/B testing per title, especially for games with shader compilation hitching. Do not flip it globally until visual correctness is checked.

4. Resolution and texture guardrails

   The Thor 8 Gen 2 can handle more than native resolution in many titles, but 3x+ can still be a bad default for heavy games or dual-screen presentation. Keep default 1x, cap problem titles at 2x, and avoid preload/custom textures unless a title is proven stable.

5. Compatibility-cost toggles

   `simulate_3ds_gpu_timings` improves correctness but can cost performance in some games. `delay_game_render_thread_us` is available for dynamic-framerate edge cases. These should be per-title profile toggles, not global Thor defaults.

## Benchmark Checklist

- Test with the release-style Thor APK: `:app:assembleVanillaRelWithDebInfoLite`.
- Use the same Thor Control Center performance mode, fan mode, brightness, and driver before comparing.
- Capture FPS, frametime stability, speed percentage, battery temperature, and whether audio crackles.
- Run one cold-cache pass and one warm-cache pass.
- Record the title ID, region, ROM revision, cheat preset, renderer, internal resolution, secondary display layout, and GPU driver.
