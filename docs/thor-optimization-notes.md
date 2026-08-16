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

- **RPCS3 timer-scaled `busy_wait`:** [RPCS3 #18055](https://github.com/RPCS3/rpcs3/pull/18055) fixed waits that treated a low-frequency ARM generic timer like a multi-GHz x86 cycle counter. Azahar has no equivalent host-timer-calibrated busy-wait utility in its active CPU, audio, or Vulkan paths, so there is no constant to copy. Adding a second correction would repeat the kind of double-calibration regression already seen in related ARM ports.
- **ISB-based spin waiting:** Azahar has no equivalent hot emulator/render spin loop. Its Vulkan scheduler and master semaphore block on condition variables, Vulkan fences, or timeline semaphores, while Dynarmic's ARM64 lock already uses `SEVL`/`WFE`. [RPCS3 #18151](https://github.com/RPCS3/rpcs3/pull/18151) was a small improvement over ineffective ARM `yield`, and [RPCS3 #18830](https://github.com/RPCS3/rpcs3/pull/18830) later confirmed that hardware waits are the better primitive but did not measure an application-level power win at its first call sites.
- **Single-instruction `FMAX`/`FMIN`:** PICA's asymmetric NaN behavior differs from the PPC operation RPCS3 optimized. Azahar's A64 shader JIT intentionally uses `FCMGT` plus `BIF` to preserve PICA results.
- **SPU checksum, SHUFB, SHA3, and dot-product paths:** these target PS3 SPU/PPC workloads and have no direct 3DS guest equivalent. [RPCS3 #18056](https://github.com/RPCS3/rpcs3/pull/18056) is still useful as a method: express the guest permutation directly with native ARM vector operations and verify final codegen. Azahar's A64 PICA shader JIT already emits native table lookup for its swizzle operation, and its A64/x64 JIT compiler method sets are currently equal at 44 methods each.
- **LLVM ARM feature attributes:** [RPCS3 #18133](https://github.com/RPCS3/rpcs3/pull/18133) prevents LLVM from assuming that Snapdragon 8 Gen 2 exposes the Cortex-X3's disabled SVE feature. Azahar's 3DS CPU backend is Dynarmic rather than an LLVM guest recompiler and targets baseline AArch64/NEON, so it cannot reuse that patch. Azahar's existing AArch64 feature detector currently feeds host-information logging, not generated-code feature attributes; optional dot-product/i8mm paths should be added only with Android HWCAP gates and a proven hot integer kernel.
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
- A focused test checks every prefix length across scalar, exact-vector, multi-vector, and tail cases for both index widths against `std::minmax_element`. It passed on the AYN Thor's Snapdragon 8 Gen 2 with 200 assertions in one test case.
- Binary verification on the built `libcitra-android.so` found one `uminv` and one `umaxv` in each function, with zero `umov` lane extracts and zero stack-check references. The `u8` function shrank from 904 to 284 bytes and the `u16` function from 600 to 284 bytes: 936 bytes, or 62.2%, removed from the pair.
- `:app:assembleVanillaRelWithDebInfoLite` completed successfully. The generated APK is 28,944,839 bytes with SHA-256 `CBD28CDBD3F254FA8F896AFBEF02D95EEF87F9AF55068EA121030363FCADF152`.
- ADB now enumerates the Thor over USB as `c3ca0370` and over Wi-Fi as `192.168.1.33:5555`; USB is the deterministic deployment target. The focused correctness test passes on device, but power/FPS claims remain pending. Compare a fixed indexed-draw-heavy scene with identical title, cache state, renderer, resolution, driver, display layout, performance/fan mode, and brightness, then record FPS, frametime distribution, battery power, temperature, and thermal slope.

## 2026-08-16 AArch64 HLE Audio Downmix

- The 3DS HLE audio final mixer downmixes three 160-sample quadraphonic buses into stereo or mono every DSP frame. The original 2016 generic loop remained scalar in the release AArch64 object even though the larger per-source mixer loop auto-vectorizes successfully.
- The AArch64 path now processes four frames at a time with NEON structure loads/stores, vector integer-to-float conversion, the same multiply/FMA order as the old AArch64 code, truncating float-to-integer conversion, saturating `s32`-to-`s16` narrowing, and saturating accumulation into the current stereo frame. Mono, stereo, and the existing surround-as-stereo fallback all retain their prior behavior; non-AArch64 builds retain the scalar implementation.
- A focused end-to-end mixer test feeds all three buses with lane-varying values that cross both saturation limits and compares mono, stereo, and surround output with the scalar reference. It passed on the AYN Thor with three assertions covering mono, stereo, and surround.
- Exact release codegen changed from one-sample scalar loops to four-sample NEON loops. The stereo body fell from 39 instructions per sample to 20 instructions per four samples (5 per sample), while the mono body fell from 35 instructions per sample to 19 instructions per four samples (4.75 per sample). The containing function shrank from 436 to 280 bytes, a 156-byte or 35.8% reduction.
- The complete `:app:assembleVanillaRelWithDebInfoLite` build passed in 5m33s. The APK is 28,945,479 bytes with SHA-256 `34549C9F41FB5B6D773E40DC8DBD26E9B22D5DA210453915F1358873E2A067B2`.
- The same machine-code audit rejected several tempting false positives: Crypto++ already compiles Rijndael with `-march=armv8-a+crypto`; SoundTouch ships in 16-bit integer mode and its correlation loop already auto-vectorizes to NEON; and the per-source 24-channel HLE mixer already becomes an eight-frame NEON loop. Crypto acceleration can improve encrypted content and service latency, but it is not currently evidence of a sustained FPS or wattage win.

## 2026-08-16 AArch64 PICA Tile Codec

- `MortonCopyTile()` sits in every rasterizer-cache texture upload and download. Exact release AArch64 codegen proved that a full 8x8 RGBA8 tile was still expanded into 64 scalar loads, 64 scalar stores, and, for Vulkan's required RGBA conversion, 64 scalar byte reversals.
- The AArch64 path now maps the PICA 2x2/4x4/8x8 Morton structure directly onto NEON structured loads and stores. RGBA8 upload uses eight `LD2` operations to deinterleave pairs of rows, sixteen vector `REV32` operations when Vulkan needs component reversal, and eight paired 256-bit row stores. That replaces 192 scalar load/reverse/store operations with 32 vector memory/shuffle operations per full converted tile, an 83.3% reduction in those core operations. The reverse download path uses the corresponding `ST2` interleave. Native RGB5A1, RGB565, RGBA4, and D16 tiles use the same approach at 16 bits per pixel.
- The always-expanded PICA texture formats were a second scalar gap. IA8, RG8, I8, A8, and IA4 now combine Morton deinterleave with NEON `ST4` RGBA expansion during uploads instead of processing and storing one pixel at a time. For I8, the old exact AArch64 tile function ran a 53-instruction row body eight times; the ThinLTO full-tile vector body is 54 instructions total, an 87.3% reduction in that hot body. IA4 performs both nibble replications in vector lanes.
- The optimization is gated to AArch64 and only to formats whose old behavior can be represented exactly: RGBA8 with or without its existing whole-pixel byte reversal, non-converted raw 16-bit formats, and the five upload-only expansion formats above. RGB8/D24 packing, D24S8 rotation, ETC decoding, I4/A4 unpacking, and expanded-format downloads stay on their existing scalar paths.
- A focused test covers both Morton-to-linear upload and linear-to-Morton download, a deliberately padded ten-pixel stride, native and converted RGBA8, all three native 16-bit color formats, D16, and exact IA8/RG8/I8/A8/IA4 expansion. It compares against byte-wise scalar Morton references and verifies exact raw-format round trips. It passed on the AYN Thor with 17 assertions in one test case.
- The latest `:app:assembleVanillaRelWithDebInfoLite` passed in 2m16s. ThinLTO preserved the intended `LD2`, `UZP`, vector `REV32`, `ST2`, paired-store, and `ST4` sequences in `libcitra-android.so`. The APK is 28,945,287 bytes with SHA-256 `4B8A57E3ECD8754389F6CA568E53BF463F1C0888D94B40BDB001ACA52B8B17B6`.
- This is a static work and instruction-count win, not yet a claimed FPS or wattage result. Texture-cache effectiveness makes the on-device impact title- and scene-dependent; the matched Thor A/B must include texture-streaming scenes and render-target readbacks as well as steady-state gameplay.
- The verified APK installed successfully as `org.azahar_emu.azahar.debug`, launched into `MainActivity`, and remained running. Temporary stripped test runners were deleted from both the PC temp directory and `/data/local/tmp` after the checks.

## 2026-08-16 Vulkan Anime4K Repair

- The old Vulkan path did not implement Anime4K. It bound one surface as all three shader inputs and ran only the final refine shader while rendering back into that same image. That omitted both gradient passes and created an invalid sample/render feedback dependency.
- Vulkan now copies the requested unscaled source rectangle to an independent image, renders the X gradient to RG16F at 2x, renders the Y/luma gradient to R16F at 2x, and uses those results for the final refine pass into the scaled surface. The passes use independent framebuffers, cached format-compatible pipelines, clamp-to-edge samplers, and explicit `GENERAL`-layout dependencies between transfer writes, color writes, fragment reads, and the final surface use.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and `:app:assembleVanillaRelWithDebInfoLite` both passed. The resulting APK installed on the AYN Thor and loaded 7TH DRAGON III CODE: VFD at 3x resolution with Vulkan, Turnip Adreno 740, and Anime4K explicitly reported in the native configuration log.
- A fixed title-screen capture rendered at 30 FPS with no fatal, pipeline-creation, Vulkan, or shader error. Its expected Anime4K edge refinement matched a control capture from the existing OpenGL Anime4K path; screenshots and device logs were test artifacts and are not committed.
- This restores correctness, not efficiency. Anime4K performs three full-screen texture passes plus a source copy for each filtered upload and keeps intermediate images by source size/format for safe reuse across queued Vulkan work. It is labeled very heavy in the Android UI. None or Bicubic remains the better Thor choice when lower GPU load, memory use, and battery power matter more than aggressive anime-line refinement.

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
