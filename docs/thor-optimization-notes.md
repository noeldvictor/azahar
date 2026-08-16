# Thor Optimization Notes

These notes are for AYN Thor Base/Pro/Max only. The assumed target is Snapdragon 8 Gen 2 with Adreno 740, active cooling, LPDDR5X memory, and UFS 3.1 storage per AYN's current product page. The mirrored device manual instead reports UFS 4.0, so storage generation remains unverified until checked on the physical device. Thor Lite uses Snapdragon 865 / Adreno 650 and should be treated as a separate target.

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

## 2026-08-16 3DS and AYN Manual Review

- Added the public Arm ARM11 MPCore DDI 0360E and ARM946E-S DDI 0201D manuals to the untracked workspace reference library. Their cache, control, WFE/WFI, and cycle-timing sections are guest correctness and instrumentation inputs; they are not host optimization recipes.
- The ARM11 manual documents parallel ALU, multiply, and load/store pipelines with forwarding and instruction-dependent interlocks. Optimize Dynarmic output from measured Snapdragon hot paths rather than trying to preserve guest pipeline structure in generated ARM64 code.
- No complete public PICA200 technical reference manual was found. The archived DMP SIGGRAPH 2007 slide is a high-level, pre-3DS description of PICA200/MAESTRO features and nominal power/throughput, not an authoritative register or shader reference.
- The AYN manual confirms a 120 Hz primary display and 60 Hz secondary display. Benchmark at fixed refresh/brightness and account for the second panel when comparing power; a frame limiter that avoids needless work above the guest rate is more useful than targeting the panel maximum.
- The AYN manual and current AYN product page disagree about UFS generation. Do not use either UFS 3.1 or UFS 4.0 bandwidth as a performance explanation until the connected Thor is queried or measured.
- Full provenance, hashes, and source links live in [`hardware/README.md`](hardware/README.md). The PDF binaries remain outside Git by explicit project policy.

## 2026-08-16 AArch64 Indexed-Draw Reduction

- `RasterizerAccelerated::AnalyzeVertexArray()` scans every indexed draw's `u8` or `u16` index buffer through `Common::FindMinMax()`. The release AArch64 binary proved that the original NEON port accumulated vector minima/maxima but then stored the vectors to the stack and expanded `std::min_element` / `std::max_element` into per-lane extracts, comparisons, conditional selects, stack traffic, and stack-protector work.
- The AArch64 path now reduces the vectors with the architecture's `vminvq_u8` / `vmaxvq_u8` and `vminvq_u16` / `vmaxvq_u16` intrinsics. The SIMD crossover is one full vector on AArch64 (16 byte indices or 8 halfword indices); x86 SSE4.2 and 32-bit NEON retain their existing two-vector crossover and fallback behavior.
- Correctness is unchanged because unsigned horizontal min/max is associative and produces the same extrema as reducing the vector lanes in scalar order. Scalar tails still process every non-vector-aligned index. The empty and scalar-only cases now also initialize their extrema explicitly instead of relying on uninitialized fallback values.
- A focused test checks every prefix length across scalar, exact-vector, multi-vector, and tail cases for both index widths against `std::minmax_element`. The test source compiled and linked into the AArch64 test executable during the release build; the same matrix also passed as a native scalar smoke test (exit code 0).
- Binary verification on the built `libcitra-android.so` found one `uminv` and one `umaxv` in each function, with zero `umov` lane extracts and zero stack-check references. The `u8` function shrank from 904 to 284 bytes and the `u16` function from 600 to 284 bytes: 936 bytes, or 62.2%, removed from the pair.
- `:app:assembleVanillaRelWithDebInfoLite` completed successfully. The generated APK is 28,944,839 bytes with SHA-256 `CBD28CDBD3F254FA8F896AFBEF02D95EEF87F9AF55068EA121030363FCADF152`.
- Device execution and power/FPS claims remain pending: Windows currently returns an empty `adb devices -l` list even though the Thor is physically connected. Once ADB enumerates it, compare a fixed indexed-draw-heavy scene with identical title, cache state, renderer, resolution, driver, display layout, performance/fan mode, and brightness. Record FPS, frametime distribution, battery power, temperature, and thermal slope; this source/codegen improvement is not yet an on-device wattage claim.

## 2026-08-16 AArch64 HLE Audio Downmix

- The 3DS HLE audio final mixer downmixes three 160-sample quadraphonic buses into stereo or mono every DSP frame. The original 2016 generic loop remained scalar in the release AArch64 object even though the larger per-source mixer loop auto-vectorizes successfully.
- The AArch64 path now processes four frames at a time with NEON structure loads/stores, vector integer-to-float conversion, the same multiply/FMA order as the old AArch64 code, truncating float-to-integer conversion, saturating `s32`-to-`s16` narrowing, and saturating accumulation into the current stereo frame. Mono, stereo, and the existing surround-as-stereo fallback all retain their prior behavior; non-AArch64 builds retain the scalar implementation.
- A focused end-to-end mixer test feeds all three buses with lane-varying values that cross both saturation limits and compares mono, stereo, and surround output with the scalar reference. It compiled and linked into the optimized AArch64 test executable. Device execution remains pending until ADB enumerates the physically connected Thor.
- Exact release codegen changed from one-sample scalar loops to four-sample NEON loops. The stereo body fell from 39 instructions per sample to 20 instructions per four samples (5 per sample), while the mono body fell from 35 instructions per sample to 19 instructions per four samples (4.75 per sample). The containing function shrank from 436 to 280 bytes, a 156-byte or 35.8% reduction.
- The complete `:app:assembleVanillaRelWithDebInfoLite` build passed in 5m33s. The APK is 28,945,479 bytes with SHA-256 `34549C9F41FB5B6D773E40DC8DBD26E9B22D5DA210453915F1358873E2A067B2`.
- The same machine-code audit rejected several tempting false positives: Crypto++ already compiles Rijndael with `-march=armv8-a+crypto`; SoundTouch ships in 16-bit integer mode and its correlation loop already auto-vectorizes to NEON; and the per-source 24-channel HLE mixer already becomes an eight-frame NEON loop. Crypto acceleration can improve encrypted content and service latency, but it is not currently evidence of a sustained FPS or wattage win.

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
