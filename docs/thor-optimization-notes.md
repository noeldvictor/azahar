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
- Fetched the later `upstream/master` tip `3392c56ce`. Its two new commits only revise the
  MSVC workaround in `src/core/hle/service/service.{h,cpp}`; they were applied narrowly as
  `44b30dc92` and `5f3b01a9f` so the divergent Thor fork could not replace fork-only files.
  Both service files match the fetched upstream tip exactly.
- Mirrored the directly relevant sibling-project research under [`research/`](research/README.md), including the chapter-by-chapter notes for Whatcookie's "PS3 emulation is fast on ARM now" video and the follow-up "what didn't make the cut" article.
- Kept the new PICA command-list lookup, batching, and four-command vectorizable path from `c688076ac`. This is directly relevant to ARM host CPU time and energy because it reduces command-dispatch overhead without changing guest semantics.
- Kept the shader output register-banking work from `74d38ddcc`, including its A64 shader-JIT changes.
- Corrected the resource-tick comparison introduced by `b34de55b5`. A sentenced surface is retained while the runtime's completed tick is equal to or older than its retirement tick and is deleted only after the completed tick advances beyond it. The upstream comparison did the opposite and could retain sentenced surfaces indefinitely.

### RPCS3 ideas deliberately not copied

- **RPCS3 timer-scaled `busy_wait`:** [RPCS3 #18055](https://github.com/RPCS3/rpcs3/pull/18055) fixed waits that treated a low-frequency ARM generic timer like a multi-GHz x86 cycle counter. Azahar has no equivalent host-timer-calibrated busy-wait utility in its active CPU, audio, or Vulkan paths, so there is no constant to copy. Adding a second correction would repeat the kind of double-calibration regression already seen in related ARM ports.
- **ISB-based spin waiting:** Azahar has no equivalent hot emulator/render spin loop. Its Vulkan scheduler and master semaphore block on condition variables, Vulkan fences, or timeline semaphores, while Dynarmic's ARM64 lock already uses `SEVL`/`WFE`. [RPCS3 #18151](https://github.com/RPCS3/rpcs3/pull/18151) was a small improvement over ineffective ARM `yield`, and [RPCS3 #18830](https://github.com/RPCS3/rpcs3/pull/18830) later confirmed that hardware waits are the better primitive but did not measure an application-level power win at its first call sites.
- **Single-instruction `FMAX`/`FMIN`:** PICA's asymmetric NaN behavior differs from the PPC operation RPCS3 optimized. Azahar's A64 shader JIT intentionally uses `FCMGT` plus `BIF` to preserve PICA results.
- **SPU checksum, SHUFB, SHA3, and dot-product paths:** these target PS3 SPU/PPC workloads and have no direct 3DS guest equivalent. [RPCS3 #18056](https://github.com/RPCS3/rpcs3/pull/18056) is still useful as a method: express the guest permutation directly with native ARM vector operations and verify final codegen. This review found that Azahar's A64 PICA source-swizzle fallback still used a vector copy plus serial lane inserts; the AArch64 shader-swizzle change below closes that specific gap. Its A64/x64 JIT compiler method sets remain equal at 44 methods each.
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
- That first optimization was gated to AArch64 and only to formats whose old behavior could be represented exactly: RGBA8 with or without its existing whole-pixel byte reversal, non-converted raw 16-bit formats, and the five upload-only expansion formats above. It deliberately left RGB8/D24 packing, D24S8 rotation, ETC decoding, I4/A4 unpacking, and expanded-format downloads on their scalar paths; the separate RGB8/D24 follow-up below closes the safe 24-bit part of that gap.
- A focused test covers both Morton-to-linear upload and linear-to-Morton download, a deliberately padded ten-pixel stride, native and converted RGBA8, all three native 16-bit color formats, D16, and exact IA8/RG8/I8/A8/IA4 expansion. It compares against byte-wise scalar Morton references and verifies exact raw-format round trips. It passed on the AYN Thor with 17 assertions in one test case.
- The latest `:app:assembleVanillaRelWithDebInfoLite` passed in 2m16s. ThinLTO preserved the intended `LD2`, `UZP`, vector `REV32`, `ST2`, paired-store, and `ST4` sequences in `libcitra-android.so`. The APK is 28,945,287 bytes with SHA-256 `4B8A57E3ECD8754389F6CA568E53BF463F1C0888D94B40BDB001ACA52B8B17B6`.
- This is a static work and instruction-count win, not yet a claimed FPS or wattage result. Texture-cache effectiveness makes the on-device impact title- and scene-dependent; the matched Thor A/B must include texture-streaming scenes and render-target readbacks as well as steady-state gameplay.
- The verified APK installed successfully as `org.azahar_emu.azahar.debug`, launched into `MainActivity`, and remained running. Temporary stripped test runners were deleted from both the PC temp directory and `/data/local/tmp` after the checks.

## 2026-08-16 AArch64 RGB8/D24 Tile Codec

- RGB8 and raw D24 were the remaining byte-packed full-tile formats in the scalar 64-pixel
  `MortonCopyTile()` loop. Vulkan cannot use RGB8 as an attachment and selects a converted RGBA8
  host surface, so RGB8 render-target traffic specifically paid both the Morton walk and scalar
  BGR-to-RGBA expansion. D24-to-float conversion keeps its existing scalar path because changing
  its normalization or rounding would not be byte-equivalent; raw D24 copies are safe to vectorize.
- The AArch64 path now loads each pair of Morton chunks with `LD3`, combines their component
  lanes, and uses one-table `TBL` permutations to recover the two linear rows. Native RGB8/D24
  writes use `ST3`. Converted RGB8 uses explicit `ZIP1`/`ZIP2` interleaving and ordinary paired
  32-byte stores, preserving the exact BGR-to-RGBA channel order and opaque alpha without a
  per-pixel loop. Downloads apply the inverse compile-time-checked permutation.
- This store choice came from the actual Thor core manuals indexed under
  [`hardware/`](hardware/README.md), not an x86 analogy. The X3 and A710 tables list D-form `ST4`
  throughput at one instruction per three cycles, while the A510 lists one per 25 cycles; the A715
  is much stronger at one per cycle. The portable `ZIP` plus paired-store sequence avoids the
  extreme efficiency-core cliff and remains vectorized on every core, but the A715 tradeoff is why
  this is not yet presented as a measured whole-device win.
- Focused tests add native RGB8 and D24 round trips plus converted RGB8 with a padded ten-pixel
  stride. The converted reference independently computes every Morton offset, checks BGR-to-RGBA
  order and alpha `0xFF`, and then verifies an exact inverse round trip. The ARM64 test executable
  linked successfully; per the no-device restriction it was not run.
- Final ThinLTO codegen contains the intended `LD3`, `TBL`, `ZIP`, `STP`, `LD4`, and `ST3`
  instructions. The common converted RGB8 upload symbol fell from 588 to 404 bytes (31.3%), native
  RGB8 upload from 552 to 372 bytes (32.6%), and raw D24 upload from 556 to 376 bytes (32.4%). The
  download wrappers grew by 220 bytes because partial-tile staging duplicates the inlined inverse
  body; full aligned tiles still take the vector path. Avoiding a call in the hot full-tile loop was
  kept as the better theoretical tradeoff pending profiles.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` both passed. The APK is 28,963,931 bytes with SHA-256
  `0349A65F8604ECB6A495F1A645CEE6915CAAF17E0343535FEE5E5FBECE38746A`. Only the active
  `arm64-v8a` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or Thor execution
  was performed.
- This is a source, machine-code, and manual-supported instruction reduction, not an FPS or wattage
  claim. A future allowed A/B should target RGB8 render-target churn or readback-heavy scenes with
  fixed title, scene, caches, renderer, resolution, layout, driver, display state, performance/fan
  mode, and run time while recording CPU time, frametimes, battery power, temperature, and thermal
  slope.

## 2026-08-16 AArch64 D24S8 Tile Codec

- D24S8 was the last 32-bit full-tile format still using the scalar 64-pixel Morton loop. Each
  upload pixel performed a scalar load, an eight-bit rotate, and a scalar store; the inverse
  download repeated the same pattern in the other direction.
- The AArch64 path now reuses the exact RGBA8 `LD2`/paired-store Morton geometry and applies one
  `TBL` permutation per 16-byte vector. Upload maps each four source bytes from
  `[b0, b1, b2, b3]` to `[b3, b0, b1, b2]`, matching the old `rotl(u32, 8)`. Download applies
  `[b1, b2, b3, b0]`, matching `rotr(u32, 8)`. A compile-time composition check proves that the
  two table permutations are inverses.
- Final ThinLTO upload code contains eight `LD2`, sixteen one-table `TBL`, and eight paired vector
  stores per full 8x8 tile. That is 32 core load/shuffle/store operations in place of 64 scalar
  loads, 64 rotates, and 64 scalar stores: an 83.3% reduction in that core tile body. The full
  aligned download path is vectorized with the inverse table as well.
- The optimized upload symbol grew from 408 to 440 bytes (7.8%). The download wrapper grew from
  592 to 1,116 bytes because ThinLTO duplicated the inlined vector body into partial-tile staging
  paths. The larger wrapper is an explicit tradeoff for avoiding a call in the full-tile hot loop;
  runtime profiling can revisit it if instruction-cache pressure outweighs the reduced tile work.
- A focused test independently computes the scalar Morton layout with a padded ten-pixel stride,
  verifies the exact D24S8 byte rotation on upload, and verifies an inverse round trip on download.
  The ARM64 test executable compiled and linked, but was not run because the current restriction
  forbids using the Thor.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` both passed. The APK is 28,963,743 bytes with SHA-256
  `D3D20220D444185398929E8BC247F54E22B7E77B58D73C3E85B05DCB2DE14C23`. Only the active
  `arm64-v8a` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or Thor execution
  was performed.
- This is a major instruction-count reduction in one texture conversion hot path, not evidence of
  a major whole-emulator FPS or wattage gain. A future allowed matched A/B should target titles and
  scenes with frequent D24S8 depth-stencil upload, readback, or render-target churn and record the
  complete correctness, frametime, power, and thermal acceptance set.

## 2026-08-16 ETC1 Block Decoder

- ETC1 and ETC1A4 were the most expensive remaining texture-upload scalar paths. The old 8x8
  tile loop called the 356-byte `SampleETC1Subtile()` function once for every pixel: 64 calls per
  tile. Each call re-extracted differential/separate base colors, table indices, and flip state
  from the same 64-bit 4x4 block, so those block-invariant calculations were repeated 16 times.
- Uploads now decode the four 4x4 blocks directly. Each block computes both base colors and its
  modifier-table indices once, then reuses them for all 16 pixels. The unchanged per-pixel
  sampler remains available for individual software texture lookups and as an independent
  correctness oracle.
  Final ARM64 ThinLTO code contains exactly four block-decoder calls per full tile and no calls back
  to the old per-pixel sampler.
- The ETC1 upload wrapper shrank from 688 to 408 bytes (40.7%); the ETC1A4 wrapper shrank from 448
  to 396 bytes (11.6%). More importantly, both changed from 64 decoder calls to four. The new block
  helpers are 312 bytes for ETC1 and 348 bytes for ETC1A4, while the original 356-byte sampler is
  unchanged. This is an algorithmic invariant-hoisting win on every host architecture, including
  the Thor's AArch64 cores, rather than a guest-semantics shortcut.
- Temporary host differential harnesses checked 100,000 arbitrary raw blocks in both ETC1 and
  ETC1A4 modes (1.6 million pixels per format) and then 10,000 complete padded-stride tiles per
  format. Every byte matched the old sampler, including all flip/differential combinations,
  modifier/sign bits, clamping, ETC1A4 alpha nibble order, subblock placement, and bottom-up output
  rows. The temporary executables and sources were deleted afterward. A permanent focused Catch2
  test covers the same layout and all four mode combinations; the ARM64 test executable compiled
  and linked but was not run because the current restriction forbids using the Thor.
- A direct Vulkan ETC2 compressed-image route was also evaluated. It could eventually avoid CPU
  decompression and reduce texture memory traffic substantially on Adreno, but the current surface
  cache requires common transfer, attachment, and blit behavior and maps PICA compressed formats
  to RGBA8. Guest block orientation, partial updates, copies, and mip generation need a separate
  sampled-only surface design plus device correctness testing, so that larger route was not enabled
  blindly.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` both passed. The APK is 28,965,083 bytes with SHA-256
  `A5FDA902BF284313BD710CB3527C2F4F4B6240BEA143694F3E817468A871A75F`. Only the active
  `arm64-v8a` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or Thor execution
  was performed.
- This proves a large reduction in repeated ETC decode work, not a whole-game FPS or wattage
  result. A future allowed matched A/B should use ETC-heavy texture-streaming scenes and record
  texture upload time, frametimes, battery power, temperature, and visual correctness.

## 2026-08-16 AArch64 I4/A4 Tile Expansion

- I4 and A4 uploads previously expanded every 8x8 Morton tile through the scalar per-pixel path.
  A full tile issued 64 byte loads and 192 scalar stores: 256 memory instructions before surrounding
  loop and address work.
- The AArch64 path now processes two rows together. `UZP` recovers the two Morton rows, `SLI`
  replicates each four-bit intensity or alpha value to eight bits, and the existing ZIP/STP RGBA
  store sequence writes both rows. This preserves the required low-nibble-first pixel order and
  deliberately avoids interleaved `ST4` stores on the Thor's X3/A710/A510 core mix.
- A full tile now uses eight word loads and eight paired vector stores: 16 memory instructions,
  93.75% fewer than the old 256. ARM64 ThinLTO code also shrank the I4 wrapper from 616 to 376 bytes
  (39.0%) and the A4 wrapper from 584 to 364 bytes (37.7%). Generated code contains the intended
  `UZP`, `USHR`, `SLI`, `ZIP`, and `STP` instructions and no `ST4`.
- An independent model checked 10,000 arbitrary 32-byte tiles against scalar Morton/nibble
  expansion with exact results. A permanent focused Catch2 test covers both formats and padded
  output stride; the ARM64 test executable compiled and linked but was not run because the current
  restriction forbids using the Thor.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` both passed. The APK is 28,963,431 bytes with SHA-256
  `3B942483933BC86B845DCB15706A1ED011C47433BC6F6BDBF99A634F2E83F586`. Only the active
  `arm64-v8a` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or Thor execution
  was performed.
- This proves a major reduction in I4/A4 conversion work, not a whole-game FPS or wattage result.
  A future allowed matched A/B should target I4/A4-heavy texture uploads and compare upload time,
  frametimes, battery power, temperature, and visual correctness.

## 2026-08-16 AArch64 Linear RGBA8/RGB8 Conversion

- The tiled texture paths had gained explicit NEON coverage, but converted linear surfaces still
  used the old per-pixel codec. Final ARM64 ThinLTO output showed RGBA8 uploads doing one scalar
  load, byte reverse, and store per pixel. RGBA8 downloads were auto-vectorized into twelve shifts,
  four table lookups, and an interleaved `ST4` per 16 pixels. Both RGB8 directions remained scalar.
- Converted linear RGBA8 now handles 16 pixels with two paired 128-bit loads, four `REV32`
  operations, and two paired stores. RGB8 uses a compile-time-verified four-register `TBL` mapping:
  uploads turn 48 packed BGR bytes into 64 RGBA bytes with opaque alpha, while downloads remove
  alpha and restore packed BGR order. Buffers shorter than 16 pixels and final partial blocks retain
  the scalar oracle path.
- In the steady 16-pixel loop bodies visible in final ThinLTO code, RGBA8 upload fell from about 96
  instructions to 14 (85.4%), and RGBA8 download from 21 to 14 (33.3%) while eliminating `ST4`.
  RGB8 upload fell from about 208 instructions to 13 (93.75%), and RGB8 download from about 208 to
  12 (94.2%). The RGBA8 download wrapper also shrank from 372 to 148 bytes. Across all four wrappers
  the explicit vector loops add only 124 bytes because the other three now carry both a vector loop
  and scalar tail.
- An independent shuffle model checked 100,000 arbitrary 16-pixel RGB8 and RGBA8 blocks: 1.6
  million pixels per direction matched exact scalar decode, alpha insertion, byte reversal, and
  round-trip packing. Permanent Catch2 tests use 37 pixels to cover two vector blocks, a five-pixel
  scalar tail, and deliberately incomplete final source/destination bytes. The ARM64 tests compiled
  and linked but were not executed because the current restriction forbids using the Thor.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` both passed. The APK is 28,963,935 bytes with SHA-256
  `EC094D00FAB9D6556F4AE68BA9367A49055A341CC56049EC470107380CD2651C`. Only the active
  `arm64-v8a` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or Thor execution
  was performed.
- This proves a large CPU instruction reduction whenever a game uploads or reads back converted
  linear RGBA8/RGB8 surfaces. It is not yet a whole-game FPS or wattage result; a future allowed
  matched A/B should target linear-surface-heavy scenes and record conversion time, frametimes,
  battery power, temperature, and visual correctness.

## 2026-08-16 AArch64 Shader JIT Entry/Exit Traffic

- The PICA AArch64 shader JIT previously saved all twelve ABI callee-saved GPRs and all eight
  callee-saved vector registers on every shader invocation. The emitted shader only assigned two
  vector registers from that set (`Q14` and `Q15`) plus the link register. Including stack
  allocation and the unconditional dummy return slot, the fixed entry/exit path emitted 26
  instructions, performed 20 register memory operations, moved 448 register bytes, and reserved
  256 stack bytes even for a leaf shader.
- The constant and final vector scratch register now use free caller-saved `Q5` and `Q6`. The
  persistent-register mask automatically saves the full `Q5` around the rare external geometry
  callback, which also avoids relying on AAPCS64's guarantee for only the low 64 bits of
  callee-saved vector registers. A complete symbolic-register audit found no remaining generated
  use of `X19`-`X29` or `Q8`-`Q15`.
- Shader bytecode is scanned before emission. Ordinary leaf shaders now have no entry/exit stack
  frame at all: the fixed 26 instructions, 20 memory operations, 448 register bytes, and 256-byte
  frame all fall to zero. A shader containing `EX2` or `LG2` preserves only `X30` once, producing
  four fixed entry/exit instructions and 16 bytes of register traffic. With one math-helper call,
  removing its old local `X30` spill reduces the relevant overhead from 28 instructions to four
  (85.7%).
- Shaders containing PICA `CALL`, `CALLC`, or `CALLU` preserve `X30` and retain the 16-byte dummy
  return frame. Their fixed entry/exit/dummy sequence falls from 26 to eight instructions while
  keeping the old per-math-helper link-register spill, because a helper may execute inside a guest
  subroutine. External `EMIT`/error callbacks retain their existing caller-save wrapper.
- The existing Catch2 shader cases cover direct `LG2`, direct `EX2`, and a guest `CALL` whose
  subroutine executes `EX2`, so both link-register strategies compiled and linked into the ARM64
  test executable. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` passed. The APK is 28,964,819 bytes with SHA-256
  `FD5A41A44EE6C7796FCAB0CB448FD222794D20BEB84B913EFCFA0998E4A91DFE`; it contains only
  `arm64-v8a` native libraries. Only the active `6t472v1d` RelWithDebInfo CMake hash remains.
- No ADB command, install, app launch, or Thor execution was performed. The emitted-code reduction
  is exact, but whole-game FPS, battery power, temperature, and sustained wattage remain unmeasured
  until a future allowed matched scene A/B.

## 2026-08-16 AArch64 Shader JIT State Traffic

- Even after removing the oversized ABI frame, every compiled shader invocation still loaded all
  three PICA address/loop registers and both condition flags, then wrote all five values back at
  every `END`. That was four load instructions plus four stores and 28 bytes of state traffic even
  when the shader never referenced or modified any of that state. The uniform-base move and
  all-ones vector initialization were also unconditional.
- The existing whole-program control-flow scan now records exact conservative access sets before
  emitting code. Relative float-uniform operands mark only their selected `a0.x`, `a0.y`, or `aL`
  register; enabled `MOVA` lanes mark their corresponding writes; `LOOP` marks `aL`; conditional
  flow marks only the condition lanes selected by `JustX`, `JustY`, `Or`, or `And`; and `CMP` marks
  both condition writes. Float-uniform operands and uniform flow mark the uniform base, while only
  relative-address fallback, DPH/SGE/SLT/RCP/RSQ, and LG2 mark the `ONE` constant.
- Any register that might be written is preloaded as well as written back. This intentionally keeps
  the original value if runtime control flow skips the write or execution enters at a later shader
  offset. Unreferenced state stays in `ShaderUnit` memory and is never transferred. The additional
  analysis runs once when compiling a shader, while the removed instructions ran for every vertex
  or geometry shader invocation.
- A simple uniform-free `MOV` shader now removes ten emitted instructions per invocation: all eight
  state memory operations, the dead uniform-base move, and the unused `ONE` initialization. Its 28
  bytes of PICA state traffic fall to zero. A read-only `a0.x` shader emits one state load instead of
  eight transfers (87.5% fewer); an x-only `MOVA` emits one load and one store (75% fewer); an aL
  loop emits one load and one store; and a condition-only `JustX` path emits one byte load and no
  condition store. Shaders using every state category conservatively retain the old traffic.
- The same access map narrows caller-save wrappers around geometry `EMIT` and error callbacks. A
  shader with no address, condition, loop, uniform, or `ONE` dependency now preserves only `STATE`
  and `X30`: wrapper memory operations fall from ten to two (80%), register traffic from 144 to 32
  bytes (77.8%), emitted save/restore instructions from twelve to four (66.7%), and stack use from
  80 to 16 bytes. State-heavy geometry shaders keep every register they can actually need.
- Focused Catch2 coverage now checks unused-state preservation, partial `MOVA` preservation,
  initial-state relative uniform reads, and `CMP` writeback. Existing conditional-flow and nested
  loop cases cover condition reads and aL persistence. The complete ARM64 test executable compiled
  and linked, but was not run under the current no-Thor restriction.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and
  `:app:assembleVanillaRelWithDebInfoLite` passed. The arm64-only APK is 28,964,975 bytes with
  SHA-256 `5341AA99FBFABF37F301FA7651F529F06B32BB295325BE029F0A73A8A5E3A0FE`.
  Only the active `6t472v1d` RelWithDebInfo CMake hash remains. No ADB command, install, launch, or
  Thor execution was performed, so whole-game FPS and wattage remain unmeasured.

## 2026-08-16 Vulkan Anime4K Repair

- The old Vulkan path did not implement Anime4K. It bound one surface as all three shader inputs and ran only the final refine shader while rendering back into that same image. That omitted both gradient passes and created an invalid sample/render feedback dependency.
- Vulkan now copies the requested unscaled source rectangle to an independent image, renders the X gradient to RG16F at 2x, renders the Y/luma gradient to R16F at 2x, and uses those results for the final refine pass into the scaled surface. The passes use independent framebuffers, cached format-compatible pipelines, clamp-to-edge samplers, and explicit `GENERAL`-layout dependencies between transfer writes, color writes, fragment reads, and the final surface use.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` and `:app:assembleVanillaRelWithDebInfoLite` both passed. The resulting APK installed on the AYN Thor and loaded 7TH DRAGON III CODE: VFD at 3x resolution with Vulkan, Turnip Adreno 740, and Anime4K explicitly reported in the native configuration log.
- A fixed title-screen capture rendered at 30 FPS with no fatal, pipeline-creation, Vulkan, or shader error. Its expected Anime4K edge refinement matched a control capture from the existing OpenGL Anime4K path; screenshots and device logs were test artifacts and are not committed.
- This restores correctness, not efficiency. Anime4K performs three full-screen texture passes plus a source copy for each filtered upload and keeps intermediate images by source size/format for safe reuse across queued Vulkan work. It is labeled very heavy in the Android UI. None or Bicubic remains the better Thor choice when lower GPU load, memory use, and battery power matter more than aggressive anime-line refinement.

## 2026-08-16 Anime4K v4 Mobile Screen Filter

- Android Graphics settings now has a separate **Screen Filter** selector beside Linear Filtering.
  It is distinct from **Texture Filter**: the old filter changes individual emulated textures before
  games use them, while the new mode filters each finished 3DS screen as it is scaled into the
  final layout. The default is None, so existing output and GPU cost do not change unless selected.
- **Anime4K v4 Mobile** is derived from the MIT-licensed non-CNN DoG upscaler shipped in the
  official [Anime4K v4.0.1 release](https://github.com/bloc97/Anime4K/tree/v4.0.1/glsl/Upscale).
  The desktop-oriented implementation uses luma, horizontal Gaussian, vertical Gaussian, and
  apply passes. This Thor port fuses the DoG idea into one presentation pass with a normalized 3x3
  Gaussian, the same 0.8 luma correction strength, and a local luma min/max clamp. It samples nine
  source texels per output fragment but creates no intermediate image and performs no extra
  full-frame read/write pass, reducing mobile tile-memory bandwidth at the cost of not being a
  pixel-identical port of the full separable filter or its multi-pass CNN alternatives.
- The shader activates only when both output axes exceed the input by 1.2x, matching Anime4K's
  upscale-only intent. It preserves source alpha, clamps corrected RGB, forces the required linear
  sampler only while the screen filter is active, and leaves normal nearest/linear presentation
  alone at native size or while downscaling. Anaglyph and interlaced 3D retain their existing
  two-eye shaders and the user's ordinary presentation sampler.
- Vulkan has a dedicated fourth presentation pipeline and supports both dynamic descriptor-array
  indexing and the existing switch fallback. OpenGL has a matching built-in presentation shader.
  The setting is persisted through Android and desktop configuration and is logged by name.
- NDK `glslc` accepted both Vulkan indexing variants and the OpenGL fragment shader. The complete
  ARM64 `:app:assembleVanillaRelWithDebInfoLite` build passed; the APK is 28,961,939 bytes with
  SHA-256 `4C7979B6A8AB5A6A725AF9EE07536A7D5172BB809286112E093F51F8EA58E543`.
- Per the active no-launch restriction, the APK was not installed or run. Visual quality, Adreno
  frametime, and power remain unmeasured. The required A/B is None versus Anime4K v4 Mobile in the
  same anime-heavy title and fixed scene, with identical renderer, internal resolution, layout,
  driver, brightness, fan/performance mode, cache state, and duration; record frame distribution,
  KGSL busy time, battery power, temperature, and thermal slope before changing recommendations.

## 2026-08-16 Dynarmic A32 ARM64 FastDispatch

- The ARM64 A32 backend previously sent every `FastDispatchHint` terminal back through
  the C++ dispatcher even though the x64 backend had a native two-tier dispatch cache.
  ARM64 now checks a 65,536-entry direct-mapped table in generated AArch64 code and calls
  `GetOrEmit()` only on a descriptor miss. Return-stack-buffer misses use the same fast
  path when the optimization is enabled.
- The descriptor is loaded directly from the adjacent A32 PC and upper-descriptor state,
  and the emitted hash exactly matches the C++ invalidation hash. Range invalidation
  clears a matching entry; full cache clears discard the table and block-range map at the
  same explicit virtual cache-clear boundary. Single-step mode retains the slow dispatcher.
- Two focused cache tests warm a FastDispatch entry, mutate guest code, and verify that
  range invalidation and full-cache clearing cannot execute stale host code. The release
  ARM64 test binary passed all 11 assertions on the AYN Thor.
- A hidden benchmark alternates between two dynamically selected ARM blocks so constant
  propagation and ordinary block linking cannot bypass dispatch. Every other Dynarmic
  optimization is identical between the baseline and candidate. Each long sample performs
  one million indirect dispatches and the process is pinned to Thor CPU7.
- The reverse-order long run measured the C++ dispatcher at 4.014-4.016 ms per million
  dispatches and stable ARM64 FastDispatch at 2.128 ms: 1.89x dispatch throughput and
  47.0% less time in this dispatcher-saturated workload. Across shorter pinned/unpinned
  runs and both orderings, measured throughput ranged from 1.69x to 1.95x.
- This is not a claim of 1.89x game FPS. Game impact scales with the share of CPU time spent
  on unpredictable block dispatch; GPU-bound scenes may show no FPS change. The benchmark
  was too short for a useful battery-power comparison, so lower watts remains unproven until
  matched sustained game-scene A/B runs.
- Dynarmic and its recursive build dependencies are now vendored in this repository from
  upstream commit `e77b1ba0b7da7cbe93021b01a663acfe7c4dd516`; provenance and update rules
  are in [`research/dynarmic-vendor.md`](research/dynarmic-vendor.md).
- The final `:app:assembleVanillaRelWithDebInfoLite` build passed. The APK is 28,953,903
  bytes with SHA-256 `F85766DB96E8F820BF5C6FE945714CFA111A36082CE4B2A9028B5C41D6AD2B89`.
  It installed on the Thor and booted 7TH DRAGON III CODE: VFD through the vendored JIT
  with Vulkan, Turnip Adreno 740, and Anime4K; the launch log had no fatal signal,
  exception, or native abort.

## 2026-08-16 Android Eco Turbo

- Android fast-forward previously allowed every emulated VBlank to prepare and submit a host
  presentation. On the Thor's 120 Hz primary panel, high turbo limits can therefore run layout,
  composition, and presentation work well above the normal 3DS refresh rate even though the user
  is primarily asking the game to advance faster.
- The new General setting **Eco Turbo** defaults on. Whenever the active frame limit is above 100%,
  it caps only host presentation/composition to 60 FPS. Guest CPU execution, PICA work, audio,
  timing, and the requested turbo limit continue unchanged. Both Vulkan and OpenGL skip the
  surplus host frames; screenshots and video dumping still prepare their required framebuffers.
- The limiter uses elapsed wall time and a one-frame token budget rather than dividing by the
  requested turbo percentage. This matters when a heavy scene requests 400% but achieves less than
  100%: frames at or below 60 FPS are still all presented. Returning to normal speed or disabling
  Eco Turbo resets the budget immediately.
- The final release APK was tested on 7TH DRAGON III CODE: VFD (`000400000018F800`) at 3x,
  Vulkan, Turnip Mesa 25.99.99, Anime4K, duplicate-frame skipping enabled, and a temporary 400%
  frame limit. The title screen sustained 120 game FPS and 399-403% speed in the overlay with Eco
  both off and on.
- In the reversed-order final-code 20-second A/B, Eco off measured 32.45% KGSL GPU busy and 444
  process CPU ticks. Eco on measured 26.81% GPU busy and 409 ticks: **17.37% less GPU active time**
  and **7.9% less process CPU time** while emulation speed was retained. Both runs held the same
  615 MHz GPU frequency and 24.0 C battery temperature.
- This is not a 17.37% battery-watt claim. The device reported USB power and active charging during
  the run, so battery-current telemetry could not isolate emulator power. A long, unplugged,
  fixed-brightness/fan/performance-mode A/B is still required for watts and thermal slope.
- Benefit depends on workload. At 200% this title submits 60 game frames per second, so the existing
  duplicate-frame setting already removes surplus presentation and Eco Turbo has little extra work
  to skip. The win is larger when a title/turbo combination produces more than 60 unique frames per
  second. Disabling Eco Turbo remains available for maximum fast-forward smoothness on 120 Hz.
- `:app:assembleVanillaRelWithDebInfoLite` passed, producing a 28,957,711-byte APK with SHA-256
  `67CE6DB9E4D153899B84C54249C76E8FB009D2840FE3D4BEB849C9CD8338FF53`.
  It installed on the Thor, restored the user's original config after testing, and booted the same
  game at the original 100% limit with Eco Turbo defaulting on and no fatal exception or signal.

## 2026-08-16 Dynarmic A32 ARM64 Absolute-Offset Page Table

- Every ordinary mapped A32 guest load/store uses Dynarmic's inline page-table lookup. The old
  ARM64 sequence loaded the host page pointer and then emitted `AND guest_address, 0xfff` into a
  second scratch register before the host access. Dynarmic already supports an absolute-offset
  table whose entry is `host_page_pointer - guest_page_base`; adding the full guest address then
  reaches the same host byte and removes that `AND` plus its scratch-register dependency.
- The implementation does not allocate a duplicate page table. The existing 1,048,576-entry raw
  table stores adjusted entries on AArch64, while its C++ wrapper decodes them before any normal
  memory-system caller sees a pointer. Mapping, unmapping, rasterizer-cache transitions,
  watchpoints, and savestate reconstruction already route through that wrapper or the shared
  rebuild helper. Non-AArch64 hosts retain ordinary host pointers and Dynarmic's original mode.
- Correctness relies only on unsigned `uintptr_t` arithmetic, so encoding and decoding are exact
  modulo the host address width. Null mappings remain null. An always-on assertion rejects the one
  value Dynarmic cannot represent in this mode: a valid host page whose adjusted entry is null.
  A focused test checks the C++ pointer round trip, the exact Dynarmic entry equation, and unmapping.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 3m50s, including compilation and linking of
  the ARM64 test executable and `libcitra-android.so`. Per the active no-launch restriction, the app
  and ARM64 test executable were not run on the Thor.
- This is a generated-instruction reduction, not yet a game FPS or battery-watt result. It affects
  mapped page-table loads/stores; the page-index extraction, entry load, null check, and callback
  fallback remain. The required Thor A/B is the same fixed title/scene, caches, Vulkan driver,
  resolution, layout, brightness, fan/performance mode, and run duration, recording FPS,
  frametimes, process CPU time, battery power, temperature, and thermal slope.

## 2026-08-16 Dynarmic A32 ARM64 NZCV Register Cache

- Dynarmic's A32 ARM64 backend kept the guest ARM11 N/Z/C/V flags in
  `A32JitState::cpsr_nzcv`. A flag-producing block wrote that word to memory, while the
  next conditional block, carry consumer, or conditional select loaded it again. This
  made the architectural-state structure part of ordinary linked-block execution even
  though AArch64 has enough callee-saved registers to keep the four bits live.
- A32 now reserves callee-saved `W23` for the packed guest NZCV value. The run and step
  preludes load it once, linked blocks consume and replace it directly, and the common
  exit stores it once. The A64 frontend keeps its original memory representation and its
  complete 21-register allocator order; only A32 trades one allocator register for the
  persistent flag cache.
- Generated callback relocations and generic host-function calls store `W23` before the
  host call and reload it afterward. This preserves the old observable behavior for
  SVC, exception, coprocessor, slow-memory, timer, and hook callbacks: a callback sees
  current guest flags and may update them before guest execution resumes. `X23` is both
  compile-time-checked as AAPCS64 callee-saved and excluded from the A32 allocator.
- Exact emitted-sequence accounting for a `SUBS`/conditional-branch loop changes the
  NZCV path from `STR + LDR + MSR + B` to `MOV + MSR + B`: four instructions to three
  (25% fewer) and eight bytes of per-iteration flag-state traffic to zero. An NZ-only
  update that preserves C/V followed by a condition falls from seven instructions and
  three state-memory operations to four instructions and no state-memory operations
  (42.9% fewer instructions). A carry read falls from two instructions to one, and a
  conditional select falls from three instructions to two.
- The entry/exit cost is one four-byte load and one four-byte store per `Run()` or
  `Step()`, rather than per guest block. Host callbacks deliberately add a store/load
  pair for coherence. The new hidden `SUBS`/`BNE` benchmark makes the register-pressure
  tradeoff and the removed cross-block traffic repeatable against the parent revision;
  it must be measured before deciding whether the reserved register is a net win in
  real game code.
- The focused regression compiles a sequence that sets Z/C, enters SVC, verifies the
  callback sees those flags, replaces them with N, and then verifies subsequent MI/EQ
  guest conditions use the callback's replacement. The complete ARM64 Dynarmic test
  executable and `libcitra-android.so` compiled and linked. The release-style
  `:app:assembleVanillaRelWithDebInfoLite` build also passed; its 28,965,311-byte APK
  contains only `arm64-v8a` native libraries and has SHA-256
  `09F52B9EC343F62F9E8B3E0EB04402C3537741E73335E7117285D58848F13728`. Per the active
  no-device restriction, neither executable was run on the Thor.
- This is a broad generated-code and memory-traffic reduction, not yet an emulator FPS
  or battery-watt result. A future allowed A/B should compare the parent and candidate
  revisions with the focused benchmark plus identical game scenes, and must capture
  frametimes, process CPU time, battery power, temperature, thermal slope, and visual
  correctness.

## 2026-08-16 Dynarmic A32 ARM64 Conditions and Cycle Checks

- A cycle-counted linked A32 block previously ended with `SUB Xticks`, `CMP Xticks, #0`,
  and `B.LE`. The subtraction now sets host flags with `SUBS`, and an A32 `LinkBlock`
  reuses those flags when the cycle subtraction produced them. The ordinary linked-block
  budget check therefore falls from three instructions to two (33.3% fewer). Zero-cycle,
  cycle-counting-disabled, single-step, and non-link paths keep their existing safe behavior.
- A32 guest conditions no longer copy packed guest flags into the host NZCV system register.
  EQ/NE, CS/CC, MI/PL, and VS/VC become one exact `TBZ`/`TBNZ` instead of `MSR` plus a
  conditional branch: two instructions to one (50% fewer) and no system-register write.
  HI/LS and GE/LT remain two instructions but avoid `MSR`; GT/LE need three flag-preserving
  instructions, yet do not add instructions to the combined condition-plus-cycle terminal
  because the separate cycle `CMP` is gone.
- Combining the two changes takes a common simple conditional linked-block path from five
  ARM64 control/cycle instructions (`MSR + B.cond + SUB + CMP + B.LE`) to three
  (`TBZ/TBNZ + SUBS + B.LE`), a 40% static reduction. The condition sequences use only
  flag-preserving bit-test branches and non-flag-setting `EOR`, so the signed `Xticks <= 0`
  result remains valid until the link decision.
- A focused regression covers all 16 N/Z/C/V combinations against all 14 meaningful ARM
  conditions and places an unconditional guard immediately after the exact cycle boundary.
  The source passes an Android ARM64 Clang syntax check. The standalone Dynarmic test runner
  was not executed because the current restriction forbids using the Thor; an auxiliary CMake
  attempt to enable that runner in the Android app tree was stopped after its cross-test
  configuration hung, and `DYNARMIC_TESTS` was restored to `OFF`.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled and linked the modified ARM64 backend and
  `libcitra-android.so` successfully in 1m13s. After the two current Azahar master updates were
  integrated, `:app:assembleVanillaRelWithDebInfoLite` also passed in 3m28s. The resulting
  28,965,611-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `BF5E20ABCEE5653CEE82289AE36B97FC1886A08B3338AF3625E4B9C0A92AA124`. No ADB command,
  install, launch, or device test was performed.
- These are pervasive generated-instruction reductions, but they are not additive with the
  earlier FastDispatch, page-table, or NZCV-cache percentages. Whole-game FPS, battery watts,
  and thermal-slope effects remain unmeasured until a controlled parent-versus-candidate Thor
  A/B is allowed.

## 2026-08-16 AArch64 PICA Shader Swizzles

- The PICA AArch64 vertex-shader JIT's arbitrary source-swizzle fallback previously copied the
  full vector to a scratch register and then inserted each changed 32-bit lane separately. That
  executed two instructions for a one-lane change and three to five instructions when two to four
  lanes changed.
- Identity and four broadcast selectors retain their existing zero- and one-instruction fast
  paths. A selector with exactly one changed lane now emits one direct lane move, a 50% reduction
  from two instructions. Selectors changing two or more lanes load a 16-byte byte-index literal
  and execute one baseline AdvSIMD `TBL`, reducing the old three-to-five-instruction sequence to
  two instructions (33.3%-60% fewer executed shuffle instructions).
- The byte-index literal is aligned and deduplicated by raw selector within each compiled shader.
  Sparse label bookkeeping is cleared after literal emission instead of remaining in every cached
  shader object. Compile-time assertions verify the identity and reverse mappings. The focused
  regression builds a MOV shader for every one of the 256 selector encodings and checks all four
  result lanes, for 1,024 lane assertions.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` completed from a full native rebuild in 10m37s. After
  replacing the dense persistent label array with temporary sparse bookkeeping, an incremental
  1m09s build recompiled the modified AArch64 JIT and exhaustive test source and relinked both the
  ARM64 test runner and `libcitra-android.so`. The test runner was not executed because the active
  restriction forbids using the Thor and the binary cannot run on the x64 host.
- The first release-style APK attempt hit a transient Windows lock on R8's generated `classes.dex`.
  After stopping Gradle daemons, final no-daemon packaging completed in 1m02s. The resulting
  28,966,207-byte APK contains only `arm64-v8a` native libraries and has SHA-256
  `3315A6500AB273EB7EFA744F5809BD40BB20A677FDF56B1C6D1AB88386F18381`.
- This is an exact generated-instruction reduction, not a whole-game FPS or battery-watt claim.
  The two-instruction path trades serial lane operations for a literal load, and each unique
  multi-lane selector adds 16 bytes to the shader code pool. A future allowed matched A/B should
  profile vertex-heavy scenes with identical title, driver, resolution, layout, cache state,
  performance mode, brightness, and duration while recording shader time, frametimes, process CPU
  time, battery power, temperature, thermal slope, and visual correctness.

## 2026-08-16 AArch64 PICA Partial Destination Stores

- The AArch64 PICA vertex-shader JIT previously implemented every partial x/y/z/w destination
  mask as a read-modify-write: load the old 16-byte vector, materialize a mask, select enabled
  source lanes with `BSL`, and store all 16 bytes. Disabled components were preserved correctly,
  but the sequence created a needless load-to-store dependency and touched 32 bytes of explicit
  generated memory traffic for a write that may change only one 4-byte component.
- The replacement groups enabled x/y and z/w pairs and emits baseline AdvSIMD `ST1` element
  stores. Aligned x/y or z/w pairs use one 64-bit lane store; remaining components use 32-bit lane
  stores. Any nonzero partial mask therefore needs at most two stores. The zero hardware mask now
  emits no destination write, and the existing full-mask `STR Q` fast path remains unchanged.
- Correctness follows the actual representations: each `ShaderUnit` destination is an aligned
  `Common::Vec4<f24>`, and `f24` is stored internally as four contiguous IEEE float words. PICA's
  destination bits map x/y/z/w to SIMD lanes 0/1/2/3. Arm Architecture Reference Manual DDI0487
  M.c section C7.2.366 defines `ST1 {Vt.S}[index]` and `ST1 {Vt.D}[index]` as storing exactly the
  selected register element. Output-bank selection and its address calculation are retained.
- This lowering was chosen after checking the Cortex-X3, A715, A710, and A510 optimization guides
  used for the Thor's Snapdragon 8 Gen 2 core mix. All four document the single-lane `ST1` forms;
  the X3 guide additionally notes that stores are buffered while committing in the background.
  The change is baseline Armv8-A/AdvSIMD and does not assume optional SVE.
- Exact generated instruction counts for a temporary partial write fall from four or five to two
  through four. Output-register partial writes, including bank address selection, fall from eight
  or nine to five through seven. Explicit generated memory traffic falls from a 16-byte load plus
  a 16-byte store to 4-12 store-only bytes, a 62.5%-87.5% reduction. These counts do not include
  surrounding shader arithmetic and are not a whole-game performance claim.
- The focused shader regression now preloads a nonzero sentinel destination and checks every one
  of the 14 nonzero partial masks, proving enabled lanes change and disabled lanes survive. A
  manually encoded zero mask verifies that it leaves all four lanes untouched. The existing full
  mask coverage remains. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled and linked the changed
  JIT, this test source, the ARM64 test executable, and `libcitra-android.so` successfully in 4m34s;
  a final incremental verification after adding representation assertions passed in 1m03s. The
  executable was not run because the active restriction forbids using the Thor and it cannot run
  on the x64 host.
- The final incremental `:app:assembleVanillaRelWithDebInfoLite` passed in 27s. The resulting
  28,966,319-byte APK contains only `arm64-v8a` native libraries and has SHA-256
  `F708BEBE442ACBF9BCE1EB990CD1BC6796D9D4ECF80277C4B35A428732453D49`. No ADB command,
  install, launch, or device test was performed.
- This should reduce CPU work and data-cache/store traffic in vertex shaders with partial writes,
  but no FPS or watt result is claimed without a controlled parent-versus-candidate Thor A/B. A
  future allowed test should use the same title and vertex-heavy scene, cache state, driver,
  resolution, layout, performance mode, brightness, fan mode, and duration, recording frametimes,
  process CPU time, battery power, temperature, thermal slope, stability, and visual correctness.

## 2026-08-16 AArch64 PICA Output-Bank Pointer Cache

- Every AArch64 PICA output-register write previously rebuilt the same selected-bank address. A
  full write emitted `ADD` for the fixed `ShaderUnit::output` offset, `LDRB` for `output_bank`, a
  separate `LSL` by the 256-byte bank size, another `ADD`, and finally `STR Q`: five executed
  instructions per write. Partial writes repeated the first four address instructions before their
  lane stores. Temporary-register writes did not have this cost.
- The JIT now reserves caller-saved `X8` for the current output-bank pointer. Shader entry emits
  three instructions once: `LDRB` zero-extends the Boolean bank selector, `ADD (shifted register)`
  folds the bank-size shift into the address addition, and one immediate `ADD` reaches the output
  array. Full output writes then use one `STR Q` with a register-relative immediate. Partial writes
  use one immediate `ADD` before the existing `ST1` lane-store sequence.
- For `N` full output writes, the address/store sequence falls exactly from `5N` instructions to
  `3 + N`, saving `4N - 3`: one instruction for one write, five for two, and thirteen for four.
  The partial-write address portion falls from `4N` to `3 + N`, saving `3N - 3`; the earlier lane
  store reduction remains separate. Geometry `EMIT` deliberately pays the three-instruction setup
  again because it switches banks. These counts exclude shader arithmetic and are not an FPS or
  wattage measurement.
- Correctness follows the real state layout rather than an assumed x86 alias: the two output banks
  are contiguous arrays, `ShaderUnit::OutputBankSize` is statically required to be a power of two,
  and `output_bank` is a Boolean. Arm Architecture Reference Manual DDI0487 M.c section C6.2.6
  defines `ADD (shifted register)`, which directly represents `STATE + bank * 256`. The cached
  caller-saved register is included in the JIT's live-register save set around external calls.
  After the `EMIT` helper toggles `output_bank`, generated code refreshes the pointer before any
  later write. Temporary destinations retain their original `STATE`-relative addressing.
- The destination-mask regression now runs all 14 partial masks against both output banks, so a
  stale or misbased pointer fails while enabled and disabled lanes are checked. A new geometry
  regression writes bank 0, executes a manually encoded `EMIT`, verifies the emitted vertex came
  from bank 0, then verifies the following write lands in bank 1. The same source covers the
  interpreter and JIT implementations.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` rebuilt both changed sources and linked the AArch64
  test executable and `libcitra-android.so` successfully in 1m05s. After merging the two newest
  Azahar upstream commits, the same target passed again in 1m09s. The test executable cannot run
  on the x64 host and the active restriction forbids using the Thor, so this is compile/link plus
  regression-source evidence, not a runtime test. The final
  `:app:assembleVanillaRelWithDebInfoLite` completed in 1m58s. Its 28,966,043-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `3E0783EC7BE887AC38AECBEED34D5851462EDDB2D4CC0327BB7C705F3038616C`. No ADB command, install,
  launch, or device test was performed.
- After verification, the final exact 1,854,228,806-byte reproducible
  `src/android/app/build/intermediates` tree was removed. The Gradle daemon was stopped first; the
  final APK and active `arm64-v8a` RelWithDebInfo CMake cache were retained.
- A future allowed Thor A/B should use the same title and vertex-heavy scene, cache state, driver,
  resolution, layout, performance mode, fan mode, brightness, and duration. Record frametimes,
  process CPU time, battery power, temperature, thermal slope, stability, and visual correctness;
  do not infer a whole-game speed or power result from the exact instruction counts alone.

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
