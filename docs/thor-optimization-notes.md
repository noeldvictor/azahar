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
  to 396 bytes (11.6%). More importantly, both changed from 64 decoder calls to four. At this
  checkpoint the scalar block helpers were 312 bytes for ETC1 and 348 bytes for ETC1A4, while the
  original 356-byte sampler was unchanged. This is an algorithmic invariant-hoisting win on every
  host architecture, including the Thor's AArch64 cores, rather than a guest-semantics shortcut;
  the later AArch64 SIMD follow-up is recorded below.
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

## 2026-08-16 AArch64 SoundTouch Stereo-Overlap NEON

- Android configures SoundTouch for 16-bit integer samples and defines `SOUNDTOUCH_USE_NEON`, but
  SoundTouch 2.3.3 has no NEON-specific implementation class. Disassembly showed that Clang already
  autovectorizes the expensive WSOLA cross-correlation loops into AArch64 `SMULL`/`SMLAL`, so those
  loops were deliberately left alone. The stereo overlap loop remained scalar and issued two
  `SDIV` instructions per frame, one for each channel.
- SoundTouch's integer path rounds `overlapLength` to a power of two from 16 through 1024 samples.
  The new AArch64-only path processes four stereo frames per loop with two 128-bit loads,
  `SMULL`/`SMULL2`, `SMLAL`/`SMLAL2`, signed variable shifts, narrowing, and one 128-bit store. Final
  object-code inspection contains zero `SDIV` in this function, replacing eight scalar divides for
  the same four frames. This is a scoped generated-code fact, not a whole-emulator speed claim.
- Correctness preserves the scalar expression rather than copying SoundTouch's older MMX rounding.
  For a negative numerator, adding `(overlapLength - 1)` before the arithmetic right shift exactly
  reproduces C++ signed division's truncation toward zero; nonnegative values receive no bias.
  Boundary-heavy verification covered 292,608 numerator/weight combinations at every supported
  power-of-two length. The committed differential test fills both channels with positive, negative,
  and signed-16-bit edge values and compares the production overlap against scalar division at
  16-, 256-, and 1024-frame overlap lengths. Arm's Neon Intrinsics on Android guide documents the
  widening signed multiply-accumulate operation used here.
- `externals/soundtouch` is now vendored from former submodule commit
  `9ef8458d8561d9471dd20e9619e3be4cfe564796`; a custom dependency gitlink would otherwise require a
  separate repository. The upstream LGPL license remains in-tree. The unused Android wrapper JAR
  and three prebuilt example DLL/shared-library files were omitted, avoiding 780,377 bytes of
  irrelevant binary payload while retaining source, build files, and documentation.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled the NEON source and differential test, linked
  `libSoundTouch.a`, the AArch64 test executable, and `libcitra-android.so`, and completed in 1m14s.
  `:app:assembleVanillaRelWithDebInfoLite` then passed in 54s. The resulting 28,965,579-byte APK
  contains only `arm64-v8a` native libraries and has SHA-256
  `226FC37EB4037E42D24AA8A3F6436E8654BC5C85944D9F588F74A61CB62BB5A1`. The test executable cannot
  run on this x64 host and the active restriction forbids Thor use, so runtime regression and power
  measurements remain pending. No ADB command, install, launch, or device access was performed.
- After verification, Gradle was stopped and the exact 1,854,299,737-byte reproducible
  `src/android/app/build/intermediates` tree was removed. The verified APK and active AArch64
  RelWithDebInfo CMake cache were retained.
- A future allowed A/B should use an audio-active scene that holds below full speed so time stretch
  remains engaged, with the same title, save, cache state, renderer, driver, resolution, display
  layout, speed limit, performance/fan mode, brightness, and duration. Record audio glitches,
  output underruns, frametimes, process CPU time, battery power, temperature, and thermal slope.

## 2026-08-16 AArch64 PICA Four-Command Fast Path

- `PicaCore::ProcessCmdList()` identifies itself as Azahar's most CPU-expensive function outside
  draw calls. Final Android ThinLTO disassembly showed that its existing four-command source loop
  contained no SIMD: Clang expanded the partial-batch control flow into repeated scalar header
  loads, bounds checks, LUT branches, stack staging, register updates, and dirty-bit read/modify/
  writes. The baseline function was 1,476 bytes.
- This change was selected from the actual Snapdragon core guides. Cortex-X3 issue 4.0 table 3-19,
  Cortex-A715 issue 5.0 table 3-19, and Cortex-A710 issue 4.0 table 3-35 give Q-form B/H/S `LD2`
  an eight-cycle L1-hit latency and 3/2-instruction-per-cycle throughput. Cortex-A510 issue 6.0
  table 3-35 gives the same form four-cycle latency and one-instruction-per-two-cycle throughput.
  One `LD2` per four interleaved pairs is therefore a bounded use; heavier structure-load patterns
  were not generalized across the parser. The manual PDFs and temporary rendered pages remain
  outside git and were removed after review.
- The AArch64 common path now deinterleaves four `[value, header]` pairs, validates register bounds
  and extra-data bits together, reduces the invalid mask with `UMAXV`, gathers four special-handler
  flags, and branches once. Four consecutive ordinary IDs use one 128-bit register load, byte-mask
  blend, 128-bit store, and one dirty-word update. Nonconsecutive IDs retain ordered scalar writes
  so duplicate IDs still observe preceding writes; when all four dirty bits share a word they are
  merged into one read/modify/write. Short, invalid, extended, or special batches use a separate
  236-byte scalar loop and then the original slow handler. The call is direct, not through the PLT.
- Correctness is an exact refactoring of the prior conditions. Header bits 0-15 remain the register
  ID, bits 16-19 remain the byte mask, and only bits 20-27 reject the ordinary path; reserved high
  bits and the group bit retain their prior treatment when extra length is zero. The fast path makes
  no state change before all four headers and special-handler flags pass. It adds exactly four delay
  commands, advances exactly eight words, preserves byte-select semantics, and reproduces the same
  dirty set. A consecutive four-register group is unique by construction; its rare 64-bit dirty-
  word crossing explicitly updates the next word.
- The committed ARM64 test runs every `16^4` combination of four parameter masks against an
  independent scalar expansion and blend, checking 262,144 lanes. It also checks all 65,536 IDs
  with extra lengths 0, 1, and 255 and both clear/set reserved/group high bits, for 393,216 header
  cases. Matching host-side semantic sweeps passed. The AArch64 test executable compiled and linked
  but was not run because the host is x64 and Thor use remains forbidden.
- Final ThinLTO contains the intended `LD2`, vector comparisons, `UMAXV`, Q register load/store,
  and direct compact fallback. `ProcessCmdList()` is 1,336 bytes, 140 bytes or 9.5% smaller than the
  1,476-byte baseline; the separate scalar fallback is 236 bytes. These are codegen facts, not a
  whole-game speed or power claim. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled the source and
  tests and linked `libcitra-android.so`; `:app:assembleVanillaRelWithDebInfoLite` passed in 24s.
  The 28,966,471-byte APK contains only `arm64-v8a` native libraries and has SHA-256
  `EC03BDBB838F23748E553D0A12C56D5817C496659209BF7B228E2998229EA388`. No ADB, install, launch,
  or device access occurred.
- A future allowed A/B should use a command-heavy, CPU-limited scene with identical title, save,
  caches, renderer, driver, resolution, layout, performance/fan mode, brightness, and duration.
  Record command counts, ordinary-four hit rate, frametimes, process CPU time, battery power,
  temperature, thermal slope, stability, and rendering correctness.

## 2026-08-17 AArch64 PICA EX2 Literal Packing

- The AArch64 PICA vertex-shader JIT's `EX2` approximation previously executed eight independent
  `ADR` plus scalar `LDR` pairs to materialize its input clamps, `0.5`, and five polynomial
  coefficients every time the helper ran. The constants are compile-time literals and already
  have a fixed lifetime, so repeated address generation was unnecessary.
- This change follows the actual Snapdragon CPU-core guides. Cortex-X3 issue 4.0 table 3-6 gives
  `ADR` latency 1 and throughput 4 instructions/cycle; table 3-13 gives Q-form `LDP` latency 6 and
  throughput 3/2. Cortex-A715 issue 5.0 tables 3-6 and 3-13 give `ADR` latency 1/throughput 2 and
  Q-form `LDP` latency 6/throughput 3/2. Cortex-A710 issue 4.0 tables 3-11 and 3-23 give `ADR`
  latency 1/throughput 4 and Q-form `LDP` latency 6/throughput 3/2. Cortex-A510 issue 6.0 tables
  3-10 and 3-23 give `ADR` latency 1/throughput 2 and Q-form `LDP` latency 3/throughput 1. The
  cited instruction tables are on PDF pages 18/23, 20/26, 27/39, and 22/32 respectively. The
  manuals stay in the external research library indexed by `docs/hardware/README.md`; temporary
  rendered review pages are not retained.
- The eight unchanged 32-bit words now occupy one 16-byte-aligned 32-byte block. One `ADR` and one
  Q-form `LDP` place them in two vector registers; one lane `DUP` supplies `0.5`. Polynomial
  coefficients remain in their loaded lanes and are added with `FMLA` whose multiplicand is exact
  `1.0`. Multiplication by `1.0` is exact for these finite coefficients, so each fused operation
  has the same one addition-rounding step as the former `FADD`; the Horner multiplication and
  addition order, clamps, exponent reconstruction, and NaN branch are unchanged.
- Constant setup therefore falls from 16 executed instructions (eight `ADR` plus eight scalar
  `LDR`) to 3 (`ADR`, Q `LDP`, and lane `DUP`), 13 fewer instructions per helper execution. A
  shader that otherwise needs no constant `1.0` adds one `FMOV` at entry, making the net reduction
  12 instructions for one `EX2`; shaders that already need `ONE`, and subsequent `EX2` helpers,
  retain the full 13-instruction reduction. Alignment can affect emitted padding, so this is an
  executed-instruction count rather than an exact code-byte claim.
- Focused Catch2 coverage now adds fractional negative and positive inputs around the polynomial
  range (`-1`, `-0.5`, `0.5`, and `1.5`) to the existing NaN, clamp, zero, integer-power, and
  high-magnitude cases. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m15s, compiling and
  linking the complete AArch64 test executable and `libcitra-android.so`; the executable was not
  run because the host is x64 and the current instruction forbids using the Thor.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2m03s. The 28,967,111-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `F43CFD5C41900F957380EE4AA4D65135C848D351792C7D26F071132511BD516F`. No ADB, install,
  launch, or device access occurred. The instruction reduction is not a whole-game speed or
  battery-watt claim.
- A future allowed A/B should use an `EX2`-heavy vertex-shader scene with identical title, save,
  caches, renderer, driver, resolution, display layout, performance/fan mode, brightness, and
  duration. Record helper hit counts, frametimes, process CPU time, battery power, temperature,
  thermal slope, stability, and visual correctness.

## 2026-08-17 AArch64 PICA LG2 Literal Packing

- The normal positive-input PICA `LG2` helper previously materialized its first polynomial
  coefficient with `ADR`, scalar `LDR`, and a general-to-vector lane `MOV`, then issued another
  `ADR` and Q `LDR` for the remaining four coefficients. That is five executed setup instructions
  before the otherwise register-only Horner polynomial.
- The same official Snapdragon core tables used for `EX2` were visually rechecked for this change.
  Cortex-X3 issue 4.0 tables 3-6/3-13, Cortex-A715 issue 5.0 tables 3-6/3-13, Cortex-A710 issue
  4.0 tables 3-11/3-23, and Cortex-A510 issue 6.0 tables 3-10/3-23 cover `ADR` and Q-form `LDP` on
  PDF pages 18/23, 20/26, 27/39, and 22/32. Q `LDP` has latency 6 and throughput 3/2 on X3,
  A715, and A710; A510 gives latency 3 and throughput 1. The external PDFs remain indexed in
  `docs/hardware/README.md`, and temporary rendered pages are removed after review.
- The five unchanged coefficient words (`3d74552f`, `beee7397`, `3fbd96dd`, `c02153f6`, and
  `4038d96c`) now occupy an aligned two-Q-register block. One `ADR` plus one Q `LDP` loads `c0-c3`
  into `SRC2` and `c4` into the low lane of `VSCRATCH2`. Every Horner multiply/add remains in the
  same order. The only operand-order spelling change is finite positive `c0 * mantissa` to
  `mantissa * c0`; an exhaustive sweep of all 8,388,608 normalized float32 mantissas in `[1,2)`
  found zero result-bit mismatches. NaN, zero, negative, and infinity control flow and literal
  vectors were not changed.
- Positive-input coefficient setup falls from five executed instructions to two, removing three
  instructions per `LG2` helper execution with no new shader-entry setup. The aligned block adds
  12 bytes of unused literal padding; alignment can also move following code, so this is an exact
  runtime instruction-count result rather than a generated-code-byte claim.
- Focused Catch2 coverage now adds `0.5`, `1.0`, `1.5`, and `2.0` around both range-reduction
  boundaries to the existing NaN, negative, zero, integer-power, and high-magnitude checks.
  `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m13s and linked the complete 443,607,952-byte
  ELF64/AArch64 test executable plus `libcitra-android.so`. The executable was not run because the
  host is x64 and the current instruction forbids using the Thor.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1m57s. The 28,966,559-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `7C163D6E16A1BB1EFCF4D8C7ED28FAA2EA4EC38571217967745D1CE77081E055`. No ADB, install,
  launch, or device access occurred. No whole-game speed or battery-power claim is made.
- A future allowed A/B should use an `LG2`-heavy vertex-shader scene with identical title, save,
  caches, renderer, driver, resolution, display layout, performance/fan mode, brightness, and
  duration. Record helper hit counts, frametimes, process CPU time, battery power, temperature,
  thermal slope, stability, and visual correctness.

## 2026-08-17 Vulkan Recycled-Chunk Wakeup Fix

- `Scheduler::CommandChunk` maintained both a linked-list head and a `recorded_counts` field.
  `Record()` incremented the counter before checking storage capacity, while `ExecuteAll()`
  destroyed every command and reset `submit`, `command_offset`, `first`, and `last` but never reset
  the counter. Once any recycled chunk had carried work, its `Empty()` result therefore remained
  false forever even when its actual command list was empty.
- This intersects the normal frame path. `RendererVulkan::RenderToWindow()` records screen work and
  calls `scheduler.Flush()`, which dispatches the submit chunk and acquires a new or recycled chunk.
  `RasterizerVulkan::TickFrame()` then calls `WaitWorker()`, which first calls `DispatchWork()`.
  With a stale recycled counter, that call could invoke the descriptor dispatch callback, queue an
  empty chunk, notify and wake the worker, acquire the queue/execution/reserve locks, execute zero
  commands, and recycle the chunk again. The bug could therefore add up to one empty worker job to
  a frame after chunk reuse; exact frequency still depends on scheduling and reserve timing.
- `Empty()` now checks `first == nullptr`, and the redundant counter and per-command increment are
  removed. `first` becomes non-null only after a command passes the capacity check and is placed;
  it stays non-null while any linked command is pending and is cleared only after `ExecuteAll()`
  executes and destroys the entire list. A full chunk remains dispatchable, a submit chunk always
  contains its recorded submission callback before `MarkSubmit()`, and a truly empty recycled
  chunk is skipped. No GPU command, submission, fence, semaphore, descriptor update needed by
  recorded work, or callback ordering is removed.
- The shared condition variable still uses `notify_all`: a dispatch must wake the worker even if a
  simultaneous queue-drain waiter also sleeps on that variable. Queue-before-execution lock order,
  command-buffer allocation, submit serialization, and reserve ownership are unchanged. This keeps
  the fix scoped to stale empty-state accounting rather than attempting a risky scheduler rewrite.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m18s, rebuilding the scheduler and all Vulkan
  consumers and linking the complete 443,612,688-byte ELF64/AArch64 test executable plus
  `libcitra-android.so`. `:app:assembleVanillaRelWithDebInfoLite` passed in 2m00s. The 28,966,427-byte
  APK contains only `arm64-v8a` native libraries and has SHA-256
  `056AABB1F4348A5C55E01343E0692EA85932D1F07D61EAE83E350041D7B04D53`.
- No ADB, install, launch, or device access occurred. This proves removal of the stale empty-job
  route in source and validates the ARM64 build, but it does not quantify a watt or FPS result. A
  future allowed A/B should instrument dispatched chunks and executed command counts in the same
  Vulkan title/scene, caches, driver, resolution, display layout, performance/fan mode, brightness,
  and duration, then record empty-dispatch count, worker wakeups, process CPU time, frametimes,
  battery power, temperature, thermal slope, stability, and rendering correctness.

## 2026-08-17 AArch64 PICA Eight-Word Range Scan

- PICA command processing sends contiguous shader program-code and swizzle writes through
  `UpdateProgramCodeRange()` and `UpdateSwizzleDataRange()`. Their AArch64 path compared four words
  at a time, then used `UMAXV` first to decide whether any lane changed and again to locate the
  highest changed lane. Re-uploaded shader data therefore paid one horizontal reduction and the
  complete loop bookkeeping for every four unchanged words.
- This change is based on the official guides for every CPU type in Thor's Snapdragon 8 Gen 2,
  not an x86 analogy. For 4H/4S max/min reductions including `UMAXV`, Cortex-X3 issue 4.0 PDF page
  26 reports latency 2 and throughput 2 instructions/cycle; Cortex-A715 issue 5.0 page 29 reports
  latency 3 and throughput 1; Cortex-A710 issue 4.0 page 43 reports latency 2 and throughput 2;
  and Cortex-A510 issue 6.0 page 36 reports latency 4 and throughput 1. The A510 dependency is the
  strongest reason not to repeat the reduction unnecessarily. The external manuals remain indexed
  by hash in `docs/hardware/README.md`; no PDF or rendered review page is committed.
- A new baseline-Armv8-A block loads two old and two new Q vectors, compares both, ORs their change
  masks, and performs one `UMAXV` for the common all-equal path. It stores both vectors only after
  detecting a difference. High-half index constants are 4-7, so a zero high reduction
  unambiguously selects the low half; the earlier combined reduction proves that low lane zero is a
  valid changed result rather than an all-equal sentinel. The existing four-word NEON loop and
  scalar remainder handle lengths below eight and every tail. SSE and non-NEON behavior are
  unchanged.
- Final ThinLTO emits `LDP` for the adjacent old vectors, two Q `LDR` instructions for new data,
  two vector compares, `MVN`/`ORN`, and one `UMAXV` on the unchanged path; changed data uses `STP`.
  For eight unchanged words, the complete vector-loop body and its result bookkeeping fall from 42
  executed instructions across two old four-word iterations to 25 in one eight-word iteration: 17
  fewer, or 40.5% for that local loop case. Program and swizzle functions each grow by 156 bytes to
  retain optimized four-word and scalar tails (292 to 448 bytes and 296 to 452 bytes respectively),
  a 312-byte total code-size tradeoff. These are local machine-code facts, not whole-game FPS or
  battery-watt estimates.
- New Catch2 differential coverage compares range writes with scalar public-API writes for program
  and swizzle storage, offsets 0 and 3, every count from 0 through 24, unchanged/all/alternating
  masks, and every individual changed lane. It also replays an unchanged range after hash
  calculation and then changes the first word, comparing arrays, largest-used sizes, and hashes.
  A separate exhaustive semantic check passed all 256 eight-lane masks. The Android ARM64 test
  executable compiled and linked as a 443,683,104-byte ELF64/AArch64 PIE but was not run because the
  host is x64 and current instructions forbid using the Thor.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m00s, and
  `:app:assembleVanillaRelWithDebInfoLite` passed in 1m56s. The 28,966,475-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `11535A31D050274F48E4E16E72D0E27F94E659280236F25D9C18663D2839F2DD`. No ADB, install, launch,
  or device access occurred.
- A future allowed A/B should use a shader-command-heavy scene with identical title, save, caches,
  renderer, driver, resolution, display layout, performance/fan mode, brightness, and duration.
  Instrument range-call count and size plus changed/unchanged block rates, then record frametimes,
  process CPU time, battery power, temperature, thermal slope, stability, and visual correctness.

## 2026-08-17 Vulkan Timeline-Poll Cadence

- Every `Scheduler::SubmitExecution()` previously called `MasterSemaphoreTimeline::Refresh()`,
  which enters the Vulkan driver through `vkGetSemaphoreCounterValueKHR`. The call occurred after
  recording the submission command but before dispatching that command to the worker, so it could
  only observe completion of older submissions. At one submission per rendered frame, this was a
  routine host/driver crossing every frame even when no resource or CPU wait needed fresh progress.
- Khronos documents timeline counters as monotonically increasing and explicitly warns that
  [`vkGetSemaphoreCounterValue`](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-semaphores-signaling)
  may be immediately out of date while queue work is pending. Azahar already treats `gpu_tick` as
  a conservative completion cache: `Refresh()` only advances it, `ResourcePool::CommitResource()`
  refreshes immediately if its stale value cannot free an entry, and `Wait()` refreshes before a
  blocking semaphore wait and again afterward. A stale-low value cannot authorize premature reuse.
- Routine submit polling now calls `RefreshOnSubmit()` and queries only for signal ticks divisible
  by four, matching the four-entry command-buffer pool. This leaves three intermediate submissions
  without a routine query; it does not assume that any query observes all older pending work.
  Resource-pool wrap still refreshes on demand; explicit waits are unchanged; rasterizer garbage
  collection may retain sentenced surfaces until a later periodic or on-demand refresh instead of
  destroying them before confirmed completion. The fence fallback's `Refresh()` is already a no-op.
- Final ThinLTO emits `TST signal_tick, #3` plus a conditional branch around the virtual refresh.
  Exactly three of every four scheduled submit polls are skipped, reducing that routine source of
  timeline-counter driver calls by 75% (for example, 60 scheduled calls/second become 15 at 60
  submissions/second). Actual total queries can be higher when waits or resource pressure demand
  fresh state, so this is not a whole-renderer CPU, FPS, or wattage percentage.
- New Catch2 coverage drives twelve sequential signal ticks through a fake master semaphore and
  proves refresh counts of 0, 0, 0, 1 through each four-tick group. A fake four-entry resource pool
  then keeps cached progress stale, fills every entry, wraps, refreshes immediately, and reuses only
  an entry whose recorded tick is confirmed complete. The 443,799,000-byte test executable compiled
  and linked as ELF64/AArch64 but was not run because the host is x64 and current instructions
  forbid using the Thor.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m13s after the test dependency fix, and
  `:app:assembleVanillaRelWithDebInfoLite` passed in 1m49s. The 28,966,695-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `4BC405EF1EB848E1E3841DB951B3F287A17264BBBB4DD43D2FEB7193AEB5E984`. No ADB, install, launch,
  or device access occurred.
- A future allowed A/B should instrument scheduled and on-demand refresh counts in an identical
  Vulkan title/scene, save, caches, driver, resolution, layout, performance/fan mode, brightness,
  and duration. Record driver-call counts, garbage-collection backlog, command-pool growth,
  frametimes, process CPU time, battery power, temperature, thermal slope, stability, and rendering
  correctness. Revert or shorten the cadence if pool growth or retained-surface memory increases
  materially.

## 2026-08-17 ARM64 Vulkan Timeline Atomic Ordering

- A complete use-site audit found that `current_tick` allocates unique numerical submission IDs and
  `gpu_tick` caches only the highest completion ID observed from a Vulkan timeline semaphore or a
  completed fence. Neither atomic publishes command memory, queue entries, resource contents, or
  ownership. Queue work is published under the scheduler mutex/condition variable; the fence path
  waits for Vulkan completion and transfers its fence under `free_mutex`; Vulkan itself orders the
  submitted GPU work.
- Arm Architecture Reference Manual DDI 0487 M.c section C6.2.180 (PDF page 2214) states that
  `LDADDL` stores with release semantics while plain `LDADD` has neither acquire nor release
  semantics. Section C6.2.192 (PDF page 2240) defines `LDAR` as an acquire load. The Cortex-X3 page
  19, Cortex-A715 page 21, and Cortex-A710 page 29 instruction tables list ordinary AArch64 `LDR`
  at latency 4/throughput 3; the Cortex-A510 page 23 table lists latency 2/throughput 2. Those core
  tables do not separately quantify `LDAR`, so no unsupported per-instruction cycle saving is
  claimed.
- `CurrentTick()`, `KnownGpuTick()`, and `NextTick()` now use relaxed ordering. Completed progress
  is merged by `AdvanceGpuTick()`, a relaxed compare/exchange atomic max shared by the timeline and
  fence paths. `MasterSemaphoreTimeline::Refresh()` queries
  `vkGetSemaphoreCounterValueKHR` exactly once and then retries only the local cache update; the old
  weak-CAS loop could repeat the driver query after either a race or a spurious failure.
- Correctness depends on atomicity and monotonicity, not a cross-object happens-before edge. A
  relaxed fetch-add still has one atomic modification order and produces unique ticks. Every
  `gpu_tick` candidate originates only after actual Vulkan completion, and atomic max cannot move
  it backward. A stale-low read can delay reuse, deletion, or a wait short-circuit but cannot claim
  unfinished work is complete. The existing immediate refreshes for resource pressure and explicit
  waits are unchanged.
- Final release-style AArch64 code changed `Scheduler::SubmitExecution()` from
  `__aarch64_ldadd8_rel` to `__aarch64_ldadd8_relax`. Both timeline and fence completion updates call
  `__aarch64_cas8_relax`, and resource-pool progress reads are ordinary `LDR` rather than `LDAR`.
  The timeline refresh contains one driver call before its CAS loop. This is direct confirmation of
  the manual-directed lowering, not an estimate from source.
- Catch2 coverage now proves the cached completion sequence advances from 0 to 7, rejects a
  regression to 3, and then advances to 9, alongside the existing cadence and exhausted-pool
  tests. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m21s, compiling and linking the
  ELF64/AArch64 test executable. It was not executed because this x64 host cannot run it and the
  current instruction forbids Thor/device use.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2m19s. The 28,966,367-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `F87A7B10154F601D7FA880834167DF48AC713EF3538D3C3846552BAF05DC7AC5`. No ADB, install, launch, or
  device access occurred.
- This removes ordering work from frequent small counter operations and avoids redundant driver
  queries under contention, but it is not a major whole-emulator speed or wattage claim. A future
  allowed parent-versus-candidate Thor A/B should use an identical Vulkan title/scene, caches,
  driver, resolution, layout, performance/fan mode, brightness, and duration. Record submission and
  refresh counts, CPU time, frametimes, battery power, temperature, thermal slope, memory growth,
  stability, and rendering correctness.

## 2026-08-17 ARM64 HLE Audio Planar Mix Layout

- Command-line Git/SSH fetched Azahar upstream `master` at `3392c56ce` (`core: Fix another msvc
  compiler bug`). Fork `master` at `297e9a0da` was already 0 commits behind and 70 ahead, so no
  upstream merge or conflict resolution was required.
- The first proposed aux-copy NEON patch was rejected after inspecting the complete ThinLTO
  `libcitra-android.so`. Clang already recognizes the scalar-looking 4x160 transpose: the old
  `AuxReturn()` fast path loaded two four-sample groups and emitted two `ST4` instructions per loop,
  while `AuxSend()` emitted two `LD4` instructions plus ordinary vector stores. The old
  `Source::MixInto()` also used two `LD4` and two `ST4` instructions for each eight source samples.
  Hand-written `vld4q_s32`/`vst4q_s32` would therefore have duplicated existing optimization and
  risked worse loop control and alias behavior.
- Arm Architecture Reference Manual DDI 0487 M.c sections C7.2.213 and C7.2.371 confirm that
  multiple-structure `LD4` de-interleaves memory into four registers and `ST4` interleaves four
  registers into memory. The visually checked Cortex-A510 issue 6.0 table 3-37 on PDF page 49 lists
  Q-form B/H/S `ST4` execution throughput as `1/50`, not an extraction or footnote error. The
  corresponding X3 issue 4.0 page 36 and A710 issue 4.0 page 60 tables list `1/6`; A715 issue 5.0
  page 39 lists `1/2`, while its page 67 complex-instruction guidance still calls out quad
  multiple-structure `LD4`/`ST4` forms as decode-limited. X3 page 32, A715 page 35, A710 page 53,
  and A510 page 44 provide the comparison data for ordinary `TRN`/`ZIP` permutations. The source
  PDFs remain outside the repository and indexed through `docs/hardware/README.md`.
- Rather than replacing one transpose instruction with a core-dependent shuffle sequence, the HLE
  DSP now keeps its temporary four-channel mixes planar end to end. `Source::MixInto()` accumulates
  directly into four contiguous channels; mono/stereo downmix loads those channels directly; and
  shared-memory aux send/return copies an already matching planar layout. Little-endian hosts use
  one 2,560-byte `memcpy` per enabled bus and direction. The compile-time big-endian fallback keeps
  element assignment so `s32_le` conversion semantics remain intact.
- Mixer state still serializes through the historical `std::array<QuadFrame32, 3>` sample-major
  archive type. Save converts planar live state into that exact legacy shape before archival; load
  converts it back afterward. This preserves old save-state field structure and order rather than
  silently changing archives with the in-memory optimization.
- Final release-style ARM64 code contains no `LD4` or `ST4` in `Source::MixInto()` or
  `DownmixAndMixIntoCurrentFrame()`. Source mixing uses ordinary `LDP`/`LDR` and `STP`/`STR`; its
  vector loop grows by seven executed instructions per eight samples versus the structured path,
  an explicit big-core tradeoff for avoiding the A510 bottleneck. Downmix replaces one `LD4` with
  four independent Q loads. `AuxReturn()` shrinks from 0x164 to 0x5c bytes and `AuxSend()` from
  0x1b4 to 0x88 bytes, with one `memcpy` call per full enabled bus instead of transpose loops.
- Catch2 source coverage now fills every aux bus/sample/channel with distinct signed values, checks
  the exact planar aux send result, and compares aux return plus final stereo mixing against the
  scalar reference. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 56 seconds after the final
  save-state compatibility change, compiling and linking the full ELF64/AArch64 test executable and
  `libcitra-android.so`. The test executable was not run because this host is x64 and current
  instructions forbid Thor/device use.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1m21s. The 28,966,339-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `4141003D54AE8B454625EEF021A70A4517A5068D70FB9892F194CE78A25501E5`.
- No device, ADB, install, launch, or game was used. Static code generation makes this a strong
  efficiency candidate, especially if Android schedules HLE audio on an A510, but no whole-game FPS,
  frametime, or wattage gain is claimed. A future allowed A/B should compare audio-heavy gameplay
  with identical title, save, caches, renderer, resolution, driver, layout, performance/fan mode,
  brightness, and duration. Record DSP-thread CPU placement/time, audio underruns, frametimes,
  battery power, temperature, thermal slope, stability, and output correctness.

## 2026-08-17 AArch64 HLE Stereo Source Filters

- A source-level audit found that the 160-sample HLE simple and biquad filters still processed the
  independent left and right channels through duplicated scalar arithmetic. The time dimension
  cannot be parallelized because each output feeds the next sample, but stereo lanes have separate
  histories and can be evaluated together without changing filter order.
- The complete relevant AArch64 AdvSIMD table pages were visually checked in the Cortex-X3 issue
  4.0, Cortex-A715 issue 5.0, Cortex-A710 issue 4.0, and Cortex-A510 issue 6.0 optimization guides.
  Their tables cover `SMULL`/`SMLAL`, arithmetic shifts, and `SQXTN` on every Thor CPU class. Arm
  Architecture Reference Manual DDI 0487 M.c sections C7.2.319, C7.2.325, and C7.2.352 confirm that
  the multiply instructions widen signed elements and that `SQXTN` performs the exact signed
  saturating narrow required by the old clamp. This uses baseline AdvSIMD, not an SVE assumption.
  The source PDFs remain outside the repository and are indexed through `docs/hardware/README.md`.
- AArch64 now packs each stereo sample into two 16-bit lanes. Simple filtering uses one widening
  multiply and one widening multiply-accumulate; biquad filtering uses one widening multiply plus
  four widening multiply-accumulates. The arithmetic right shift and signed saturating narrow match
  the old per-channel fixed-point shift and clamp. Adjacent time samples are never combined.
- Coefficient vectors load once per 160-sample frame. Previous input/output vectors stay in NEON
  registers for the full loop and are written back only at frame end. The reset simple coefficient
  is `32768`, which does not fit signed 16-bit, so reset passthrough is handled as an exact frame copy
  with final history advancement rather than being truncated. Biquad reset passthrough likewise
  records the last two inputs/outputs without redundant arithmetic. The non-AArch64 scalar path is
  unchanged.
- Final release-style ThinLTO code contains the intended by-element `SMULL`/`SMLAL`, `SSHR`, and
  `SQXTN`. For the simple filter, four scalar multiply operations, two scalar shifts, and two
  duplicated clamp sequences become four vector arithmetic/saturation instructions for both
  channels. For biquad, ten scalar multiply operations, two shifts, and two clamp sequences become
  seven vector arithmetic/saturation instructions for both channels. Coefficient and recurring
  state loads/stores also leave the per-sample loop.
- Focused Catch2 coverage compares simple-only, biquad-only, combined-order, multi-frame history,
  signed extremes, channel independence, saturation, and reset-passthrough history against a
  sequential scalar reference. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m4s,
  compiling and linking the full ELF64/AArch64 test executable and `libcitra-android.so`. The test
  executable was not run because this host is x64 and current instructions forbid Thor/device use.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1m29s. The 28,965,995-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `EC107B8FE6D0AB226B68ABE620DC277A4165950E95D00F77C69B9DF3CBA19A11`.
- No device, ADB, install, launch, or game was used. This is a real sustained HLE DSP instruction and
  memory-traffic reduction when source filters are active, but its whole-game FPS and wattage effect
  remains unmeasured. A future allowed matched A/B should use an audio/filter-heavy title with the
  same save, caches, renderer, resolution, driver, layout, performance/fan mode, brightness, and
  duration, then record DSP-thread CPU time/placement, audio underruns, frametimes, battery power,
  temperature, thermal slope, stability, and output correctness.

## 2026-08-17 AArch64 HLE Linear Interpolation

- The HLE linear resampler runs once per output sample for every active source configured for
  linear interpolation. Its independent left and right lanes still used duplicated signed
  subtraction/clamp sequences, scalar 64-bit multiplies, and shifts in the final AArch64 binary.
  Polyphase interpolation remains a separate TODO and currently falls back to this path.
- The full relevant manual pages were visually checked rather than inferred from x86 code. Arm
  Architecture Reference Manual DDI 0487 H.a section C7.2.289 (PDF pages 2663-2664) defines vector
  `SQDMULH` as a corresponding-lane signed saturating doubling multiply that returns the truncated
  high half. The AArch64 ASIMD tables list `SQDMULH` with latency/throughput 4/2 on Cortex-X3 issue
  4.0 page 27, 4/1 on Cortex-A715 issue 5.0 page 29, 4/1 on Cortex-A710 issue 4.0 page 43, and
  latency 4 with the documented `2,1` throughput notation on Cortex-A510 issue 6.0 page 36. These
  are the actual X3/A715/A710/A510 classes in Snapdragon 8 Gen 2, and the implementation uses
  baseline AdvSIMD rather than SVE. The PDFs remain outside Git and are indexed in
  `docs/hardware/README.md`.
- The DSP delta is first saturated to signed 16-bit exactly as before. The phase is always in
  `[0, 2^24 - 1]`; shifting it left seven produces a positive Q31 multiplier no greater than
  `0x7fffff80`. `SQDMULH(delta, phase << 7)` therefore equals the signed arithmetic form of
  `(delta * phase) >> 24`, and its saturation case is unreachable for the bounded delta. For a
  negative product, the old unsigned C++ promotion differs by `2^40` after the shift, which vanishes
  under the final signed-16 narrowing, so every output bit remains identical. The non-AArch64
  scalar path is unchanged.
- Final release-style ThinLTO contains one `SSUBL`, one `SQXTN`, one `SSHLL`, and one two-lane
  `SQDMULH` for both channels. The old binary emitted two scalar `SMULL`s, two scalar logical
  shifts, four signed sample loads, and two duplicated four-instruction clamp chains. The complete
  function shrank from 680 to 636 bytes (44 bytes, or 6.5%), while the deque traversal and sample
  timing remain unchanged.
- Focused Catch2 coverage compares output, output index, consumed input, history, and fractional
  state with an independent scalar DSP reference across six rates, five boundary phases, partial
  output frames, signed extremes, and both saturated-delta directions.
- Command-line Git/SSH refreshed `upstream/master` at `3392c56ce` (`core: Fix another msvc compiler
  bug`); this fork remains zero commits behind and no upstream merge was needed.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1m5s, compiling and linking the focused test
  source, the full ELF64/AArch64 Catch2 executable, and `libcitra-android.so`. The test executable
  was not run because this host is x64 and current instructions forbid Thor/device use.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1m27s. The 28,965,971-byte APK contains only
  `arm64-v8a` native libraries and has SHA-256
  `74A107FA9EEBE2E39A3C04413BD774AD8C4D8AAA7168EB510EC7D07C38339DB0`.
- No device, ADB, install, launch, or game was used. This is a verified per-sample DSP instruction
  reduction, not a whole-game FPS or wattage measurement. A future allowed matched A/B should use a
  title that selects linear resampling, with identical save, caches, renderer, resolution, driver,
  layout, performance/fan mode, brightness, and duration, then record DSP-thread time/placement,
  audio underruns, frametimes, battery power, temperature, thermal slope, and output correctness.

## 2026-08-17 AArch64 ETC1 Block SIMD

- The block-level ETC1 change removed repeated setup, but final release AArch64 code still decoded
  all 16 pixels with a scalar nested loop. Each pixel performed variable selector/sign shifts,
  modifier and base-color table choices, three adds, three duplicated clamps, and four byte stores.
  The approximately 37-instruction pixel body ran 16 times per block, and an 8x8 PICA tile contains
  four blocks. This was the clearest remaining x86-originated scalar texture-upload gap.
- The complete relevant manual pages were visually checked. Arm Architecture Reference Manual DDI
  0487 H.a sections C7.2.309, C7.2.339, C7.2.390, and C7.2.403 (PDF pages 2717, 2801, 2911, and
  2938) define the exact vector operations used here: `SQXTUN` saturates signed lanes into narrower
  unsigned lanes, `TBL` performs byte lookup, signed per-lane `USHL` counts select left or
  truncating right shifts, and `ZIP1` interleaves the lower halves of two vectors. These are
  baseline AdvSIMD instructions and do not assume SVE.
- The Snapdragon 8 Gen 2 core manuals support the choice across the heterogeneous CPU. Cortex-X3
  issue 4.0 pages 27/31/32 list latency/throughput of 2/2 for `USHL`, 4/2 for `SQXTUN`, 2/2 for a
  one-table `TBL`, and 2/4 for `ZIP`. Cortex-A715 issue 5.0 pages 30/34/35 and Cortex-A710 issue 4.0
  pages 44/52/53 list 2/1, 4/1, 2/2, and 2/2 respectively. Cortex-A510 issue 6.0 pages 37/43/44
  list latencies 3, 4, 4, and 3 with the guide's `2,1` execution-throughput notation. The A510's
  table lookup is slower, but one 16-byte lookup still replaces 16 scalar alpha-nibble extractions.
  The external PDFs remain uncommitted and are indexed in `docs/hardware/README.md`.
- AArch64 now decodes each block as two compile-time eight-pixel bands. Lane shifts gather ETC's
  column-major selector and negation bit `4 * x + y` into row-major order. A vector flip mask
  selects horizontal `x / 2` subblocks or whole-band `y / 2` subblocks without per-lane scalar
  setup. Modifier selection, sign, base-color selection, and signed addition stay in 16-bit lanes;
  six `SQXTUN` instructions reproduce the old `[0,255]` clamps for RGB. ETC1A4 splits all 16 alpha
  nibbles, reorders them once with `TBL`, and duplicates each nibble with `SLI`. A ZIP network then
  writes the complete RGBA block with four 16-byte stores, replacing 64 scalar byte stores. The
  non-AArch64 scalar decoder is unchanged.
- Final ThinLTO contains no pixel loop and no lane-by-lane mask construction. The ETC1 function has
  114 straight-line instructions after its unchanged decoder-constructor call; ETC1A4 has 123.
  Static helper sizes grow from 312/348 bytes to 516/560 bytes because the two bands are unrolled,
  but the old roughly 600 dynamically executed pixel-body instructions are gone. Linked code shows
  the intended `USHL`, `SQXTUN`, `ZIP`, and four Q stores; ETC1A4 additionally shows exactly one
  `TBL` and one `SLI`.
- Permanent Catch2 coverage now generates 128 deterministic raw blocks with all flip/differential
  combinations, all modifier-table indices, structured selector/sign extremes, random base colors,
  and alpha extremes. Each block is checked as ETC1 and ETC1A4 with both `+24` and `-24` output
  stride against the independent scalar sampler, comparing the whole guarded buffer so row padding
  and canaries must remain untouched. That is 512 direct decoder cases / 8,192 pixels in addition
  to the existing complete padded-tile tests.
- Command-line Git/SSH refreshed `upstream/master` at `3392c56ce` (`core: Fix another msvc compiler
  bug`); this fork remains zero commits behind, so no upstream merge was needed.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled and linked the source plus the 444,079,176-byte
  ELF64/AArch64 Catch2 executable. `:app:assembleVanillaRelWithDebInfoLite` then passed in 1m59s.
  The 28,964,919-byte APK contains only `arm64-v8a` native libraries and has SHA-256
  `172CFB1B162F118869DCA8360B576AF1AC89BB7B7C0A3CA024EBF17DBE90D5B5`.
- No device, ADB, install, launch, or game was used, and the AArch64 test executable was not run on
  this x64 host. This is a large instruction and store-count reduction when ETC blocks are decoded,
  not yet a whole-game FPS or wattage measurement. A future allowed matched A/B should use an
  ETC-heavy texture-streaming scene with identical save, caches, renderer, resolution, driver,
  layout, performance/fan mode, brightness, and duration, then record texture-upload CPU time,
  frametimes, battery power, temperature, thermal slope, visual correctness, and stability.

## 2026-08-17 HLE Audio Resampler Window

- The shared None/Linear stepping loop previously inserted `xn2` and `xn1` at the front of its
  `std::deque` on every call. Final release AArch64 code then repeated deque block-map arithmetic,
  block-pointer loads, and separate adjacent-sample loads for every output sample, including when
  upsampling reused the same integer input position. This bookkeeping survived even after the
  stereo interpolation arithmetic itself had been reduced to one exact two-lane `SQDMULH`.
- The complete AArch64 load-table pages were rendered and visually checked in the Cortex-X3 issue
  4.0 guide (table 3-7, PDF pages 18-19), Cortex-A715 issue 5.0 guide (table 3-7, pages 20-21),
  Cortex-A710 issue 4.0 guide (table 3-13, pages 28-29), and Cortex-A510 issue 6.0 guide (table
  3-12, pages 23-24). The big-core tables list four-cycle L1-hit latency for the relevant ordinary
  register loads; A510 lists two cycles. Removing dependent container loads is therefore useful on
  every Snapdragon 8 Gen 2 CPU class, without assuming SVE or changing the interpolation ISA. The
  external PDFs remain uncommitted and indexed in `docs/hardware/README.md`.
- The replacement treats history as a virtual sequence: `V(0) = xn2`, `V(1) = xn1`, and
  `V(j) = input[j - 2]` for `j >= 2`. A cached adjacent-sample window follows the monotonic integer
  input position. Reusing a position touches no deque sample; advancing by one performs one
  sequential iterator load. End-of-input state records the same final two virtual samples,
  subtracts the same consumed Q24 position, and erases exactly the corresponding real input
  samples. No history elements are inserted or moved.
- Two apparently broader variants were rejected only after linked-code inspection. A target-based
  rebase lambda became a 488-byte helper called for every output. A later large-decimation seek
  guard still became a 356-byte helper called for every output and forced a 192-byte stack frame.
  Both would have made the common path worse despite looking reasonable in C++. The accepted
  monotonic cursor inlines completely; the only 72-byte out-of-line lambda is the cold
  `ASSERT(rate > 0)` failure path.
- Final ThinLTO `Linear()` has no call in the valid per-output loop. Its normal advance is one
  post-increment `LDR`, while an unchanged position branches directly into the arithmetic. The
  exact `SSUBL`, `SQXTN`, `SSHLL`, `SQDMULH`, `SADDW`, and `UZP1` sequence remains. `Linear()`
  shrinks from 636 to 408 bytes and `None()` from 560 to 368 bytes. For rates above one, the cursor
  loads each skipped sample; the permanent tested matrix reaches 2.75x, where that is at most two
  or three sequential loads per output and avoids the old repeated map lookups. Do not infer a win
  for extreme unprofiled rate multipliers from this static result.
- Permanent Catch2 coverage now compares both None and Linear against the old independent
  deque-prefix algorithm across six rates, five boundary phases, signed extremes, partial output,
  empty and one-to-three-sample input, already-full output, and four consecutive calls. The full
  ELF64/AArch64 test executable compiled and linked successfully. Separately, the actual edited
  `interpolate.cpp` was compiled for the x64 host with only the assertion backend stubbed and
  matched the old algorithm across 20,000 randomized streaming cases, one to four calls per case,
  rates from 0.0625x through 8x, arbitrary PCM16 history/data, input sizes 0-700, appended data, and
  output starts 0-160.
- Command-line Git over SSH refreshed `upstream/master` at `3392c56ce` (`core: Fix another msvc
  compiler bug`). The fork remains zero commits behind, so no upstream merge was required.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 52 seconds after the final rejected
  experiment was removed. `:app:assembleVanillaRelWithDebInfoLite` then passed incrementally in 23
  seconds. The 28,963,055-byte APK contains only `arm64-v8a` native libraries and has SHA-256
  `4D3402454B4D1C499EC736791EA26429B2A670ADAD45E90C49FDF638E8970D2A`.
- No Thor, ADB, install, launch, game, FPS run, or battery measurement was used. This is a verified
  sustained DSP bookkeeping reduction, not a whole-game speed or wattage claim. A future allowed
  matched A/B should use a title with multiple resampled sources and identical save, caches,
  renderer, resolution, driver, layout, performance/fan mode, brightness, and duration. Record
  audio-thread CPU time/placement, underruns, frametimes, battery power, temperature, thermal slope,
  stability, and output correctness.

## 2026-08-17 AArch64 Converted 16-bit Texture Codec

- Converted `RGB5A1`, `RGB565`, and `RGBA4` texture copies still performed scalar per-pixel
  expansion or packing on AArch64. Before this change, final ThinLTO Morton-copy symbols were
  1,436, 1,340, and 1,536 bytes for the encode direction, and 372, 364, and 380 bytes for decode.
  These are common PICA texture formats, so the remaining scalar work was a better target than
  adding a broad architecture flag or approximate color math.
- The relevant instruction tables were read from the actual Cortex-X3 issue 4.0 guide (PDF pages
  27 and 31-36), Cortex-A715 issue 5.0 guide (pages 29-30 and 34-39), Cortex-A710 issue 4.0 guide
  (pages 44 and 52-60), and Cortex-A510 issue 6.0 guide (pages 37 and 43-49). All four cores make
  shifts, narrowing, and `ZIP` useful building blocks. The A510 table is the critical constraint:
  Q-form byte/halfword `ST4` is documented at only `1/50` throughput while ordinary `ST1` is
  `1/cycle`. The implementation therefore interleaves RGBA with `ZIP1`/`ZIP2` and emits ordinary
  paired Q stores rather than using an attractive-looking `ST4` output. D-form `LD4` remains useful
  where encode must deinterleave existing RGBA input. The external manuals remain uncommitted and
  are indexed in `docs/hardware/README.md`.
- `texture_codec.h` now converts sixteen pixels per linear iteration. Full Morton decode handles
  two eight-pixel rows per iteration with `LD2`, vector shifts/masks, exact narrowing, `ZIP`, and
  paired Q stores. Reverse Morton encode uses D-form `LD4` to deinterleave RGBA and `ST2` to write
  the two Morton rows. Exact 5/6/4-bit replication is retained on decode, encode still truncates
  to the high source bits, RGB5A1 alpha remains one bit, bottom-up row placement and padded strides
  are unchanged, and every non-AArch64 path remains scalar.
- Final linked-code inspection confirms that the intrinsics survived ThinLTO. The RGB565 Morton
  decode body contains two `LD2`, two paired Q stores, no `ST4`, and no halfword scalar load. Its
  encode body contains `LD4`/`ST2` and no `ST4`. The linear decode's only halfword scalar load is in
  the tail. Final encode-direction Morton symbols shrink to 1,004, 944, and 932 bytes for RGB5A1,
  RGB565, and RGBA4: 30.1%, 29.6%, and 39.3% below baseline. Decode-direction symbols become 464,
  432, and 440 bytes; they are 15.8-24.7% larger but replace the full 64-pixel scalar conversion
  loop with the vector body. Linear encode symbols are 276, 252, and 256 bytes, while linear decode
  symbols are 392, 352, and 368 bytes.
- Permanent Catch2 coverage exhaustively round-trips all 65,536 packed values for each format
  through Morton tiles. Separate 37-pixel linear decode/encode cases exercise vector bodies,
  scalar tails, and canaries. The ELF64/AArch64 test executable compiled and linked successfully.
  A temporary independent model also verified every possible packed decode and round trip plus
  one million random RGBA encodes per format; it was deleted after use and was not committed.
- While this slice was in progress, command-line Git over SSH refreshed upstream to `32a3c0bfd`
  (`core: dsp: Add volume ramping to the HLE backend (#2409)`). The merge conflict was limited to
  the fork's planar HLE mixer: the resolution keeps `PlanarQuadFrame32` and channel-major indexing
  while adopting upstream ramp state, serialization, dirty activation, per-frame ramp completion,
  and the required non-const `MixInto()`. The complete ARM64 native build passed after the merge.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed, followed by a successful
  `:app:assembleVanillaRelWithDebInfoLite` in 2 minutes 35 seconds. The resulting 28,963,995-byte
  APK contains only `arm64-v8a` libraries and has SHA-256
  `21F4D58969445E3FA3732F9AD1940BB09A170A68B5BF5D53A4DF098C108ABDFA`.
- After verification, only exact generated paths under `src/android/app` were cleaned: Gradle
  intermediates and the 444,317,952-byte linked test executable. The final APK and active ARM64
  release native cache were retained, while free C: space increased by 2,019,221,504 bytes
  (about 1.88 GiB). No source, manual, save, or unrelated file was touched.
- No Thor, ADB, install, launch, game, FPS run, or battery measurement was used. This is an exact
  16-pixel-at-a-time texture conversion and a major dynamic instruction/store-count reduction when
  these formats are copied, not yet a whole-game FPS or wattage claim. A future allowed matched A/B
  should hold title, scene, save, caches, renderer, resolution, driver, layout, performance/fan
  mode, brightness, and duration constant, then record texture-upload CPU time, frametimes, battery
  power, temperature, thermal slope, visual correctness, and stability.

## 2026-08-17 AArch64 Morton Structured-Store Removal

- A final audit of the AArch64 texture codec found two x86-shaped leftovers: native RGB8/D24
  copies emitted D-form byte `ST3`, while IA8, RG8, I8, A8, and IA4 expansion emitted D-form byte
  `ST4`. These instructions are concise in source but are a poor match for Thor's efficiency
  cluster, so this slice targets them without changing formats, guest-visible math, or the scalar
  fallback.
- The decision comes from the checked Arm manuals, not a generic NEON assumption. The Cortex-X3
  tables on PDF pages 32 and 35-36 list D-form byte/halfword `ST3` at `1/2`, `ST4` at `1/3`,
  ordinary one-register `ST1` at `2/cycle`, and `ZIP` at `4/cycle`. The A715 tables on pages 35 and
  38-39 list D-form `ST3`/`ST4` at `1/cycle`, ordinary `ST1` at `2/cycle`, and `ZIP` at `2/cycle`.
  The A710 tables on pages 53 and 59-60 list `ST3` at `1/2`, `ST4` at `1/3`, ordinary `ST1` at
  `2/cycle`, and `ZIP` at `2/cycle`. Most importantly, the A510 tables on pages 44 and 48-49 list
  D-form byte/halfword `ST3` at only `1/17`, `ST4` at only `1/25`, and ordinary one-register `ST1`
  at `1/cycle`. The external PDFs remain uncommitted and are indexed in
  `docs/hardware/README.md`.
- IA8/RG8/I8/A8/IA4 expansion now preserves the existing per-row component generation, combines
  each two-row band, and reuses `StoreRGBA8RowsA64()`. Final code performs the exact interleave
  with `ZIP1`/`ZIP2` and two paired Q stores per band. Native RGB8/D24 retains D-form `LD3` for
  deinterleaving, then uses two exact two-register `TBL` permutations and ordinary Q/D stores for
  each packed 24-byte row. A compile-time proof checks all 24 shuffle indices against
  `component * 8 + pixel`.
- Existing Catch2 cases cover native RGB8 and D24 in both swizzle directions and expanded IA8,
  RG8, I8, A8, and IA4 decoding, including full tiles, bottom-up row placement, and padded linear
  strides. The final ELF64/AArch64 test executable compiled and linked. It was not run because the
  host is x64 and this work deliberately did not use the Thor.
- Final ThinLTO inspection confirms that all edited symbols contain no `ST3` or `ST4` stores.
  Expanded-format bodies contain `ZIP1`/`ZIP2` and paired Q stores. Packed RGB8/D24 bodies contain
  the expected `LD3`, table permutations, and ordinary Q/D stores, with shuffle constants hoisted
  outside the full-tile loop. IA8, RG8, I8, A8, and IA4 symbols shrink from 480, 480, 440, 440, and
  504 bytes to 328, 324, 320, 308, and 344 bytes: reductions of 31.7%, 32.5%, 27.3%, 30.0%, and
  31.7%. RGB8 decode/encode grows from 372/844 to 444/1,028 bytes, and D24 grows from 376/848 to
  448/1,032 bytes, trading about 19-22% more code for removal of the A510's pathological stores.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 14 seconds after the final
  compile-time proof was added. The resulting 28,964,139-byte APK contains only `arm64-v8a`
  libraries and has SHA-256
  `3707EC72ED8EF52A30B9C39E307B180EA2EAB0B158F1608BB6783476409A9BC7`.
- After verification, only exact generated paths under `src/android/app` and the repo-local
  temporary manual/codegen extracts were removed. The final APK and active ARM64 native cache were
  retained; free C: space increased by 2,060,705,792 bytes (about 1.92 GiB). No source, external
  manual, save, or unrelated file was touched.
- No Thor, ADB, install, launch, game, FPS run, or battery measurement was used. This is a verified
  removal of severe structured-store bottlenecks when these texture-copy paths execute, not a
  whole-game speed or wattage result. A future allowed matched A/B must hold scene, save, caches,
  renderer, resolution, driver, layout, performance/fan mode, brightness, and duration constant,
  then record texture-upload CPU time, frametimes, battery power, temperature, thermal slope,
  visual correctness, and stability.

## 2026-08-17 AArch64 Vulkan D24S8 Staging Unpack

- Vulkan uploads reserve five staging bytes per D24S8 pixel, then split contiguous little-endian
  S8D24 input into a four-byte depth plane followed by a one-byte stencil plane before the buffer
  copy. Final baseline ARM64 code did this strictly one pixel at a time. The native D24 loop
  executed ten load/store/shift/bookkeeping instructions per pixel; the D32 fallback executed
  thirteen and issued one scalar `FDIV` per pixel. This pixel-count-wide Vulkan path ranked above
  Y2R and crypto setup work because it is in the active renderer, grows directly with uploaded
  surface area, and had authoritative scalar ThinLTO evidence.
- The relevant manual pages were visually checked before choosing the data layout. Cortex-A510
  issue 6.0 pages 23-24 cover ordinary loads, page 43 lists `XTN`, page 44 lists `UZP`, page 45
  lists one-register `LD1` at `2/cycle`, page 47 lists Q-form byte `LD4` at only `1/3`, and pages
  39-40 list integer-to-float conversion plus Q-form F32 `FDIV` at `1/10`. Cortex-X3 issue 4.0
  pages 18-19 cover ordinary loads, page 31 lists `XTN` at `4/cycle`, and page 32 lists `UZP` at
  `4/cycle`. This favors ordinary vectors plus a narrowing/permute tree over structured `LD4` or
  table constants on both the prime and efficiency ends of Thor. The external PDFs remain
  uncommitted and indexed in `docs/hardware/README.md`.
- `VideoCore::UnpackDepthStencil()` now handles sixteen pixels per AArch64 band. It loads all four
  packed Q vectors before overwriting the in-place depth plane, shifts exact 24-bit depth values,
  narrows the four low stencil-byte streams into one Q vector, and writes contiguous planes. The
  native D24 mode stores shifted integers. The fallback converts with exact vector `UCVTF` and
  `FDIV` by 16,777,215; it deliberately does not substitute reciprocal multiplication. All
  non-AArch64 and sub-sixteen-pixel work retains the scalar expression.
- Final ThinLTO improves on the source intrinsics: Clang folds the six-step narrowing expression
  into three `UZP1` operations. The D24 loop is two `LDP Q`, four `USHR`, two `UZP1 .8H`, two
  `STP Q`, one `UZP1 .16B`, one `STR Q`, and five loop/address instructions: seventeen executed
  instructions per sixteen pixels versus the baseline 160, an 89.4% core-loop instruction-count
  reduction. The D32 loop is twenty-five instructions per sixteen pixels versus the baseline 208,
  an 88.0% reduction, and replaces sixteen scalar divisions with four four-lane `FDIV`. The new
  shared helper is 496 bytes; moving conversion out of the Vulkan command lambda shrinks that
  lambda from 4,616 to 4,424 bytes.
- Permanent Catch2 coverage checks both depth modes at 0, 1, 3, 15, 16, 17, 31, 32, 33, 63, 64,
  and 65 pixels. It includes zero, one, midpoint, top-edge, and patterned 24-bit depths, varied
  stencil bytes, exact float bit patterns, returned depth-plane size, full output, and 32 bytes of
  trailing canary. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1 minute 16 seconds,
  compiling the tests and linking the full ELF64/AArch64 test executable plus final ThinLTO
  library. The executable was not run because the host is x64 and this slice deliberately did not
  use the Thor.
- `:app:assembleVanillaRelWithDebInfoLite` then passed in 2 minutes 25 seconds. The resulting
  28,964,523-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `65BEBCAF86469FC740A8C8E4D18DA02DA3CF6D31B4FF01E2DFF83C85D4007440`.
- After verification, exact generated Gradle intermediates, the 444,353,384-byte ARM64 test
  executable, and repo-local manual renders were removed. The APK and active ARM64 native cache
  were retained; final cleanup increased free C: space by 2,018,963,456 bytes (about 1.88 GiB).
  No source, external manual, save, or unrelated file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. Static codegen
  proves much less CPU work when D24S8 staging is unpacked; it does not prove a whole-game speed or
  wattage change. A future allowed matched A/B should record D24S8 upload CPU time alongside
  frametimes, battery power, temperature, thermal slope, visual correctness, and stability.

## 2026-08-17 AArch64 HLE Source Gain Mixing

- Every enabled HLE source accumulates 160 stereo samples into four planar channels for each of
  three intermediate mix buses. Final baseline ThinLTO kept this loop scalar despite the planar
  layout: its steady body executed 31 instructions per sample, and the ramped body executed 38
  while testing the frame-wide ramp flag again for every sample. This ranked above fallback PICA
  shader swizzles because normal hardware-shader draws bypass that CPU JIT, whereas this mixer is
  sustained work for every active HLE source and enabled bus.
- The complete relevant manual pages were visually inspected before selecting the loop shape.
  Cortex-A510 issue 6.0 pages 39-40 cover Q-form integer/float conversion, multiply, and FMA, while
  pages 43-44 cover widening and `UZP`; Cortex-A710 issue 2.0 pages 47-48 and 52-54, Cortex-A715
  issue 3.0 pages 31-32 and 34-35, and Cortex-X3 issue 4.0 pages 28-29 and 31-32 provide the
  corresponding AdvSIMD execution data. A710 page 82, A715 page 59, and X3 page 56 also recommend
  loop unrolling and non-writeback `LDP`/`STP`. Those tables favor an eight-sample ordinary-load
  plus `UZP` design that works across Thor's prime, performance, and efficiency cores; the external
  PDFs remain uncommitted and indexed in `docs/hardware/README.md`.
- `Source::MixInto()` now selects the ramped or steady AArch64 specialization once per frame. Each
  iteration consumes eight interleaved stereo samples with one compiler-combined `LDP Q`, uses
  `UZP1`/`UZP2` to separate left and right, shares four `SSHLL` plus four `SCVTF` operations across
  the four destinations, and performs vector `FMUL`/`FCVTZS`/integer accumulation directly on the
  planar buses. The ramped specialization creates exact integer sample indices, converts and
  scales them by `1 / 159`, and retains the old fused `start + (end - start) * progress` operation.
  It therefore avoids both a per-sample flag branch and accumulated floating-point index drift.
  Non-AArch64 builds retain the original scalar implementation.
- Final linked ThinLTO contains both specializations inline in `Source::MixInto()` with no helper
  call or vector spill. The steady loop is 52 instructions per eight samples, or 6.5 per sample,
  versus 31 before: a 79.0% executed-instruction reduction. The ramped loop is 74 per eight, or
  9.25 per sample, versus 38 before: a 75.7% reduction. The containing function grows from 268 to
  736 bytes, a deliberate 468-byte tradeoff for eliminating repeated work across this sustained
  DSP loop.
- Focused Catch2 coverage compares steady and ramped output against an independent channel-major
  scalar reference. It includes signed-16 minimum/maximum, zero and +/-1 input, positive,
  negative, fractional, and zero gains, nonzero destination accumulators, 32-byte prefix/suffix
  canaries, ramp-state transitions, and a disabled source. The test compiles and links into the
  native ARM64 test executable; it is not executed on this x64 host because this slice deliberately
  does not use the Thor. `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1 minute 9 seconds.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 15 seconds. The resulting
  28,965,523-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `719AF98D109686002BB37FA19A2AFA43F62647960035837445D5A0B52F8E4C27`.
- After verification, exact generated Gradle intermediates, mapping/debug-symbol output, the
  444,476,912-byte ARM64 test executable, and repo-local manual renders were removed. The APK and
  active ARM64 native cache were retained; final cleanup increased free C: space by 1,053,016,064
  bytes (about 0.98 GiB). No source, external manual, save, or unrelated file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. The instruction
  reduction should lower DSP-thread work when this path executes, but it is not yet a measured
  whole-game speed or wattage gain. A future allowed matched A/B must hold title, scene, save,
  caches, renderer, resolution, driver, performance/fan mode, brightness, layout, and duration
  constant, then record DSP-thread time/placement, audio underruns, frametimes, battery power,
  temperature, thermal slope, output correctness, and stability.

## 2026-08-17 HLE Silent-Bus Elision

- `DspHle::Impl::GenerateCurrentFrame()` previously called `Source::MixInto()` separately for each
  of 24 sources and three buses: 72 cross-function calls per audio frame, even when a source was
  disabled or an auxiliary bus had exact-zero gain. The MerryAudio fixture supplies a representative
  routing shape: it sets only main-bus left/right gains to one while marking all three gain groups
  dirty, leaving both auxiliary buses at zero. This supports optimizing a common case without
  assuming that every title uses it.
- The complete relevant instruction-table pages were visually inspected in the external manuals.
  The 4S `UMINV` and vector `FCMEQ` entries are on Cortex-A510 issue 6.0 pages 36 and 39,
  Cortex-A710 issue 4.0 pages 43 and 46, Cortex-A715 issue 5.0 pages 29 and 30, and Cortex-X3
  issue 4.0 pages 26 and 28. The tables make a single 128-bit compare/reduction a sound
  heterogeneous-core trade for avoiding a full 160-sample pass; the PDFs remain outside Git and
  their hashes stay recorded in `docs/hardware/README.md`.
- `Source::MixInto()` now accepts all three planar destinations at once, handles a disabled source
  once, and loops over the buses internally. A bus is skipped only when all ending gains compare
  equal to zero and, for an active ramp, all starting gains also compare equal to zero. Thus `+0`
  and `-0` are silent, while NaN, every nonzero steady gain, nonzero-to-zero ramps, and
  zero-to-nonzero ramps keep the existing arithmetic and state-transition path. Non-AArch64 builds
  retain `std::any_of`; AArch64 uses one Q load, `FCMEQ #0.0`, `UMINV 4S`, and `FMOV` per checked
  gain vector.
- Final ThinLTO proves one `MixInto()` call inside the 24-source caller loop, reducing calls from 72
  to 24 per frame (66.7%). A steady silent bus executes about 13 predicate/control instructions and
  bypasses the 1,040 instructions in the full 20-iteration NEON body, about 98.8% less core work on
  that bus. A zero-to-zero ramp takes about 20 instructions and bypasses the 1,480-instruction
  ramped body, about 98.6% less. The vector predicate also reduced the combined function from the
  scalar-predicate interim's 936 bytes to 832 bytes. The caller shrank from 552 to 492 bytes. Active
  ramp mixing saves/restores `d8`/`d9` once at function entry/exit, but has no spill/reload traffic
  inside the sample loop; that measured pair replaces two removed outer calls.
- Focused Catch2 coverage now treats all three destinations as one guarded object and checks exact
  steady/ramped output, signed-zero steady silence, zero-to-zero ramp silence, nonzero-to-zero ramp
  arithmetic, zero-to-nonzero ramp arithmetic, all-three-bus disabled-source state advancement,
  unchanged inactive buses, and 32-byte canaries.
  The final `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1 minute 9 seconds, compiling and
  linking the ARM64 test ELF and final library. The test ELF was not run because this host is x64
  and device use is currently forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 13 seconds. The resulting
  28,965,375-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `3683A746697B6E731264EFA00941BE81ED21931392349FEF09B0BCDBB0FB5070`.
- After verification, exact Gradle intermediates, mapping/debug-symbol output, the 444,493,136-byte
  ARM64 test executable, and repo-local manual renders were removed. The APK and active ARM64
  CMake cache were retained; free C: space increased by 2,027,655,168 bytes (about 1.89 GiB). No
  source, external manual, save, or unrelated file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. This is a strong
  DSP-thread efficiency and power candidate when buses are silent, not a whole-game FPS or wattage
  claim. A future allowed matched A/B should hold title, scene, save, caches, renderer, resolution,
  driver, layout, performance/fan mode, brightness, and duration constant, then record DSP-thread
  time/placement, audio underruns, frametimes, battery power, temperature, thermal slope, output
  correctness, and stability.

## 2026-08-17 HLE Front-Stereo Specialization

- The all-bus elision still leaves four destination channels of work on every active bus, even when
  only front-left/front-right gains are configured. The in-tree MerryAudio fixture sets only main
  gains `[0][0]` and `[0][1]`, while its biquad fixture sets only auxiliary gain `[1][0]`; both
  dirty all three gain groups. This is direct repository evidence for a common reduced-routing
  shape, while the unchanged full path remains available for games that use rear channels.
- The complete relevant load/store table pages were visually inspected in the external manuals:
  Cortex-A510 issue 6.0 pages 45 and 48, Cortex-A710 issue 4.0 pages 55 and 58, Cortex-A715 issue
  5.0 pages 36 and 38, and Cortex-X3 issue 4.0 pages 33 and 35. Across the efficiency, performance,
  and prime cores, vector loads/stores consume load/store and vector-side resources; the latter
  three also document forwarding cost into FP/AdvSIMD/vector consumers, and store operations split
  into address and data work. That makes eliminating proven-unused destination traffic preferable
  to performing zero multiplies. The PDFs remain outside Git and their hashes stay recorded in
  `docs/hardware/README.md`.
- AArch64 now selects a front-only template when both ending rear gains are exact signed zero and,
  for an active ramp, both starting rear gains are exact signed zero. One D load plus a 64-bit
  `AND`/`TST #0x7fffffff7fffffff` removes only the two sign bits. Thus `+0` and `-0` can skip rear
  work, while every subnormal, finite nonzero, infinity, or NaN takes the unchanged four-channel
  arithmetic path. Each source/destination iterator remains within its own `std::array` object;
  no flattened cross-subarray pointer arithmetic is used. Non-AArch64 behavior is unchanged.
- Final ThinLTO proves that front-only loops contain no rear offsets (`0x500`, `0x510`, `0x780`,
  or `0x790`). Their two paired destination loads and two paired stores move 2,560 bytes per active
  bus/frame instead of 5,120, a 50% reduction. The steady body falls from 52 to 32 instructions per
  eight samples (38.5%), and the ramped body falls from 74 to 46 (37.8%). The four-channel steady
  and ramped bodies remain exactly 52 and 74, avoiding the one-instruction fallback regression in
  an earlier index-loop draft. `Source::MixInto()` grows from 832 to 1,244 bytes, a 412-byte
  instruction-cache trade for the two specialized loops. The 24-source caller remains 492 bytes
  with one `MixInto()` call per source.
- Focused Catch2 sections compare both steady and ramped front-only routing with the independent
  scalar reference and guarded three-bus destination object, proving exact front accumulation,
  untouched rear channels, ramp-state advancement, and intact canaries. The final native build
  compiled and linked those tests and the ThinLTO library in 56 seconds. The 444,501,016-byte ARM64
  test ELF was not executed on the x64 host because device use remains forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 19 seconds. The resulting
  28,965,995-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `A1EEF78968F5CACAA42C877F68B0C9E77BEE9F8EDCC8EDC21267F3A1A3B6F62A`.
- After verification, exact Gradle intermediates, downloaded JNI copies, mapping/debug-symbol
  output, the ARM64 test executable, and repo-local manual renders were removed. The APK and active
  ARM64 CMake cache were retained; free C: space increased by 2,465,615,872 bytes (about 2.30 GiB).
  No source, external manual, save, or unrelated file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. These are exact
  linked hot-loop and traffic reductions for a repository-evidenced routing shape, not a whole-game
  speed or wattage claim. A future allowed matched A/B must hold title, scene, save, caches,
  renderer, resolution, driver, layout, performance/fan mode, brightness, and duration constant,
  then record DSP-thread time/placement, audio underruns, frametimes, battery power, temperature,
  thermal slope, output correctness, and stability.

## 2026-08-17 HLE Zero-Volume Final-Mix Elision

- `Mixers::MixCurrentFrame()` cleared the output and then unconditionally downmixed all three
  160-sample intermediate buses. Repository evidence shows that this is sustained zero work in a
  normal routing shape: MerryAudio explicitly configures `master_volume = 1.0` and both
  `aux_return_volume` entries to `0.0`. Aux send/return still has to run because it exchanges DSP
  data and updates saved intermediate state, but a zero-volume bus cannot contribute to the final
  signed-16 frame.
- The frame loop now compares each volume with zero before output-format dispatch. Both signs of
  zero skip only the downmix; every finite nonzero and infinity continues to mix, and unordered
  AArch64 `FCMP` makes NaN fall through to the existing arithmetic/conversion path. Integer input
  samples multiplied by signed zero convert to integer zero, so omitting their saturated add is
  exact. `current_frame.fill({})`, aux copies, intermediate buffers, configuration parsing, and
  status behavior are unchanged on all architectures.
- Final ThinLTO proves that production `Mixers::Tick()` contains one `FCMP S, #0.0` plus `B.EQ`
  ahead of the existing format dispatch; the separately emitted `MixCurrentFrame()` has the same
  lowering. The change adds eight bytes to each symbol (`Tick`: 588 to 596 bytes; outlined mixer:
  384 to 392) while leaving active stereo and mono bodies exactly 24 and 23 instructions per four
  samples. A skipped stereo bus avoids all 40 iterations, or 960 loop instructions; mono avoids
  920. Each iteration otherwise reads four input Q vectors and 16 interleaved output bytes and
  writes 16 output bytes, so either skip avoids 3,840 bytes of buffer traffic per bus/frame.
- For MerryAudio's one-active/two-zero stereo shape, the three downmix bodies fall from 2,880 to
  960 executed instructions, a 66.7% loop-work reduction, and traffic falls from 11,520 to 3,840
  bytes, saving 7,680 bytes per audio frame. Predicate and outer-loop control remain, so these
  figures deliberately describe the downmix bodies rather than the entire DSP frame.
- Focused Catch2 sections compare Mono and Stereo output against the independent scalar reference
  with `{0.5, -0.0, +0.0}` volumes, in addition to the existing all-active Mono/Stereo/Surround,
  saturation-edge, and auxiliary-buffer coverage. The ARM64 native build passed and linked the
  tests; the 444,502,472-byte test ELF was not executed on the x64 host because device use remains
  forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 18 seconds. The resulting
  28,966,503-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `7BC55E8E453CC2AA66D7E5EA452A840FC6A03067F18E77FC80AB71CAADE6666B`.
- After verification, exact Gradle intermediates, downloaded JNI copies, mapping/debug-symbol
  output, and the ARM64 test executable were removed. The APK and active ARM64 CMake cache were
  retained; free C: space increased by 2,027,388,928 bytes (about 1.89 GiB). No source, manual,
  save, or unrelated file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. This is an exact
  linked zero-work elimination with direct fixture evidence, not a whole-game speed or wattage
  claim. A future allowed matched A/B must hold title, scene, save, caches, renderer, resolution,
  driver, layout, performance/fan mode, brightness, and duration constant, then record DSP-thread
  time/placement, audio underruns, frametimes, battery power, temperature, thermal slope, output
  correctness, and stability.

## 2026-08-17 AArch64 Eight-Sample Final Downmix

- The active final stereo and mono downmix loops still handled only four output samples per
  iteration after their inputs became planar. Their interleaved signed-16 accumulator made D-form
  `LD2`/`ST2` natural, but repeated the structured memory instructions and loop control 40 times per
  active bus/frame.
- The decision to widen the structured pair comes from the actual Snapdragon 8 Gen 2 core manuals.
  Cortex-A510 issue 6.0 pages 46/49 list D-form halfword `LD2`/`ST2` at throughput 1 and Q form at
  `1/2`, so Q form moves twice the samples with proportional execution cost. Cortex-A710 issue 4.0
  pages 55/59 and Cortex-X3 issue 4.0 pages 33/35 list D/Q `LD2` at 2 versus `3/2` and D/Q `ST2` at
  1 versus `1/2`; Q form improves load bytes per cycle and preserves store bytes per cycle.
  Cortex-A715 issue 5.0 pages 36/38 lists `LD2` at 2 versus `3/2` and `ST2` at 2 for both widths, so
  Q form improves useful bytes per cycle for both. Ordinary loads plus `UZP`/`ZIP` were rejected:
  they add permutation instructions to a two-way interleave whose structured operations are already
  efficient across every Thor core class.
- AArch64 stereo and mono now handle eight samples per iteration. Each half retains the exact prior
  conversion and multiply/FMA sequence; `SQXTN`/`SQXTN2` combines the halves, `.8h` `SQADD` retains
  saturating accumulation, and Q-form `LD2`/`ST2` preserves interleaved output. The non-AArch64 path
  is unchanged, and 160 samples has no tail.
- Final ThinLTO contains Q-form `LD2 {v?.8h, v?.8h}` and `ST2 {v?.8h, v?.8h}`, `SQXTN2`, and `.8h`
  saturated adds with no D-form structured operation, extra `UZP`/`ZIP`, or vector spill in either
  hot loop. Stereo falls from two 24-instruction four-sample iterations to one 39-instruction
  eight-sample iteration: 960 to 780 instructions per active bus/frame, an 18.75% reduction. Mono
  falls from two 23-instruction iterations to one 37-instruction iteration: 920 to 740, a 19.6%
  reduction. Input/output traffic remains 3,840 bytes per active bus/frame; exact zero-volume buses
  still bypass the active body entirely.
- Existing differential Catch2 coverage exercises Mono, Stereo, and Surround output, signed-zero
  buses, all-active gains, saturation edges, aux exchange, and all 160 sample positions against an
  independent scalar reference. The ARM64 native build compiled and linked that test executable and
  the final ThinLTO library successfully; the ARM64 executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 21 seconds. The resulting
  28,966,095-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `B7C89A4157658BFBE4F9054F0C6182280346A7D934487E7E68D33BCB4E41B1C0`.
- After verification, exact Gradle intermediates, downloaded JNI copies, mapping/debug-symbol
  output, the 444,504,472-byte ARM64 test executable, and repo-local manual renders were removed.
  The final APK and active ARM64 CMake cache were retained; free C: space increased by
  2,024,067,072 bytes (about 1.88 GiB). No source, external manual, save, or unrelated file was
  touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. This is a bounded
  linked DSP-loop instruction reduction and a plausible CPU-energy improvement, not a measured
  whole-game speed or wattage result. A future allowed matched Thor A/B must hold title, scene,
  save, caches, renderer, resolution, driver, layout, performance/fan mode, brightness, and duration
  constant, then record DSP-thread time/placement, audio underruns, frametimes, battery power,
  temperature, thermal slope, output correctness, and stability.

## 2026-08-17 AArch64 Converted-D24 Morton Tiles

- The Vulkan renderer's converted `D24` path still expanded each 192-byte PICA Morton tile to
  256 bytes of D32 float, and packed it back again, one pixel at a time on AArch64. Baseline final
  ThinLTO showed 102 instructions in the decode x-column loop repeated eight times, or 816 core
  inner instructions per tile. Encode showed 93 instructions repeated eight times, or 744 per
  tile. Both contained 64 scalar conversions and encode also issued 192 scalar byte stores.
- The complete relevant table pages were visually inspected in all four external Snapdragon 8 Gen
  2 core manuals: Cortex-A510 issue 6.0 pages 39-46, Cortex-A710 issue 4.0 pages 46-56,
  Cortex-A715 issue 5.0 pages 30-37, and Cortex-X3 issue 4.0 pages 28-34. A direct four-register
  table gather was rejected because A510 documents four-table `TBL` at latency 16 and throughput
  `1/9`. D-form `LD3` directly de-interleaves eight packed D24 pixels, while one-table `TBL`,
  ZIP/UZP, and narrowing avoid that efficiency-core cliff. The PDFs remain outside Git and their
  hashes stay recorded in `docs/hardware/README.md`.
- The new AArch64 path handles a complete two-row, sixteen-depth band per iteration. Decode uses two
  D-form byte `LD3`, three one-table Morton permutations, ZIPs to assemble little-endian `u32`
  lanes, and four-lane `UCVTF` plus true `FDIV`. Encode uses paired Q float loads, exact `FMUL` plus
  `FCVTZU`, narrowing/UZP byte extraction, three inverse Morton permutations, and the existing
  exact two-`TBL2` packed-store helper. The scalar non-AArch64 path is unchanged; no reciprocal
  approximation or changed truncation was introduced.
- Final ThinLTO proves decode's two-row loop is 37 instructions repeated four times, or 148 core
  inner instructions per tile: 81.9% fewer than the 816-instruction scalar baseline. Encode is 57
  instructions repeated four times, or 228 per tile: 69.4% fewer than the 744-instruction baseline.
  The linked loops contain the intended `LD3`, `TBL1`, ZIP/UZP/narrow, vector conversion/divide,
  and ordinary packed stores, with no four-table `TBL`, per-pixel fallback, or hot-loop spills.
  Tile memory traffic is unchanged; this removes CPU instruction and scalar memory-operation work.
- Focused Catch2 coverage constructs all 64 Morton positions from zero, one, midpoint, maximum,
  near-maximum, recognizable edge values, and deterministic patterns. It compares every decoded
  float byte against the scalar division, preserves a ten-pixel padded stride and canaries, and
  compares encode against the scalar float-multiply/truncate expression instead of assuming every
  decoded float round-trips to its original integer.
- The final native ARM64 build compiled and linked the focused test plus the production ThinLTO
  library successfully. The 444,519,336-byte ARM64 test executable was not run on this x64 host
  because device use remains forbidden. `:app:assembleVanillaRelWithDebInfoLite` then passed in
  1 minute 22 seconds; the resulting 28,966,415-byte APK contains only `arm64-v8a` libraries and
  has SHA-256 `E0A8C836AA1E9D1240F71223E30DC8C0BD54115451ED9CD9631FCD38B5F07DC8`.
- After verification, exact Gradle intermediates, generated sources, Kotlin/temp output,
  mapping/debug-symbol output, the ARM64 test executable, and every repo-local manual render were
  removed. The final APK and active ARM64 CMake cache were retained; free C: space increased by
  1,935,851,520 bytes (about 1.80 GiB). No source, external manual, save, or unrelated file was
  touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. This is a strong
  depth-upload/readback CPU-efficiency candidate when converted D24 surfaces are active, not a
  measured whole-game speed or wattage result. A future allowed matched Thor A/B must hold title,
  scene, save, caches, renderer, resolution, driver, layout, performance/fan mode, brightness, and
  duration constant, then record renderer-thread time, upload/readback frequency, frametimes,
  battery power, temperature, thermal slope, visual depth correctness, and stability.

## 2026-08-17 AArch64 SoundTouch FIR

- SoundTouch documents `LONG_SAMPLETYPE` as its 32-bit integer accumulation type, but defined it as
  C++ `long`. That is 32-bit under Windows LLP64 and 64-bit under Android AArch64 LP64. Final ARM64
  code therefore ran the 64-tap stereo anti-alias FIR as scalar `LDRSH`/`SMADDL` with two 64-bit
  accumulators: 25 inner instructions repeated 32 times, or about 800 core inner instructions per
  output stereo frame.
- `LONG_SAMPLETYPE` is now explicitly `int32_t`. The AArch64 stereo loop also reads the canonical
  coefficient table once for both channels instead of loading the table that duplicates every
  coefficient for older generic SIMD compilers. Non-AArch64 builds retain that generic duplicated
  table path. The 64 taps, signed products, arithmetic result shift by 14, signed-16 saturation,
  scalar remainder, and output count are unchanged.
- The complete relevant pages were visually checked in the external Cortex-X3 issue 4.0 guide
  (pages 26-28 and 33), Cortex-A715 issue 5.0 guide (pages 28-29 and 36), Cortex-A710 issue 4.0
  guide (pages 42-43 and 55), and Cortex-A510 issue 6.0 guide (pages 35-36 and 46). They cover the
  emitted `ADDV`, `SMLAL`/`SMLAL2`, multiply-accumulate dependency behavior, and Q-form `LD2`.
  This directly drove the choice to keep eight independent accumulation vectors and remove two
  unnecessary structured coefficient loads per sixteen taps.
- Final linked ARM64 code handles sixteen taps with one paired coefficient load, two sample `LD2`,
  eight `SMLAL`/`SMLAL2`, and loop control. Its 17-instruction inner body repeats four times, or
  68 instructions per output frame: 91.5% fewer than the roughly 800-instruction scalar baseline.
  The first 32-bit auto-vectorized form was 24 instructions repeated four times; using the canonical
  coefficients removes another 28 instructions per output frame (29.2%) and cuts coefficient reads
  from 256 to 128 bytes. Total sample-plus-coefficient input traffic falls from 512 to 384 bytes per
  output frame. The full function grows from 336 to 444 bytes to hold vector and remainder paths.
- A coefficient sweep across 100,001 cutoffs from 0 through 0.5 found a maximum absolute 64-tap
  coefficient sum of 36,421. Even full-scale signed-16 input bounds accumulation at 1,193,443,328,
  below `INT32_MAX`. Permanent Catch2 coverage independently designs the same 64-tap Hamming/sinc
  filters at cutoffs 0.2, 0.391755, and 0.5; it compares every stereo output to a 64-bit scalar
  reference, checks every tested sum fits `int32_t`, exercises signed-16 extremes, and guards both
  ends of the destination buffer.
- A standalone Windows build of the real SoundTouch FIR passed the same reference vectors and was
  deleted afterward. The complete Android ARM64 test executable and final ThinLTO library compiled
  and linked successfully; the linked `evaluateFilterStereo` retains the audited 444-byte NEON
  body. The ARM64 tests were not executed because device use remains forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` passed. The resulting 28,966,279-byte APK contains only
  `arm64-v8a` libraries and has SHA-256
  `DADBA13F988DC6E5E614C814BEF197E96074A4B6E1B67D763F53AAE90BE05F30`.
- After verification, exact Gradle intermediates, downloaded JNI copies, Kotlin/temp output,
  mapping/debug-symbol output, the 444,568,360-byte ARM64 test executable, host-verifier files, and
  repo-local manual renders were removed. The final APK and active ARM64 CMake cache were retained;
  free C: space increased by 2,018,377,728 bytes (about 1.88 GiB). No source, external manual, save,
  or unrelated file was touched.
- This targets SoundTouch's anti-alias filter while time stretching/rate transposition is active.
  It is a large local instruction and memory-traffic reduction, not evidence of a whole-game FPS
  or battery-watt gain. A future allowed matched Thor A/B must hold title, scene, save, caches,
  renderer, resolution, driver, layout, performance/fan mode, brightness, and duration constant,
  then record DSP/audio-thread time, audio underruns, frametimes, battery power, temperature,
  thermal slope, output correctness, and stability.

## 2026-08-17 AArch64 SoundTouch WSOLA Correlation

- SoundTouch's integer WSOLA code explicitly scales its correlation to avoid overflowing a 32-bit
  register, and its adaptive normalizer thresholds top out at 1.6 billion. Nevertheless, it used
  C++ `long`/`unsigned long` for `corr`, `lnorm`, and `maxnorm`: 32 bits on Windows LLP64 but 64
  bits on Android AArch64 LP64. The linked Android loops consequently widened every shifted 32-bit
  lane into 64-bit accumulators even though the pair products and shifts were already 32-bit.
- Those values are now exact `int32_t`/`uint32_t`. The code keeps SoundTouch's paired product/shift
  arithmetic and adaptive thresholds unchanged. Android AArch64 Clang is limited to one vector
  interleave group: its unrestricted 32-bit lowering processed sixteen frames per iteration but
  spilled/restored callee-saved `d8`; the selected eight-frame loop exposes four independent 4S
  accumulators and has no stack or vector-register spill.
- The complete relevant pages were visually checked in the external Cortex-X3 issue 4.0 guide
  (pages 26-28), Cortex-A715 issue 5.0 guide (pages 28-29), Cortex-A710 issue 4.0 guide (pages
  42-43), and Cortex-A510 issue 6.0 guide (pages 35-36). Their basic/widening arithmetic,
  `SMULL`/`SMLAL`, reduction, dependency, latency, and throughput tables drove the decision to
  retain multiple independent 32-bit chains and defer `ADDV` until after the loop.
- Final linked `calcCrossCorr` shrinks from 464 to 416 bytes (10.3%). Its core loop falls from 24
  to 20 instructions per eight stereo frames (16.7%); at a 512-frame overlap, that is 1,536 to
  1,280 inner instructions, saving 256 for the initial correlation window.
- Final linked `calcCrossCorrAccumulate`, used at every subsequent full-search offset, shrinks from
  1,004 to 788 bytes (21.5%). Its correlation body falls from 30 instructions per sixteen frames
  to 12 per eight, or 24 per sixteen (20%); at a 512-frame overlap, that is 960 to 768 inner
  instructions, saving 192 per tested search offset. The final body is exactly two `LD2`, four
  `SMULL`/`SMLAL`, two vector shifts, two vector adds, loop control, and a deferred `ADDV`.
- Permanent Catch2 coverage uses independently generated signed samples and checks the real
  16-, 256-, and 1024-frame overlap configurations across an initial correlation plus nine rolling
  offsets. It asserts every tested correlation, normalizer, and delta fits the intended width and
  models SoundTouch's important rounding detail: initial norm shifts paired squares, while rolling
  updates shift the outgoing/incoming samples individually. An optimized Windows build of the
  real SoundTouch sources passed the same scalar differential algorithm and was removed afterward.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled and linked the permanent test and production
  ThinLTO library. `:app:assembleVanillaRelWithDebInfoLite` then passed in 2 minutes 19 seconds. The
  resulting 28,965,755-byte APK contains only `arm64-v8a` libraries and has SHA-256
  `E8DD5F641E9DDFB4E1A949ADDAFFA5D9DD82CC7F3B923F60ADC6B61D29A33DF9`.
- After verification, exact Gradle intermediates and downloaded JNI copies, mapping/debug-symbol
  output, the 444,622,688-byte ARM64 test executable, host-verifier objects, and repo-local manual
  renders were removed. The APK and active ARM64 CMake cache were retained; net free C: space
  increased by 1,050,345,472 bytes (about 0.98 GiB). No source, external manual, save, or unrelated
  file was touched.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. These are exact
  path-local instruction/code-size reductions, not a whole-game speed or wattage claim. A future
  allowed Thor A/B should hold title, scene, save, caches, renderer, resolution, driver, layout,
  performance/fan mode, brightness, audio backend, speed limit, and duration constant, then record
  DSP/audio-thread time, audio underruns, frametimes, battery power, temperature, thermal slope,
  output correctness, and stability.

## 2026-08-17 SoundTouch Pure-Tempo Rate-Transposer Bypass

- Azahar's `TimeStretcher` changes only tempo; it explicitly holds pitch and playback rate at
  exact `1.0`. SoundTouch's own algorithm documentation says tempo control is implemented purely
  by time stretching, while rate transposition exists for playback-rate and pitch changes.
  Nevertheless, the generic crossover-safe `putSamples()` path sent unity-rate input through
  RateTransposer before TDStretch: a 64-tap anti-alias FIR at cutoff 0.5, linear interpolation at
  rate 1.0, and several intermediate FIFO transfers. See the upstream
  [SoundTouch algorithm description](https://soundtouch.surina.net/README.html#about-algorithms).
- A new default-off `SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY` restores the documented pure-tempo
  topology only for clients that opt in before processing. Azahar enables it in the
  `TimeStretcher` constructor. Explicit topology changes are rejected once input/output accounting
  or TDStretch buffers are live; leaving exact unity effective rate automatically disables it.
  Default generic SoundTouch rate/pitch crossover behavior is unchanged, while `clear()` and
  `flush()` retain the setting for Azahar's continuing pure-tempo stream. Initial latency now
  excludes the unused transposer's 32-sample FIR delay.
- Final ARM64 ThinLTO shows the enabled `SoundTouch::putSamples()` branch loading the setting flag
  and tail-calling `TDStretch::putSamples` directly. It executes no call to RateTransposer,
  `FIRFilter::evaluateFilterStereo`, or `InterpolateLinearInteger::transposeStereo`. The disabled
  branch retains all existing generic behavior.
- In steady state this removes the already optimized FIR's 68 core instructions and 384 bytes of
  logical input reads per output stereo frame, plus the unity interpolator's 32-instruction loop,
  eight sample bytes read, and four bytes written. Removing the following FIFO transfer saves
  another four-byte read/write. The initial RateTransposer input copy is replaced by TDStretch's
  direct input copy, so the net path-local reduction is about 100 DSP instructions, 396 logical
  read bytes, and 12 intermediate write bytes per stereo frame. These are instruction/load counts,
  not estimates of physical DRAM traffic.
- Permanent Catch2 coverage feeds 24,000 deterministic signed-16 stereo frames at tempos 0.72,
  0.93, and 1.08 through chunk sizes from one to 1,024 frames. With x86 extensions disabled for a
  portable host reference, the bypass output matches a standalone TDStretch stage byte-for-byte
  after every chunk. It also checks TDStretch input backlog, reduced latency, flush, clear,
  setting persistence, rejection of explicit mid-stream topology changes, and automatic disable at
  rate 1.01.
- A separate optimized Windows verifier passed the same differential checks. Its five-round,
  order-alternated 192,000-frame microbenchmark measured median generic SoundTouch processing at
  48.70 ms before and 38.97 ms with the bypass, or 1.250x isolated throughput. This x64 result
  validates that material work disappeared but is not a Thor, game, FPS, or wattage measurement.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` compiled and linked the production path, permanent
  test, and ThinLTO library after lifecycle hardening in 1 minute 2 seconds. The ARM64 test
  executable was not run on this x64 host because device use remains forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` then produced an ARM64-only APK successfully. Final
  package: `app-vanilla-relWithDebInfoLite.apk`, 28,966,067 bytes, SHA-256
  `8721FB2078B65E0BF03E342E44026E35F80E89DF78D7D90350EDF658AE9436EF`.
- Post-verification cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while
  deleting the temporary native verifier/objects, 445 MB ARM64 test ELF, Gradle intermediates,
  downloaded JNI staging, mapping, native-symbol, and other reproducible package trees. The build
  tree fell from 2,041,610,960 to 28,966,543 bytes, the retained CMake tree fell from 3,236,646,241
  to 2,786,013,944 bytes, and reported C: free space rose by 2,034,778,112 bytes.
- No device, ADB, install, launch, game, FPS run, or battery measurement was used. A future allowed
  Thor A/B should hold title, scene, save, caches, renderer, resolution, driver, layout,
  performance/fan mode, brightness, audio backend, speed limit, and duration constant, then record
  DSP/audio-thread time, audio underruns, frametimes, battery power, temperature, thermal slope,
  output correctness, and stability.

## 2026-08-17 AArch64 PICA Register-Only Source Swizzles

- The CPU PICA shader JIT previously handled identity, four broadcasts, and twelve single-lane
  substitutions directly. Every other selector loaded a 16-byte byte-index literal into a scratch
  vector and executed `TBL`. That extra data load and table dependency execute for each affected
  source operand on every software shader invocation.
- The actual Cortex-X3 instruction tables list element `DUP`, `EXT`, element `INS`, `REV64`,
  `TRN`, `ZIP`, and `UZP` at latency 2 and throughput 4 instructions/cycle, while one-table `TBL`
  has latency 2 and throughput 2. Cortex-A715 and A710 list both simple permutations and one-table
  `TBL` at latency 2 and throughput 2. Cortex-A510 lists the simple operations at latency 3 and
  one-table `TBL` at latency 4. The old path additionally depended on the index-literal load, so a
  register-only sequence removes data-cache work on all four Thor core classes.
- A compact compile-time planner models 26 exact operations: three rotations with `EXT`, one
  `REV64`, both `ZIP`/`UZP`/`TRN` halves, four lane broadcasts, and twelve lane moves. Composing at
  most two operations covers exactly 149 selectors: one identity, 26 one-operation plans, and 122
  two-operation plans. The other 107 selectors keep the exhaustive literal `LDR` plus `TBL` path.
- Relative to the prior emitter, 10 additional selector values shrink from two generated
  instructions to one. Another 122 retain two generated instructions but replace the literal load
  and `TBL` with two register permutations. If one shader used every newly covered selector, its
  unique literal pool would be 2,112 bytes smaller; real savings depend on each shader's selector
  distribution because literals are shared by selector within a compiled shader.
- Compile-time assertions compose and compare every accepted plan against its exact eight-bit PICA
  selector, lock the `1/26/122/107` distribution, and reject plans longer than two operations. The
  permanent `All Source Swizzles` generated-shader test covers all 256 selectors and the Android
  ARM64 build compiles it along with the production emitter.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` rebuilt and linked the production emitter and ARM64
  test executable successfully in 1 minute. Final ThinLTO retains a 768-byte plan table, a
  104-byte operation table, and the register-permutation emitter. The test executable was not run
  on this x64 host because device use remains forbidden.
- `:app:assembleVanillaRelWithDebInfoLite` produced an ARM64-only package successfully. Final APK:
  `app-vanilla-relWithDebInfoLite.apk`, 28,966,315 bytes, SHA-256
  `895095A30723E9F3FB1A7106B05DCE58EAB44EBE65A6B03C4FB9DEAE66DEB46A`.
- Post-verification cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while
  removing the temporary baseline library, 445 MB ARM64 test ELF, Gradle intermediates, downloaded
  JNI staging, mapping, native-symbol, and other reproducible package trees. The build tree fell
  from 2,041,677,559 to 28,966,791 bytes, the retained CMake tree fell from 3,236,885,695 to
  2,786,232,198 bytes, and reported C: free space rose by 2,058,338,304 bytes.
- Scope is deliberately narrow: normal draws that successfully use hardware vertex shaders bypass
  this CPU JIT. The reduction applies to immediate-mode draws, geometry-shader work, and batches
  that fall back to software vertex processing. No whole-game FPS or battery-watt gain is claimed
  without a controlled Thor A/B. No device, ADB, install, launch, game run, or battery measurement
  was used for this slice.

## 2026-08-17 AArch64 Linear RGB8 Table-Width Removal

- Converted linear RGB8 upload/download still used four-register table lookup for every vector
  conversion. Each sixteen-pixel decode loaded 48 packed BGR bytes, appended an opaque vector, and
  issued four `TBL4` operations to write 64 RGBA bytes. Encode loaded 64 RGBA bytes and issued
  three `TBL4` operations to pack 48 BGR bytes. This runs in the rasterizer-cache linear conversion
  tables; non-converted copies remain `memcpy` and Morton surfaces keep their separate tile paths.
- The complete relevant manual tables were checked in Cortex-X3 issue 4.0 pages 31-35,
  Cortex-A715 issue 5.0 pages 34-38, Cortex-A710 issue 4.0 pages 52-56, and Cortex-A510 issue 6.0
  pages 43-49. X3/A715/A710 list `TBL4` at latency 4 and throughput `2/3`, versus latency 2 and
  throughput 2 for `TBL2`. A510 lists `TBL4` at latency 16 and throughput `1/9`, versus latency 8
  and throughput `2/5` for `TBL2`; its Q-form byte `LD3` is latency 5, throughput `1/3`, and ZIPs
  are latency 3. The PDFs remain external and uncommitted.
- Decode now performs one exact Q-form `LD3` over the complete 48-byte source block. It reverses
  BGR component order, inserts `0xFF` alpha, and emits sixteen RGBA pixels with the existing
  ZIP/store helper. This removes all four `TBL4` operations, four 16-byte shuffle-mask
  loads, and 64 bytes of mask data. It does not over-read the source or approximate any color math.
- Encode proves that each 16-byte packed output block touches only two adjacent Q input vectors.
  Three overlapping two-vector tables therefore retain the same four ordinary Q loads, three
  ordinary Q stores, and twelve-instruction loop while changing all three lookups from `TBL4` to
  `TBL2`. A compile-time proof checks every one of the 48 output indices against exact
  `pixel * 4 + 2 - component` RGBA-to-BGR selection and guarantees every local index is below 32.
- From the manuals' steady issue rates, the encode lookup-only budget falls from 4.5 to 1.5 cycles
  per sixteen pixels on X3/A715/A710 and from 27 to 7.5 cycles on A510. Decode removes a four-`TBL4`
  lookup budget of 6 cycles on the performance cores or 36 cycles on A510, replacing it with
  structured load, simple ZIP, and store work on their corresponding pipelines. These are
  instruction-class issue bounds, not measured loop latency, FPS, or watts.
- Isolated Android-clang `-O3` codegen confirms decode contains `LD3`, ZIPs, and no `TBL`, while
  encode contains three `TBL2` instructions and no `TBL3`/`TBL4` or extra loop instructions. The
  two wrappers' `.text` plus shuffle data fall from 336 to 268 bytes; decode code shrinks from 120
  to 116 bytes, encode stays 104 bytes, and shuffle data falls from 112 to 48 bytes.
- The permanent 37-pixel Catch2 case checks both directions, exact BGR/RGBA component order, opaque
  alpha, two vector iterations, a five-pixel scalar tail, and source/destination canaries. The
  compile-time proof and full ELF64/AArch64 test executable linked successfully with production
  ThinLTO in 1 minute 35 seconds; the final rebuild after strengthening the local-index proof passed
  in 1 minute 10 seconds. Final linked `LinearCopy<..., RGB8, true>` bodies retain the exact intended
  `LD3`/ZIP and three-`TBL2` loops. The ARM64 executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` then passed in 1 minute 27 seconds and produced an
  ARM64-only 28,966,375-byte APK with SHA-256
  `4490B56AB6749AB2D5B81B87246B56E8F3723571F181ED0CE573E023DEE294E0`. After verification,
  2,463,355,063 logical bytes of disposable intermediates, test binaries, and shuffle-codegen
  scratch were removed. C: free space increased by 2,018,582,528 bytes; the APK and active ARM64
  CMake cache remain in the repository workspace.
- No device, ADB, install, launch, game run, FPS test, or battery measurement was used. A future
  allowed matched Thor A/B should record RGB8 linear conversion frequency and renderer-thread time
  alongside frametimes, battery power, temperature, thermal slope, and visual correctness.

## 2026-08-17 AArch64 16-bit Encode Band Fusion

- Converted RGB5A1, RGB565, and RGBA4 encode already handled sixteen pixels per vector body, but
  prepared each eight-pixel half independently. Linear conversion issued two D-form byte `LD4`
  operations and duplicated masks, shifts, widening, and field assembly. Morton conversion needed
  the two loads because its rows can be separated by padded stride, but still duplicated most of
  the arithmetic.
- The complete relevant tables were checked in Cortex-X3 issue 4.0 pages 31-37, Cortex-A715 issue
  5.0 pages 34-40, Cortex-A710 issue 4.0 pages 52-59, and Cortex-A510 issue 6.0 pages 43-50.
  X3 lists D-form byte `LD4` at throughput 1 and Q-form at `1/2`; A715 lists both at `1/2`;
  A710 lists D-form at 1 and Q-form at `1/2`; A510 lists both at `1/3`. One Q-form load transfers
  the same 64 bytes as two D-form loads, so the linear structured-load issue budget stays two
  cycles on X3/A710, falls from four to two on A715, and falls from six to three on A510. The PDFs
  remain external and uncommitted.
- Encode now keeps all sixteen channel bytes in Q registers. Exact high-bit truncation is expressed
  before widening: RGB565 uses `(R & 0xF8) << 8`, `(G & 0xFC) << 3`, and `B >> 3`;
  RGB5A1 changes green to `(G & 0xF8) << 3` and assembles
  `((B >> 2) & 0x3E) + (A >> 7)`; RGBA4 uses `(R & 0xF0) << 8`,
  `(G & 0xF0) << 4`, and `(B & 0xF0) | (A >> 4)`. `SHLL`/`SHLL2` then produces both packed
  halves from each shared byte vector.
- Linear conversion reads the complete sixteen-pixel RGBA block with one Q-form `LD4` and writes
  both packed halves with one `STP`. Morton conversion retains the required D-form `LD4` for
  each non-contiguous row, combines matching channels, shares their byte preparation, and preserves
  the exact two `ST2` tile stores. Neither path over-reads padding or changes the scalar fallback.
- Isolated Android-Clang 18 `-O3` output for a sixteen-pixel encode falls from 25 to 18
  instructions for RGB565 (100 to 72 bytes), 30 to 20 for RGB5A1 (120 to 80 bytes), and 24 to 17
  for RGBA4 (96 to 68 bytes): 28.0%, 33.3%, and 29.2% fewer instructions. This is code shape, not a
  claim that the whole loop or game is faster by those percentages.
- Production ThinLTO confirms one Q-form `LD4`, Q masks/field assembly, `SHLL`/`SHLL2`, and one
  paired Q store in every linear vector loop. Final RGB5A1/RGB565/RGBA4 linear encode symbols shrink
  from 276/252/256 to 232/220/224 bytes, reductions of 15.9%, 12.7%, and 12.5%. Their full Morton
  encode symbols shrink from 1,004/944/932 to 956/912/912 bytes, reductions of 4.8%, 3.4%, and
  2.1%, while retaining the two row loads and exact tile stores.
- An independent 132,608-case component/paired-component algebra check found zero mismatches.
  Permanent Catch2 source exhaustively round-trips all 65,536 packed values for each format through
  Morton encode/decode and separately covers 37-pixel linear vector bodies, scalar tails, and
  canaries. The complete ELF64/AArch64 test executable and production shared library compiled and
  linked successfully with ThinLTO in 1 minute 34 seconds. The ARM64 executable was not run on this
  x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 2 minutes 43 seconds and produced an ARM64-only
  28,966,475-byte APK with SHA-256
  `852CD2B4A43DAAEAD8A2381ADDF262499AD06B38AD6B2DED63782703C361231E`. After verification,
  2,463,352,987 logical bytes of scratch, test binaries, and disposable package intermediates were
  removed. C: free space increased by 2,018,557,952 bytes; the APK and active ARM64 CMake cache
  remain in the repository workspace.
- Command-line Git over SSH refreshed `upstream/master` to `32a3c0bfd`; this fork already
  contained it and remained 93 commits ahead with no upstream-only commit. No device, ADB, install,
  launch, game run, FPS test, or battery measurement was used for this slice.

## 2026-08-17 AArch64 GC-ADPCM Nibble Decode

- HLE source buffers use the GameCube-style ADPCM decoder for fourteen sequential samples in each
  eight-byte frame. The original implementation mapped each high and low four-bit value through a
  sixteen-entry `int` table. Final AArch64 ThinLTO showed two reads of every packed source byte and
  two indexed 32-bit nibble-table loads in the repeated two-sample body: 28 data loads per complete
  frame just to obtain fourteen compressed nibbles.
- The complete relevant instruction pages were checked in Cortex-X3 issue 4.0 page 18,
  Cortex-A715 issue 5.0 page 20, Cortex-A710 issue 4.0 pages 27-28, and Cortex-A510 issue 6.0 pages
  22-23. X3/A715/A710 list basic `SBFM` at one-cycle latency and throughput 6/4/4 respectively;
  A510 lists `SBFX` at two-cycle latency and throughput 3. Direct bitfield sign extension also
  removes the indexed address work and L1 data accesses. The PDFs remain external and uncommitted.
- Decode now reads one packed byte, retains it across the high-nibble result and recurrent state
  update, and sign-extends both four-bit fields directly. Scale, coefficient pair, fixed-point add
  order, high-before-low history dependency, signed clamp, duplicate stereo stores, partial-frame
  behavior, and the historical padded second output/state update for odd sample counts are
  unchanged. This is scalar AArch64 acceleration because the second-order recurrence prevents
  time-lane SIMD without changing the algorithm.
- Production ThinLTO changes the repeated two-sample body from 50 to 46 instructions. A full
  fourteen-sample frame therefore removes 28 inner-loop instructions, fourteen indexed table
  loads, and seven redundant packed-byte loads. Two one-time table-address setup instructions also
  disappear per decoder call. `DecodeADPCM()` shrinks from 500 to 476 bytes (4.8%), and its separate
  64-byte `SIGNED_NIBBLES` constant is removed. Final code retains one post-indexed `LDRSB`, direct
  `SBFX`/bitfield scaling, the dependent `MADD` chains, exact clamp selects, and duplicate stores.
- An independent 512-case byte/nibble sweep found zero differences from the former table. New
  permanent Catch2 coverage compares the complete decoder against an independent table-based
  reference across sixteen data phases, twelve lengths from zero through nine frames, four initial
  histories, every scale/coefficient pair, clipping values, partial frames, and odd sample counts:
  768 complete decode/state comparisons. The full ELF64/AArch64 test executable and production
  shared library compile and link successfully; the executable is not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 22 seconds and produced an ARM64-only
  28,966,955-byte APK with SHA-256
  `949C8F7851C87CF324B482249D7DBAD00EA6A5E6F2DC3BC73CC4DD36910C5F9D`. After verification,
  2,496,851,053 logical bytes of the temporary disassembly, native test executable, and disposable
  package intermediates were removed. C: free space increased by 2,059,210,752 bytes; the APK and
  active ARM64 CMake cache remain in the repository workspace.
- This reduces HLE DSP-thread work for games that stream GC-ADPCM. It is not a whole-game FPS or
  battery-watt measurement, and no device, ADB, install, launch, or game run was used. A future
  allowed matched Thor A/B should instrument ADPCM-decoded samples and DSP-thread time while
  holding the normal title, scene, renderer, driver, display, thermal, and power controls fixed.

## 2026-08-17 Exact-Unity HLE Linear Resampler Bypass

- HLE Linear resampling still entered its full stereo AdvSIMD interpolation body when the requested
  rate was exactly `1.0f` and the Q24 phase had no fractional bits. In that state the step is one
  complete input sample, every subsequent fraction remains zero, and the existing saturated linear
  formula returns `x0` exactly. Running delta formation, Q24-to-Q31 conversion, `SQDMULH`, and result
  repacking cannot change the output.
- Linear now checks both necessary conditions once per call and tail-routes this case through the
  existing None implementation. None uses the same `StepOverSamples()` traversal and therefore
  preserves output fill, monotonic deque-window advancement, consumed input, `xn2`/`xn1`, and final
  `fposition`. A fractional starting phase, including exact-unity calls restored from such a state,
  and every non-unity rate retain the unchanged Linear path.
- The first inlined experiment duplicated a complete copy loop into both template instances and was
  rejected after production ThinLTO grew None and Linear by 216 bytes each without improving None's
  repeated loop. The retained implementation shares the already optimized loop: None stays 368
  bytes and Linear grows from 408 to 448 bytes for its two predicates and tail route.
- Final AArch64 disassembly shows the rate compare and low-24-bit phase test before a tail branch to
  None. The general path still contains the exact `SSUBL`/`SQXTN`/`SSHLL`/`SQDMULH` sequence. For
  every sample on the routed path, the copied output body omits two `FMOV`, `UBFIZ`, `DUP`, `SSUBL`,
  `SQXTN`, `SSHLL`, `SQDMULH`, `SADDW`, and `UZP1`: ten interpolation/packing instructions per
  output, amortized against one small dispatch per resampler call. This is stage elimination proven
  from the exact Q24 arithmetic and linked code rather than an instruction-latency estimate.
- Existing independent scalar-reference Catch2 coverage exercises None and Linear across rates
  `0.25`, `0.5`, `0.9999`, `1.0`, `1.25`, and `2.75`; five starting fractions including zero;
  partial/full output positions; tiny inputs; and exact state/input/output comparison. The complete
  ELF64/AArch64 test executable and production shared library compiled and linked successfully in
  54 seconds. The ARM64 executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 19 seconds and produced an ARM64-only
  28,966,763-byte APK with SHA-256
  `FEE9145F766D7279D10E3B5E012A8B0DF1E14A8A11AEBE50AB3FA69C58659048`. After verification,
  2,457,557,368 logical bytes of the native test executable and disposable package intermediates
  were removed. C: free space increased by 2,019,803,136 bytes; the APK and active ARM64 CMake
  cache remain in the repository workspace.
- This reduces HLE DSP-thread work only for exact-unity, aligned Linear sources. It is not a
  whole-game FPS or battery-watt measurement, and no device, ADB, install, launch, game run, or
  battery measurement was used. A future allowed matched Thor A/B should instrument how many
  sources take the route and record DSP-thread time/placement, underruns, frametimes, battery power,
  temperature, and thermal slope with title, scene, renderer, driver, display, and fan mode fixed.

## 2026-08-17 Sequential HLE PCM Decode Output

- PCM8 and PCM16 decode allocate a `StereoBuffer16` deque and fill it in strict sample order before
  the source resampler consumes from the front. The old loops nevertheless used indexed
  `deque::operator[]` for every output. Final AArch64 ThinLTO recalculated the logical start plus
  index, shifted/masked it into a block number and offset, loaded the deque block-map entry, and only
  then formed the destination address for each sample.
- Decode now obtains the output iterator once and advances it with a counted loop. PCM8 still maps
  each unsigned byte into the high byte of signed 16-bit output. PCM16 still performs native
  little-endian unaligned-safe loads. Mono duplicates exactly into both lanes, stereo keeps left/
  right order, the returned deque size is unchanged, and no input byte or output format changes.
- The four Thor core manuals confirm why removing the map dependency matters even for an L1 hit.
  Cortex-X3 pages 18-19, Cortex-A715 pages 20-21, and Cortex-A710 pages 28-29 list ordinary integer
  load latency 4 and throughput 3; Cortex-A510 pages 23-24 list latency 2 and throughput 2. These are
  the manuals' L1-hit figures. The retained code does not depend on those estimates: linked output
  directly proves that the destination pointer now advances between samples and the deque map no
  longer reconstructs each store address. Three loops retain the current block base across samples;
  PCM16 stereo's remaining per-sample base load only checks the boundary and is not on the store-
  address dependency chain.
- Production ThinLTO changes the repeated PCM8 mono loop from 12 to 10 instructions (16.7%) and
  stereo from 14 to 13 (7.1%). PCM16 mono falls from 11 to 9 (18.2%) and stereo from 11 to 8
  (27.3%). Per-iteration data loads fall from 2/3/2/4 to 1/2/1/2 in the same order. Over 160 decoded
  samples this removes 160-480 loop instructions and 160-320 data loads, depending on format and
  channel count, before the rare block transition. PCM8 grows from 240 to 288 bytes and PCM16 from
  228 to 268 bytes; the 88-byte total code-size trade avoids repeatedly executing the indexed path.
- New permanent Catch2 coverage independently generates PCM8 and little-endian PCM16 inputs for
  mono and stereo, verifies both output lanes, and covers counts 0, 1, 7, 159, 1023, 1024, 1025,
  and 2049. This crosses the Android libc++ 1024-element/4 KiB deque boundary and a second block.
  The complete ELF64/AArch64 test executable and production shared library compile and link
  successfully in 58 seconds; the executable is not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 28 seconds and produced an ARM64-only
  28,966,767-byte APK with SHA-256
  `1F08973FEFA3460C1F1D220F26622A097BC70DBCC921B1C05F569D92DBDA8CF3`. After verification,
  2,457,624,332 logical bytes of the native test executable and disposable package intermediates
  were removed. C: free space increased by 2,019,819,520 bytes; the APK and active ARM64 CMake
  cache remain in the repository workspace.
- This reduces HLE audio decoding work when a title submits PCM8 or PCM16 buffers. It is not a
  whole-game FPS or battery-watt measurement, and no device, ADB, install, launch, game run, or
  battery measurement was used. A future allowed matched Thor A/B should count decoded PCM samples
  and record DSP-thread time/placement, underruns, frametimes, battery power, temperature, and
  thermal slope with the usual title, scene, renderer, driver, display, and fan controls fixed.

## 2026-08-17 Tail-Only HLE Source Frame Silence

- `Source::GenerateFrame()` previously called `memset(current_frame, 0, 640)` before checking the
  source buffer. During normal playback, None, Linear, and the Polyphase placeholder all fill the
  produced prefix through the same resampler traversal. A complete 160-sample frame therefore
  overwrote all 640 bytes immediately, making the initial clear pure write-before-write traffic.
- Generation now clears the whole frame only on empty entry, before either dequeue or disable can
  return a silent frame. A running source starts with its previous contents, lets the resampler
  overwrite `[0, frame_position)`, and clears `[frame_position, 160)` only after an underrun. The
  tail clear remains before sample accounting and `SourceFilters::ProcessFrame()`, so recurrent
  filters still observe the exact same zero padding and histories. Reset, sleep/wakeup, buffer
  state, resampler state, and sample-count behavior are unchanged.
- At the native 32,728 Hz rate, a DSP frame runs about 204.55 times per second. The maximum 24
  active sources formerly wrote 15,360 redundant bytes per tick, or 3,141,888 bytes per second,
  before producing the real samples. This is modest DRAM bandwidth but continuous avoidable store,
  L1/cache-line, and dirty-data work on the DSP thread.
- Baseline production AArch64 ThinLTO emitted a 392-byte `GenerateFrame()` with an unconditional
  entry `memset` of `0x280` bytes. The retained 440-byte function branches around that call when
  `current_buffer` is nonempty; a full 160-sample result reaches accounting/filtering with no clear.
  The empty path still passes `0x280`, and the underrun path computes exactly `640 - 4 *
  frame_position` bytes. The 48-byte code-size increase retains one extra integer register and the
  two correctness paths in exchange for removing the large call and stores from steady playback.
- Permanent Catch2 coverage dirties the previous frame and checks three cases independently: a
  complete frame overwrites every sample, a 35-sample underrun preserves its produced history/input
  prefix and zeros the complete tail, and an empty source returns all-zero output while disabling
  itself. Sample accounting and enabled state are checked as well. The production shared library
  and complete ELF64/AArch64 test executable compile and link successfully; the executable is not
  run on this x64 host, and no Thor/device/ADB action was used.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 27 seconds and produced an ARM64-only
  28,967,359-byte APK with SHA-256
  `E415F1AC895877AED56896AA4AF4A0F9E7E263F7C67EB7466688A974113FE21C`.
- After verification, 2,334,759,497 logical bytes of the native test executable and disposable
  Gradle intermediates were removed. C: free space increased by 1,896,480,768 bytes; the final APK
  and active ARM64 CMake cache remain in the repository workspace.
- This change removes continuous source-generation writes but is not a whole-game FPS or wattage
  result. A future allowed Thor A/B should count active/full/partial sources and record DSP-thread
  time and placement, underruns, frametimes, battery power, temperature, and thermal slope with the
  title, scene, renderer, driver, resolution, display layout, fan, and performance mode fixed.

## 2026-08-17 First-Audible-Bus Final Mix

- `Mixers::MixCurrentFrame()` previously cleared its complete 640-byte stereo output before
  examining three intermediate buses. The first audible bus then loaded those zeros through
  twenty Q-form `LD2` instructions and executed forty lane-wise saturating adds. Each bus
  contribution is already independently clamped to signed 16-bit, so its saturating addition to
  known zero is exactly the contribution itself.
- The mixer now skips leading signed-zero buses and lets the first audible main or auxiliary bus
  define the output directly. Every nonzero or NaN gain still takes the arithmetic path. Later
  audible buses retain the original per-bus clamp followed by saturating accumulation, preserving
  order-dependent clipping. If all three buses are silent, the complete output is cleared so an
  audible previous frame cannot leak. Aux send/return, persistent intermediate buffers, Mono,
  Stereo, and Surround-as-Stereo behavior are unchanged.
- The AArch64 Stereo and Mono downmixers use compile-time direct and accumulated variants, with the
  choice made once outside the 160-sample loop. The non-AArch64 scalar path implements the same
  distinction. `MixCurrentFrame()` is deliberately `CITRA_NO_INLINE`; without that barrier ThinLTO
  duplicated the full mixer into `Tick()`, while the retained form keeps `Tick()` at its baseline
  236 bytes.
- Production ThinLTO immediately before the change emitted common inlined Stereo/Mono bodies of
  40/38 instructions per eight samples. The direct first-main-bus bodies are now 36/35, with no
  output `LD2` or `SQADD`, removing 80/60 repeated instructions per 160-sample frame. This also
  removes the 640-byte initial clear and 640 bytes of output reloads: 1,280 bytes per frame, or
  261,824 bytes/second at 32,728 Hz. Later accumulated Stereo/Mono paths remain 38/36 instructions
  and retain their output load plus two saturating adds.
- The code-size trade is 372 bytes across the outlined implementation: `MixCurrentFrame()` grows
  by 80 bytes and the downmix dispatcher by 292 bytes. That prevents work in the common path at a
  small instruction-cache cost while avoiding the much larger ThinLTO duplication into `Tick()`.
- Focused Catch2 coverage retains exact scalar saturation checks for Mono, Stereo, Surround, and
  multiple active buses, and adds first-audible auxiliary/final-bus cases plus an audible frame
  followed by an all-silent frame for both Mono and Stereo. The production shared library and full
  ELF64/AArch64 test executable compile and link successfully; the executable was not run on this
  x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 22 seconds and produced an ARM64-only
  28,966,155-byte APK with SHA-256
  `EB4E9DA3446929B240BDA3CFC97D8818160C5869B526C7DC67F42F06B7B0AC8F`.
- After verification, 2,334,803,746 logical bytes of the native test executable and disposable
  Gradle intermediates were removed. Reported C: free space increased by 1,818,783,744 bytes; the
  final APK and active ARM64 RelWithDebInfo CMake cache remain in the repository workspace.
- No device, ADB, install, launch, game run, FPS test, or battery measurement was used. This is an
  exact final-mixer instruction and memory-traffic reduction, not a whole-game speed or wattage
  result. A future allowed Thor A/B should hold title, scene, save, caches, renderer, resolution,
  driver, display layout, performance/fan mode, brightness, audio backend, speed limit, and
  duration constant, then record DSP/audio-thread time, audio underruns, frametimes, battery power,
  temperature, thermal slope, output correctness, and stability.

## 2026-08-17 Live-Input Final-Mixer Routing

- `Mixers::Tick()` previously staged every current 2,560-byte planar bus in
  `state.intermediate_mix_buffer` before final downmix. The main bus was always copied. Each
  disabled auxiliary bus was copied to state, while an enabled bus copied the ARM11 return to
  state and separately sent its new input to shared memory. Final mixing immediately read the
  staged main/disabled data back; no later operation consumed it.
- Main and disabled auxiliary buses now mix directly from the const input whose lifetime spans the
  complete tick. Enabled buses still mix the ARM11-returned state populated by `AuxReturn()`, and
  `AuxSend()` still writes their new input to shared memory. The three historical state-buffer
  slots remain serialized for archive compatibility. Their main/disabled values need not be
  refreshed: the current output is serialized separately, and the next tick bypasses those slots
  or overwrites an enabled return before use. Sleep/wakeup behavior is unchanged for the same
  reason.
- Baseline production AArch64 ThinLTO made three plus the number of enabled auxiliaries 2,560-byte
  `memcpy` calls per tick. The retained code makes two per enabled auxiliary and zero when both are
  disabled. This removes one to three state-staging copies every DSP frame: 5,120 to 15,360 bytes
  of load-plus-store traffic. At 32,728 Hz / 160 samples, that is 1,047,296 bytes/second with both
  auxiliaries enabled, 2,094,592 with one enabled, and 3,141,888 with both disabled.
- `Mixers::Tick()` shrinks from 236 to 188 bytes (20.3%) and `AuxSend()` from 136 to 108 bytes
  (20.6%). The outlined `MixCurrentFrame()` grows from 596 to 644 bytes to select live versus
  returned input, so the complete retained mixer-function set shrinks by 28 bytes. The all-disabled
  `Tick()` disassembly has no `memcpy`; enabled branches retain only the required return/send calls.
  The established Stereo/Mono direct-first-bus NEON loops and later saturating accumulation bodies
  are unchanged.
- Existing tests already cover all-disabled direct input and both-enabled ARM11 return/send
  routing. New mixed coverage enables aux 0 only, verifies main and disabled aux 1 use live input,
  verifies aux 0 mixes its ARM11 return and sends its new input, and proves disabled aux 1 shared
  output remains byte-for-byte untouched. The complete ELF64/AArch64 test executable and
  production ThinLTO shared library compile and link successfully in 1 minute 18 seconds; the ARM64
  executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 28 seconds and produced an ARM64-only
  28,966,083-byte APK with SHA-256
  `F96E7B5E1F23C030770F666C3B041D0EF55910B902C2046E8A25027E5AE8C7FB`.
- Across packaging and the final post-format verification cleanup, 3,745,992,762 logical bytes of
  native test executables and disposable Gradle intermediates were removed. Reported C: free space
  increased by 2,859,163,648 bytes across the two cleanup passes; the final APK and active ARM64
  RelWithDebInfo CMake cache remain in the workspace.
- No device, ADB, install, launch, game run, FPS test, or battery measurement was used. The result
  is a proven continuous DSP memory-traffic reduction, not a whole-game speed or wattage claim. A
  future allowed Thor A/B should hold title, scene, save, caches, renderer, resolution, driver,
  display layout, performance/fan mode, brightness, audio backend, speed limit, and duration
  constant, then record aux-enable patterns, DSP/audio-thread time, underruns, frametimes, battery
  power, temperature, thermal slope, output correctness, and stability.

## 2026-08-17 Native Aux-Return Direct View

- The preceding live-input route still copied each enabled ARM11 auxiliary return from its shared
  `s32_le[4][160]` buffer into a persistent planar frame before immediately downmixing it. Android
  AArch64 is little-endian, so `s32_le` is native `s32`; the source lifetime covers `Tick()` and no
  ownership, alignment, or conversion boundary requires that staging copy.
- Native-endian final mixing now reads enabled returns through four independent channel pointers.
  This avoids undefined pointer traversal between nested-array subobjects. The generic non-native-
  endian path retains `CopySharedToPlanar()` and consumes the converted state buffer. Historical
  three-slot state serialization, enabled sends, aux selection, arithmetic order, saturation,
  sleep/wakeup behavior, and output serialization remain unchanged.
- The original mixer made three state-staging copies plus one required send for each enabled aux.
  The final native route makes only the zero, one, or two required sends. It therefore removes
  three 2,560-byte staging copies for every configuration: 15,360 bytes of load-plus-store traffic
  per DSP frame, or 3,141,888 bytes/second at 32,728 Hz / 160 samples. Relative to the preceding
  slice, this saves another 1,047,296 bytes/second with one enabled aux and 2,094,592 with both;
  the already-copy-free all-disabled path is unchanged.
- Production AArch64 ThinLTO shrinks `Mixers::Tick()` from 188 to 136 bytes and `AuxReturn()` from
  92 bytes to a 4-byte `RET`. `AuxSend()` remains 108 bytes. The pointer-view selection grows
  `MixCurrentFrame()` from 644 to 716 bytes and the retained downmixer from 712 to 776 bytes; the
  full retained set nevertheless falls from 1,840 to 1,836 bytes. Disassembly proves the four
  source pointers load once before the loop. The established direct-first-bus and accumulated NEON
  loop bodies retain their instruction counts and have no new spills.
- Existing all-disabled, both-enabled, and mixed enabled/disabled tests cover live main/disabled
  inputs, direct shared returns, required sends, and untouched disabled shared output. The complete
  ELF64/AArch64 test executable and production ThinLTO library compile and link successfully in
  1 minute 9 seconds; the ARM64 executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 43 seconds and produced an ARM64-only
  28,966,523-byte APK with SHA-256
  `50255CCA1E44F0E646B8F3C3178C5D2952CD348E9CB989F32839CE6F2BC1538A`.
- After verification, 2,334,854,595 logical bytes of the native test executable and reproducible
  Gradle intermediates were removed. Reported C: free space increased by 1,896,767,488 bytes; the
  final APK and active ARM64 RelWithDebInfo CMake cache remain in the workspace.
- No device, ADB, install, launch, game run, FPS test, or battery measurement was used. This is a
  bounded always-on DSP memory-system reduction, not evidence for a specific whole-game FPS or
  battery-watt gain.

## 2026-08-17 First-Source Intermediate-Bus Definition

- `DspHle::Impl::GenerateCurrentFrame()` previously value-initialized all three 2,560-byte source
  buses before visiting the 24 HLE sources. The first source with any audible routing then loaded
  the known-zero destinations, added its converted samples, and stored the result. That source can
  instead define each bus it routes directly without changing later accumulation order.
- The complete three-bus set now starts pending. Until one source is audible,
  `Source::MixIntoFirst()` tests the same exact ending gains and ramp starts, direct-writes every
  bus that source routes, and returns a three-bit mask. The caller clears adjacent silent-bus runs
  immediately, marks the complete set initialized, and sends every later source through the
  original `MixInto()` accumulator. This deliberately gives up a later per-bus direct opportunity
  to avoid carrying recurring initialization checks across the remaining sources. An all-silent
  frame still performs one contiguous 7,680-byte `memset`. Signed zero remains silent; NaN and
  every nonzero start/end gain take arithmetic, and every ramp state advances exactly once.
- AArch64 direct full-bus steady/ramped loops are 38/60 instructions per eight samples versus
  52/74 for accumulation. They contain no destination loads or vector adds, saving 280 repeated
  instructions and 5,120 bytes of load/store traffic per 160-sample first contribution. That is
  1,047,296 bytes/second at 32,728 Hz. Direct front-stereo loops are 26/40 versus 32/46, saving 120
  instructions and 2,560 bytes per frame, or 523,648 bytes/second; one 1,280-byte contiguous clear
  explicitly defines the omitted rear planes. If that first source fully routes all three buses,
  the bound is 840 instructions and 15,360 bytes per frame, or 3,141,888 bytes/second.
- Final production ThinLTO removes the unconditional entry clear. `GenerateCurrentFrame()` grows
  from 584 to 768 bytes; `MixInto()`, `MixIntoFirst()`, and the direct helper are 1,244, 440, and
  940 bytes. Against the former 1,828-byte source/driver pair, the complete retained set is 3,392
  bytes, a 1,564-byte code-size cost. `DspHle::Impl::Tick()` remains 124 bytes. The accumulator is
  exactly its baseline size, stays a leaf with only the established `d8`/`d9` save pair, and keeps
  its 52/74 full and 32/46 front loop counts without spills. The initialized state stays in `w19`,
  leaving one `TBNZ` choice per later source rather than per-bus checks. The all-silent route retains
  one bulk clear with no final flag scan.
- Permanent Catch2 coverage checks accumulated output plus first steady/ramped full and front
  contributions, exact rear zeroing, simultaneous direct main/final-bus output and mask, a silent
  first source with ramp transition, disabled-source state, existing destinations, and guard
  canaries. The full ELF64/AArch64 test executable and production ThinLTO library compile and link
  successfully in 1 minute 10 seconds; the final coverage-only test relink passed in 35 seconds.
  The ARM64 executable was not run on this x64 host.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 20 seconds and produced an ARM64-only
  28,969,479-byte APK with SHA-256
  `2E4346084247EEE1C375745BBB240A7B0864DF1C2F1BCB615AB3D715DDD96112`.
- After verification, 2,334,959,336 logical bytes of the native test executable and disposable
  Gradle intermediates were removed. Reported C: free space increased by 1,896,804,352 bytes; the
  final APK and active ARM64 RelWithDebInfo CMake cache remain in the workspace.
- No device, ADB, install, launch, game run, FPS test, or battery measurement was used. These are
  continuous DSP path-local instruction and memory-system reductions, not evidence for a specific
  whole-game FPS or wattage gain. A future allowed matched Thor A/B should hold title, scene, save,
  caches, renderer, resolution, driver, display layout, performance/fan mode, brightness, audio
  backend, speed limit, and duration constant, then record first-source routing, active source
  counts, DSP/audio-thread time and placement, underruns, frametimes, battery power, temperature,
  thermal slope, output correctness, and stability.

## 2026-08-17 Raster Fill Bulk Materialization

- `RasterizerCache::DownloadFillSurface()` previously rounded every requested start down to its
  fill-pattern boundary, backed up the bytes before the requested interval, called runtime-sized
  `memcpy` once per two-, three-, or four-byte pattern, and restored that prefix. A 1 MiB four-byte
  fill consequently executed about 262,144 loop iterations and tiny copies even though the output
  is just one repeating pattern. `SurfaceBase::CanFill()` separately heap-allocated a vector to
  inspect a compatibility pattern whose maximum size is 16 bytes.
- `SurfaceBase::FillMemory()` now writes only `[start_offset, end_offset)`. It copies the partial
  first pattern from the exact phase, seeds one aligned complete pattern, and repeatedly doubles
  the initialized prefix through non-overlapping `memcpy` ranges. Work changes from
  `O(bytes / fill_size)` copy calls to `O(log bytes)`; an aligned 1 MiB four-byte fill needs about
  19 calls. A fill whose two to four source bytes are all equal takes one `memset`. Compatibility
  checks retain their original byte comparisons but use a fixed 16-byte stack array.
- Permanent Catch2 coverage compares the production helper against a byte-at-a-time reference for
  all fill sizes 2/3/4, eight patterns including the solid fast path, starts 0 through 15, and every
  length from 0 through 256. This is 98,688 phase/range cases with full before/after guard checks.
  A separate unaligned 1 MiB three-byte case verifies the exponential path and both canaries.
- Final AArch64 ThinLTO keeps `FillMemory()` as a 292-byte helper. Its solid branch tail-calls
  `memset`; its patterned branch performs one phase calculation and a compact loop around bulk
  `memcpy`. Both OpenGL and Vulkan `DownloadFillSurface()` are 272 bytes and contain one
  `FillMemory()` call with no per-pattern copy loop. `CanFill()` contains no allocation/deallocation
  call. The complete ARM64 test executable and production `libcitra-android.so` compiled and linked
  successfully in 1 minute 33 seconds; the ARM64 executable was not run on the x64 host.
- A temporary optimized x64 verifier first confirmed the old and new output matched, then ran seven
  order-alternated timing rounds. Median 64 KiB time fell from 53.16 to 0.95 microseconds for a
  distinct three-byte pattern (55.9x), and from 46.71 to 0.69 microseconds for a solid four-byte
  pattern (67.3x). At 1 MiB, the same cases fell from 830.77 to 29.05 microseconds (28.6x) and from
  706.63 to 19.57 microseconds (36.1x). The temporary executable/source were removed afterward.
- `:app:assembleVanillaRelWithDebInfoLite` passed in 1 minute 26 seconds. The final ARM64-only APK
  is 28,969,403 bytes with SHA-256
  `6A977BD4DAC49E57080C6816B37F0CB457CBD7969F76592DC59A70DCB74B7073`. Post-build cleanup kept
  that APK and the active ARM64 RelWithDebInfo CMake cache while removing 2,458,871,607 logical
  bytes of the test ELF, JNI staging, native symbols, mappings, and reproducible Gradle output.
  One 5,179,280-byte R8 dex intermediate remains because an existing Java process has it open; it
  is bounded and not required by the APK. Reported C: free space increased by 123,207,680 bytes.
- This is a large isolated CPU-overhead reduction when GPU fill surfaces are materialized back into
  emulated memory. Cache hit rate, fill sizes, and CPU readback behavior determine whole-game
  exposure. No device, ADB, install, launch, game, FPS, power, or temperature measurement was used,
  so no whole-game speed or wattage percentage is claimed.

## 2026-08-17 AArch64 Indexed-Draw Scan Unroll

- `RasterizerAccelerated::AnalyzeVertexArray()` calls `Common::FindMinMax()` for every indexed draw
  to derive the vertex range. The earlier AArch64 fix replaced scalar lane extraction with native
  horizontal reduction, but its main loop still carried one minimum and one maximum dependency
  across every 16-byte vector.
- The Cortex-X3, Cortex-A715, and Cortex-A710 optimization guides list AdvSIMD integer `UMIN`/`UMAX`
  at two-cycle latency; the Cortex-A510 lists three cycles. All four guides recommend memory-loop
  unrolling, and their Q-load tables give two Q loads and one Q-form `LDP` the same useful-byte
  issue rate. The large-scan path now uses four independent minimum and four independent maximum
  accumulators so each chain is revisited only after 64 bytes of other work.
- The throughput path starts at 128 bytes. This requires at least two 64-byte batches before paying
  for six extra accumulator initializations and the six-instruction final tree reduction. Shorter
  scans retain the prior compact one-vector loop; the vector tail and scalar remainder are
  unchanged. Unsigned minimum and maximum are associative, so grouping the same elements into four
  accumulators and reducing them afterward is exactly equivalent for `u8` and `u16`.
- Android Clang 18 compilation of the previous source emitted seven repeated instructions per
  16-byte vector: one Q load, two address updates, one compare, `UMIN`, `UMAX`, and one branch. That
  is 28 instructions per 64 bytes with one recurrence chain. Final production AArch64 ThinLTO emits
  two Q-form `LDP`, four independent `UMIN`, four independent `UMAX`, and five address/control
  instructions per 64-byte batch: 15 instructions, no spills, and 46.4% fewer repeated
  instructions. Both `u8` and `u16` functions grow from 284 to 436 bytes to retain separate small-
  and large-scan paths, a 304-byte combined code-size trade.
- Permanent reference coverage now checks every prefix through 145 byte indices and 73 halfword
  indices. Explicit extrema straddle 63/64 and 127/128 byte positions, covering scalar tails, the
  one-vector loop, the 127/128/129-byte crossover, multiple batches, and empty-input sentinels.
  The complete ELF64/AArch64 test executable and production shared library compiled and linked
  successfully after the final crossover refinement in 1 minute 11 seconds. The ARM64 executable
  was not run on the x64 host, and no device, ADB, install, launch, game, FPS, power, or temperature
  measurement was used.
- `:app:assembleVanillaRelWithDebInfoLite` passed with JDK 17 in 1 minute 23 seconds and produced an
  ARM64-only 28,969,775-byte APK with SHA-256
  `167832B7CBC8F2478F807BFFB62FFA6093503BF0B48BCD6D9ABB14649F40D0E9`. Cleanup retained that APK
  and the active ARM64 RelWithDebInfo CMake cache while removing 2,452,981,293 logical bytes of the
  test ELF, JNI/native staging, mappings, symbols, Kotlin/Gradle intermediates, and temporary
  assembly/manual-inspection files. Reported C: free space increased by 2,011,570,176 bytes. One
  bounded 5,179,280-byte R8 `classes.dex` remains because an existing Java process has it open.

## 2026-08-17 Crypto++ ARM64 Feature-Probe Repair

- Crypto++'s Android compiler supports ARMv8 CRC32 and PMULL, but both CMake probes failed because
  their small `try_compile` projects included `<cryptopp/arm_simd.h>` without the vendored
  `include/` directory. The configure log therefore reported a missing header and treated it as an
  unsupported instruction set. Direct NDK Clang checks proved both probe sources compile when that
  directory is present.
- The shared probe helper now passes its public-header directory through the try-project's
  `INCLUDE_DIRECTORIES`. A clean re-probe reports both `CRYPTOPP_HAVE_ARM_CRC32` and
  `CRYPTOPP_HAVE_ARM_PMULL` successful. Global Crypto++ definitions remain only
  `CRYPTOPP_ARM_NEON_HEADER=1` and `CRYPTOPP_ARM_ACLE_HEADER=1`: the baseline Android binary does
  not receive a global optional-ISA assumption.
- Production `crc_simd.cpp` alone compiles with `-march=armv8-a+crc`, while `gcm_simd.cpp` and
  `gf2n_simd.cpp` alone compile with `-march=armv8-a+crypto`. LLVM disassembly proves `CRC32B/W`
  and `CRC32CB/CW` in the CRC object and `PMULL`/`PMULL2` in the GCM/GF objects. Generic callers
  retain Crypto++'s Android `HasCRC32()` and `HasPMULL()` runtime gates before dispatching to them.
- Azahar currently calls Crypto++ AES, SHA, CCM/CBC/CTR, CMAC/HMAC, RSA, and ECC paths but has no
  direct Crypto++ CRC, GCM, or GF(2) call site. AES and SHA were already compiling to their hardware
  instructions before this repair. The change restores correct latent acceleration and future
  dispatch coverage; it is not evidence of a current game-FPS or battery-watt gain.
- Crypto++ is now vendored from former submodule commit
  `8d92d788421483a43e09acf1cd4a2861cb2b8cab`, keeping the one-line probe repair in this repository
  rather than requiring a separate detached dependency fork. The upstream BSD license and its
  source/test material remain intact.
- The full `:app:buildCMakeRelWithDebInfo[arm64-v8a]` rebuild compiled 465 actions and linked the
  ELF64/AArch64 tests plus `libcitra-android.so` successfully in 4 minutes 23 seconds. Packaging
  with Java 17 then passed in 2 minutes 9 seconds. The ARM64-only APK is 28,969,783 bytes with
  SHA-256 `12B443B1B493AED3974529D587786F615AA93D3E3C933ED31B1F42D11B69A9CE`.
- Cleanup retained the APK and active ARM64 RelWithDebInfo CMake cache while removing
  2,453,230,198 logical bytes of the test ELF, JNI/native staging, mappings, symbols, and
  reproducible Gradle output. Reported C: free space increased by 2,012,958,720 bytes. One bounded
  5,179,280-byte R8 `classes.dex` remains because an existing Java process has it open. No device,
  ADB, install, launch, game, FPS, power, or temperature measurement was used.

## 2026-08-17 Rejected PICA RSQ and Blind Fastmem Shortcuts

- The tempting PICA `RSQ` lowering `FRSQRTE; FMUL; FRSQRTS; FMUL` would be four instructions, not
  three. The Cortex-X3/A715/A710 guides imply a roughly 13-cycle dependent chain versus about
  14-19 cycles for the retained exact `FSQRT; FDIV`; A510 makes the approximation look better at
  roughly 16 versus 27 cycles. That narrow performance-core margin does not justify changing
  numerical behavior. The hardware-tested PICA description documents its reduced float format and
  special cases, but not normal-result bit accuracy or rounding. The exact lowering stays in place
  until guest-output equivalence can be proved, consistent with the fork's no-approximate-PICA rule.
  See [3dbrew's hardware-tested PICA shader instruction behavior](https://www.3dbrew.org/wiki/GPU/Shader_Instruction_Set).
- A true RPCS3-style 4 GiB fastmem view was also rejected as a local toggle. Azahar's current
  AArch64 absolute-offset page table already emits a compact page-index extraction, page-entry load,
  null fallback, and guest access. A safe direct-address view would require coherent 4 GiB virtual
  aliases for each 3DS process and a redesign of the ordinary-array backing/remapping model.
  Pointing Dynarmic fastmem at the existing storage without that aliasing would be incorrect; this
  remains a separately scoped VM architecture project rather than an unsafe shortcut.

## 2026-08-17 AArch64 Y2R Fixed-Point Conversion

- The emulated 3DS Y2R hardware converts video/camera strips from four planar 4:2:2/4:2:0 formats
  and interleaved YUYV into tiled `0xRRGGBB00` output. The Android AArch64 release object still ran
  the complete fixed-point matrix, shifts, clamp, and tile address calculation once per pixel. Its
  planar repeated loop was 36 instructions per pixel; the interleaved loop was 46 per pixel and
  only partially packed two lanes with vector operations.
- The AArch64 path now loads eight luma and four subsampled chroma values per band, duplicates the
  chroma lanes with ZIPs, and evaluates eight pixels with exact signed widening
  `SMULL`/`SMLAL`/`SMLSL`. It preserves `(value >> 3) + offset + 0x18`, the following `>> 5`, and
  clamp to `[0,255]`, then uses saturating narrows and register ZIPs to write the original tile
  words. Interleaved YUYV uses one D-form `LD2`; output avoids the Cortex-A510's slow D-form byte
  `ST4`. Non-AArch64 builds keep the original scalar path.
- The choice is grounded in the checked Thor core manuals: widening multiply/accumulate is listed
  on Cortex-X3 pages 27-28, A715 page 29, A710 page 43, and A510 page 36; ZIP/narrow operations are
  on X3 pages 31-32, A715 pages 34-35, A710 pages 52-53, and A510 pages 43-44. Every coefficient
  product and worst-case three-term channel sum fits signed 32 bits, including `s16` coefficient
  extremes, so no wider or approximate arithmetic is required.
- Independent Catch2 coverage checks all five input formats, widths 8/16/24, heights 1/2/7/8, six
  normal/mixed/extreme coefficient sets, deterministic lane-varying inputs, and untouched tile-row
  canaries against the original scalar formula. The ARM64 test translation unit and test ELF link
  successfully; the executable was not run because device execution is excluded for this review.
- Isolated optimized AArch64 codegen reduces the planar repeated work from 288 instructions for
  eight pixels to 65, or 77.4%, and the interleaved work from 368 to 64, or 82.6%. That corresponds
  to 4.43x and 5.75x less instruction-count work respectively, not measured cycle-speed ratios.
  The full ThinLTO library retains 30 `SMULL`/`SMULL2`, 20 `SMLAL`/`SMLAL2`, 20
  `SMLSL`/`SMLSL2`, 30 `SQXTUN`/`SQXTUN2`, and 15 `UQXTN` instances across the five format loops.
  The test-only 1,996-byte conversion entry point is hidden: it remains in the test ELF but is
  garbage-collected from `libcitra-android.so`, reducing the latter's debug-bearing file by 25,504
  bytes without changing the production converter.
- `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed twice, including the final hidden-test-entry
  relink; both the test executable and production shared library linked with ThinLTO. A subsequent
  clean portable-SDK graph compiled all 2,196 native actions and assembled the APK, although the
  first Gradle invocation then rejected its configuration cache because the build script calls
  command-line Git. The required incremental rerun with `--no-configuration-cache` passed cleanly
  in 33 seconds. The ARM64-only APK is 28,969,635 bytes with SHA-256
  `801E86EA3F3848C8BE362CE150D77806299082B583E05A4F83820E6657275922`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,505,957,707 logical bytes of the test ELF, APK staging/mappings/symbols, Gradle intermediates,
  and temporary baseline/codegen objects. Reported C: free space increased by 2,076,962,816 bytes.
  Three Gradle HTML reports remain file-handle locked after stopping the daemons and total only
  669,766 bytes. No device, ADB, install, launch, game, FPS, wattage, battery,
  or temperature measurement was used. The gain applies when titles exercise Y2R video/camera
  conversion and must not be added to unrelated dispatch, texture, audio, or Eco Turbo percentages.

## 2026-08-17 AArch64 Y2R Output Packing

- The completed eight-pixel YUV matrix path still handed every intermediate `0xRRGGBB00` word to
  scalar output-format helpers. Android AArch64 Clang partly auto-vectorized RGBA8 and RGB8, but the
  repeated bodies were 62 and 58 instructions per 32 pixels and ended in two Q-form `ST4` or `ST3`
  structured stores. RGB565 remained a 10-instruction scalar loop per pixel, while RGB5A1 remained
  13 instructions per pixel. This was the next bottleneck in the same Y2R strip pipeline.
- The AArch64 path now packs sixteen pixels explicitly. RGBA8 ORs alpha into the known-zero low byte
  of four ordinary Q-loaded intermediate vectors. RGB8 uses three compile-time-proved adjacent-
  input `TBL2` maps and ordinary Q stores; its small outlined helper keeps the three constants out
  of the repeated loop. RGB565 and RGB5A1 deinterleave sixteen `[0,B,G,R]` words with one Q-form
  `LD4`, apply exact byte masks and shifts, then use `SHLL`/`SHLL2` and paired Q stores. The scalar
  non-AArch64 path and an at-most-fifteen-pixel scalar remainder are unchanged.
- In final production ThinLTO, repeated RGB8 work is 12 instructions per sixteen pixels, RGBA8 is
  20, RGB565 is 21, and RGB5A1 is 23. Normalized to the pre-change release-object bodies, those are
  reductions of 58.6%, 35.5%, 86.9%, and 88.9%, respectively. The complete production
  `PerformConversion()` plus outlined RGB8 helper contains no `ST3` or `ST4`. These are instruction-
  count reductions in output packing, not equivalent cycle-speed ratios or whole-game gains; each
  CDMA unit also pays fixed setup/control work outside the repeated loops.
- Independent Catch2 coverage compares all four formats to a scalar byte-level reference at
  0/1/7/15/16/17/31/32/37 pixels, across 5 alpha edges and 16 channel truncation edges. It compares
  the complete output including 32-byte canaries. The ARM64 production object and test object
  compile, and both the full test ELF and `libcitra-android.so` link with ThinLTO. The hidden
  740-byte test dispatcher remains in the test ELF and is garbage-collected from the production
  library; the 104-byte RGB8 production helper remains as intended. The test executable was not
  run because device execution remains excluded.
- The final incremental `:app:buildCMakeRelWithDebInfo[arm64-v8a]` passed in 1 minute 19 seconds,
  compiling both Y2R objects and linking the ARM64 test ELF and production shared library.
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` then passed in 1 minute 48
  seconds. The ARM64-only APK is 28,970,251 bytes with SHA-256
  `497778385D6C494D94158351EA288FB3A1B1A30D1FE8C8D127FD97BBF2228CD6`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,467,481,007 logical bytes of temporary codegen objects, the test ELF/tools, native/JNI staging,
  mappings, symbols, Kotlin/R8 output, and other reproducible Gradle intermediates. Reported C:
  free space increased by 2,026,418,176 bytes.
- The implementation follows the checked X3/A715/A710/A510 load/store and table guidance already
  indexed in `docs/hardware/README.md`: avoid throughput-limited multiway structured stores on the
  A510 and confirm the compiler emitted the intended instruction forms. No device, ADB, install,
  launch, game, FPS, wattage, battery, temperature, or visual measurement was used. A future matched
  Thor A/B should target video/camera-heavy scenes and hold the standard title, cache, renderer,
  resolution, driver, performance/fan, brightness, and display-layout controls fixed.

## 2026-08-17 Direct Unrotated Linear Y2R Output

- After YUV conversion, the common `Rotation::None` plus `BlockAlignment::Linear` route still sent
  every tile through `RotateTile0()`. Its selected `linear_lut` is the exact identity, so that step
  copied each pixel into a 256-byte `tmp_tile`; `WriteTileToOutput()` immediately read the temporary
  and copied it to the final strip. A full tile therefore performed 1,024 logical bytes of
  arrangement loads plus stores even though only 512 bytes are required. The direct route saves
  512 bytes per full tile, or eight bytes per converted pixel; a 400x240 conversion avoids 768,000
  logical bytes of redundant traffic.
- The direct writer traverses output rows first and copies each 32-byte tile row into its final
  horizontal position. This is exactly the composition of the old identity remap and output copy:
  source pixel `tiles[tile][y * 8 + x]` still reaches
  `output[y * input_line_width + tile * 8 + x]`. Every rotated route and Block8x8 output retains
  the old remap, temporary, tile-order reversal where required, and write behavior.
- The pre-change Android AArch64 release object emitted a five-instruction scalar identity-scatter
  body for every pixel and a fourteen-instruction copy body for every eight-pixel row: 432 repeated
  instructions per full tile before surrounding setup and switches. Final production ThinLTO
  keeps the new writer as a 68-byte function. Its inner band is exactly a post-indexed Q-form
  `LDP`, decrement, post-indexed Q-form `STP`, and branch: 32 repeated instructions per full tile,
  plus 69 setup/control instructions amortized across a full eight-row strip. That is about 76.6%
  less arrangement work for one tile and 92.3% less for a 400-pixel/50-tile strip. These are static
  executed-instruction counts, not measured cycle speedups.
- The emitted shape follows the checked ordinary pair load/store tables on Cortex-X3 page 23,
  Cortex-A715 page 26, Cortex-A710 page 39, and Cortex-A510 page 32. It uses baseline AArch64 only,
  streams final destination rows contiguously, has no spills, and avoids trying to SIMD-accelerate
  an unnecessary intermediate pass. `PerformConversion()` grows only four bytes from `0x3140` to
  `0x3144`; the small outlined helper avoids duplicating this loop inside the already-large format
  dispatcher.
- Permanent independent coverage compares the direct writer with the old address mapping for
  zero, one, two, and three tiles; heights 1/2/7/8; exact and five-word-padded line strides; and
  sixteen-word guards on both sides. The production and test translation units compiled, and the
  complete ELF64/AArch64 Catch2 executable plus `libcitra-android.so` linked with ThinLTO in 1
  minute 22 seconds. The test name remains in the test ELF while its hidden entry point is absent
  from the production library. The ARM64 executable was not run because device execution remains
  excluded.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 1 minute
  44 seconds. The resulting package contains only `arm64-v8a`, is 28,970,039 bytes, and has SHA-256
  `FC05BB9062FF80651E4EE83A4BDCF17BCB7FE27DBC909179D21BBBE0B501EED6`. Cleanup retained that APK
  and the active ARM64 RelWithDebInfo CMake cache while removing 2,469,286,588 logical bytes of
  APK/JNI/native staging, the test ELF, codegen objects, and local Gradle cache. Reported C: free space
  increased by 2,029,838,336 bytes to 109,481,762,816. One pre-existing Java process still holds a
  bounded 669,766-byte Gradle HTML report, so it remains rather than terminating an unidentified
  process. No device, ADB, install, launch, game, FPS, battery, wattage, temperature, or visual run
  was used; the gain applies only when a title exercises unrotated linear Y2R video/camera output.

## 2026-08-17 Direct Zero-Gap 8-bit Y2R Input

- Every 8-bit Y2R input plane previously copied its incoming CDMA bytes into `data_buffer` before
  the converter reread them. With a zero inter-unit gap, the source is already one contiguous byte
  stream: the old loop copied `input[i]` to `output[i]` without transforming it. The converter now
  borrows that read-only guest pointer for the duration of the strip and reproduces the only
  externally visible state changes, advancing `ConversionBuffer::address` by the byte count and
  subtracting the same count from `image_size`.
- The shortcut is exact for `YUV422_Indiv8`, `YUV420_Indiv8`, and interleaved `YUYV422`. Each plane
  independently chooses direct or compact input. Any nonzero gap retains the old per-transfer-unit
  compaction, and both 16-bit planar formats retain their every-other-byte extraction. Conversion
  consumes the complete input strip before output arrangement or CDMA writes begin, so the borrowed
  pointer does not outlive its read-only use. The old implementation already obtained one guest
  pointer and walked it across the complete transfer, so address-contiguity assumptions do not
  change.
- Removing the staging pass saves one source read and one staging write for every input byte. At
  400x240, 4:2:0 contains 144,000 input bytes and therefore avoids 288,000 logical bytes of copy
  traffic; 4:2:2 and YUYV contain 192,000 bytes and avoid 384,000. Per eight-row strip, the bounds
  are `24 * width` and `32 * width` logical bytes respectively. This is in addition to, but must not
  be numerically added as a speed percentage to, the separate conversion, packing, and linear-output
  reductions.
- The pre-change no-LTO AArch64 release object emitted the 8-bit receive logic inside
  `PerformConversion()`, whose code size was `0x3144`. The candidate outlines one shared 136-byte
  helper and shrinks the dispatcher to `0x29e0`, 1,892 bytes or 15.0% smaller. Final production
  ThinLTO retains those exact sizes. Its zero-gap route is seven instructions: load and test `gap`,
  paired-load address/size, add, subtract, paired-store, and return. It performs no source or
  staging data load/store. A proposed hot divisibility assertion was rejected during codegen review
  because it introduced `UDIV`/`MSUB` on every plane; permanent edge coverage supplies the safety
  check without burdening production.
- Independent Catch2 coverage exercises transfer units 1/7/16/31, zero/one/two/five units, zero
  gap with an untouched staging buffer, and gaps 1/5/13 against an independent byte reference. It
  checks exact returned pointers, address/size progression, and sixteen-byte guards. Both Y2R
  objects compiled, and the complete ELF64/AArch64 test executable plus `libcitra-android.so` linked
  with ThinLTO in 1 minute 26 seconds. Both test names remain in the test ELF while the hidden test
  wrapper is garbage-collected from the production library.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 2 minutes
  50 seconds. The package contains only `arm64-v8a`, is 28,970,147 bytes, and has SHA-256
  `52A5E611D41E46F24FED8F249250F41DCCF274ABA04D5709732795158C7757BA`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,498,930,560 logical bytes of APK/JNI/native staging, the test ELF, codegen audit objects, and
  local Gradle cache. Reported C: free space increased by 2,030,465,024 bytes to 109,477,445,632.
  The pre-existing bounded 669,766-byte Gradle HTML report remains rather than terminating an
  unidentified process. No device, ADB, install, launch, game, FPS, battery, wattage, temperature,
  or visual run was used. This is a bounded video/camera memory-traffic and code-size win, not a
  measured whole-game speed or power claim.

## 2026-08-17 Fused Zero-Gap Linear Y2R Output

- The preceding unrotated-linear shortcut removed the identity tile remap, but the common zero-gap
  route still wrote four-byte `0xRRGGBB00` pixels into a contiguous strip and immediately reread
  them for final RGBA8, RGB8, RGB5A1, or RGB565 packing. The new route gathers each completed tile
  row and packs directly into the guest destination. It removes one 32-bit staging store and one
  32-bit staging load per pixel: eight logical bytes per pixel, or 768,000 bytes for 400x240.
- This is exact only for `Rotation::None`, `BlockAlignment::Linear`, and `dst.gap == 0`. Every
  rotated, Block8x8, or gapped-CDMA configuration retains the established arrangement and
  `SendData()` path. All inputs for a strip have already been consumed before the direct output
  begins, and the old implementation also committed guest output after each strip, so possible
  cross-strip source/destination overlap keeps the same ordering. Address advancement and remaining
  image size use the same `amount * bytes_per_pixel` values as the zero-gap old route.
- RGBA8 loads each eight-pixel tile row as two Q vectors, ORs the requested alpha into the
  intermediate words' known-zero byte, and uses two ordinary Q stores. RGB8 pairs two horizontally
  separated tile rows in registers and applies three compile-time-proved adjacent-input `TBL2`
  maps to produce 48 packed bytes; an odd final tile uses exact Q plus D table/store operations.
  RGB5A1 and RGB565 use one D-form byte `LD4` for the only contiguous eight-pixel tile row, preserve
  the existing high-bit truncation and alpha-bit rules, and finish with an ordinary Q store.
- This layout follows the checked Thor core tables rather than assuming every structured access is
  beneficial. Ordinary pair loads/stores are covered on Cortex-X3 page 23, Cortex-A715 page 26,
  Cortex-A710 page 39, and Cortex-A510 page 32. The `TBL2` and byte `LD4` entries are on X3 page 34,
  A715 page 37, A710 page 56, and A510 pages 46-47. The D-form `LD4` is retained because the next
  horizontal tile row is not contiguous in memory; the output side uses no `ST3` or `ST4`.
- Final production ThinLTO keeps four outlined format helpers at 188/304/208/192 bytes for
  RGBA8/RGB8/RGB5A1/RGB565. Their repeated bodies are respectively 10 instructions per eight
  pixels, 11 per sixteen, 15 per eight, and 13 per eight. Including the removed prior 32-byte row
  writer, the corresponding old repeated bodies were 14 per eight, 20 per sixteen, 31 per sixteen,
  and 29 per sixteen. The static repeated-instruction reductions are therefore 28.6%, 45.0%, 3.2%,
  and 10.3%; they are not cycle, whole-game FPS, or wattage measurements. `PerformConversion()`
  grows from `0x29e0` to `0x2aa8` (200 bytes) to select the fused route.
- Independent Catch2 coverage checks all four formats; zero, odd, and even tile counts 0/1/2/3/5;
  heights 0/1/2/7/8; alpha 0/0x80/0xff; one- and eight-pixel transfer units; sixteen channel-edge
  values; exact address/image-size progression; and 32-byte guards. The production and test Y2R
  objects compiled, and the complete ELF64/AArch64 Catch2 executable plus `libcitra-android.so`
  linked with ThinLTO in 1 minute 32 seconds. The test name is present in the test ELF while the
  hidden test wrapper is absent from the production library. The ARM64 tests were not run because
  device execution remains excluded.
- The final `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` recovery build passed
  with JDK 17 in 2 minutes 53 seconds. The resulting package contains only `arm64-v8a`, is
  28,969,511 bytes, and has SHA-256
  `DDE1A7EEF6B6DD2A0C7415E1263C584C9C685A48A36958F9BF6DF7DB0A8468F2`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,497,952,233 logical bytes of native/JNI staging, the test/tools executables, mappings, symbols,
  Kotlin/R8 output, and local Gradle state from the final build peak. Reported C: free space
  increased by 2,058,395,648 bytes to 109,470,556,160. The retained `.cxx` cache is 2,786,752,349
  bytes and `app/build` contains only the 28,969,511-byte APK. No device, ADB, install, launch,
  game, FPS, battery, wattage, temperature, or visual run was used.

## 2026-08-17 Allocation-Free Fully Direct Y2R Route

- After direct 8-bit input borrowing and direct zero-gap output packing landed, `PerformConversion()`
  still unconditionally allocated `input_line_width * 8 * 4` bytes for the shared CDMA strip and
  freed it at return. The fully direct route never read or wrote that storage. At 400-pixel width
  this was a 12,800-byte transient reservation per conversion; the valid 1024-pixel maximum is
  32,768 bytes.
- The buffer is now omitted only when output is zero-gap, unrotated, and linear and the active input
  is either YUV422/YUV420 8-bit planar with all three active gaps zero or interleaved YUYV with its
  active gap zero. Inactive-plane gaps deliberately do not matter. Both 16-bit formats, every active
  input gap, any output gap, rotation, and Block8x8 output retain the original staging behavior.
  Fixed Y/U/V staging partitions are also calculated once outside the strip loop.
- The null pointer is safe by construction: every active receive on the bypass route enters
  `PrepareInputData8()` with `gap == 0`, advances the same address and image-size fields, and returns
  guest memory before touching its compact-output argument. Conversion consumes those borrowed
  inputs before direct output starts. The output formats, tile storage, conversion math, CDMA state,
  and all fallback data paths are unchanged.
- No-LTO AArch64 codegen exposed and rejected a tempting regression during review: array
  `make_unique` value-initialized the fallback buffer and emitted a full `memset`. The retained
  `new[]` allocation is uninitialized exactly like the baseline. The baseline `PerformConversion()`
  was `0x2aa8` bytes and unconditionally called two `new[]` functions, deleted tile storage, then
  tail-called the strip-buffer `delete[]`. The candidate is `0x2c18` bytes: its eligibility gate
  branches around the first `new[]`, leaves the tile allocation, and uses `CBZ` after deleting tiles
  to skip the matching strip-buffer delete. Final production ThinLTO retains the same `0x2c18`
  size and contains no `memset` in this function.
- Independent Catch2 coverage exercises all four output formats, both 8-bit planar formats, YUYV,
  both 16-bit formats, every active and inactive input-gap distinction, all rotations, both block
  layouts, and output gap. Existing zero-gap input coverage now also passes a null staging pointer
  across transfer units 1/7/16/31 and zero/one/two/five units while checking exact returned pointer
  and CDMA state. Both modified ARM64 objects compiled, and the complete test ELF plus production
  shared library linked with ThinLTO in 1 minute 39 seconds. The test name is present in the test ELF
  while the hidden predicate hook is absent from production. The ARM64 tests were not run because
  device execution remains excluded.
- This removes allocator and lifetime work rather than a pixel-loop instruction, so the Arm core
  manuals do not provide a defensible cycle estimate. It is a bounded Y2R video/camera win, not a
  measured FPS or wattage result.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 2 minutes
  38 seconds. The package contains only `arm64-v8a`, is 28,969,947 bytes, and has SHA-256
  `D2E7BFE0351144F646529B7DBCB0E607E29F2D2CEA3DC26B99D80CBEBBC94C49`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,499,957,480 logical bytes of native/JNI staging, the test/tools executables, codegen audit
  objects, mappings, symbols, Kotlin/R8 output, and local Gradle state. The deletion pass reported
  2,060,390,400 bytes recovered, with final C: free space at 109,464,788,992 bytes. The retained
  `.cxx` cache is 2,786,934,832 bytes and `app/build` contains only the 28,969,947-byte APK. No
  device, ADB, install, launch, game, FPS, battery, wattage, temperature, or visual run was used.

## 2026-08-17 Layout-Aware Right-Eye Presentation Elision

- Command-line Git over SSH fetched upstream Azahar `fb1c1a710` (`video_core: Fix some pica
  command handling issues (#2412)`) and merged it directly into `master` as `d60d706e8`. The
  upstream fix hardens PICA LUT wrapping, out-of-range float-uniform writes, and sequential-register
  batches. The fork's AArch64 PICA paths survived the merge, and the post-merge ARM64 native build
  passed before this optimization began.
- OpenGL and Vulkan previously prepared all three presentation images whenever any frame,
  screenshot, or OpenGL frame dump was due. Normal mono-left output cannot sample the top-right
  image, and a bottom-only layout cannot sample either top image, yet the renderer still performed
  the right framebuffer surface lookup and `AccelerateDisplay()`/fallback upload path every
  qualifying presentation.
- Presentation now forms the union of the main, secondary, screenshot, and frame-dump layouts.
  Top stereo modes and explicit mono-right retain the real right-eye resolve; mono-left and
  bottom-only output alias the current left presentation image and texture coordinates into the
  still-valid right descriptor slot and skip the right-eye load. Additional-top layouts count as a
  top consumer. The right fallback texture remains configured so a later mode change is safe.
- `RightEyeDisabler` records whether the completed frame actually blocked its right-eye transfer.
  Presentation consumes that fact once, only when a render target is actually prepared, and aliases
  left even if a stereo layout is selected. A throttled/non-presented VBlank leaves it pending, so a
  later presentation cannot sample the deliberately stale right buffer. This deliberately does not
  key off the checkbox alone because the hack's per-title detection may turn itself off.
- Permanent Catch2 coverage exercises mono-left, mono-right, all six stereo modes, disabled top,
  additional top, and additional bottom layouts. The full ARM64 native target compiled both
  renderers and the new test translation unit, then linked the ELF64/AArch64 test executable and
  production `libcitra-android.so` successfully after the final lifecycle hardening in 1 minute
  29 seconds. The ARM64 tests were not run
  because device execution remains excluded.
- Final AArch64 ThinLTO disassembly retains the boolean branch in both
  `PrepareRendertarget(bool)` implementations. On the false branch, OpenGL copies the left texture
  handle plus coordinates and bypasses `LoadFBToScreenInfo`; Vulkan copies the left image view plus
  coordinates and bypasses its corresponding load. `RightEyeDisabler::ReportEndFrame()` lowers to
  compact byte tests and an OR/store for the consumed presentation state.
- This removes one right-eye surface lookup/resolve per qualifying presented frame. A CPU fallback
  would also avoid its flush and texture upload. Exact time and power saved depend on whether the
  framebuffer is accelerated and on the active display layout, so this is not a defensible
  whole-game FPS or wattage percentage without a controlled Thor A/B run.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 1 minute
  49 seconds. The package contains only `arm64-v8a`, is 28,969,603 bytes, and has SHA-256
  `7DC1E017576271CE78E71644AD7290062D7EF09D6B5544DA1AB9D23CFFBC0129`.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake cache while removing
  2,469,127,412 logical bytes of APK/JNI/native staging, the 446,510,544-byte ARM64 test ELF, helper
  executables, mappings, symbols, Kotlin/R8 output, and local Gradle state. Reported C: free space
  increased by 2,029,428,736 bytes to 109,022,683,136. The retained `.cxx` cache is
  2,781,397,898 bytes and `app/build` contains only the final APK. No device, ADB, install, launch,
  game, FPS, battery, wattage, temperature, or visual run was used.

## 2026-08-17 Asynchronous Skipped-Frame Vulkan Submission

- Upstream duplicate-frame suppression commit `8c4e8b77b` added a fallback
  `scheduler.Finish()` whenever Vulkan rendered no host screen. That condition now also covers
  Eco Turbo presentation throttling. `Finish()` submits the current command buffer and then waits
  for its pre-submit timeline tick, so every duplicate or Eco-Turbo-skipped VBlank serialized the
  emulation thread with GPU completion even though the CPU did not consume a rendered result.
- The no-presentation fallback now uses `scheduler.Flush()`. It records the same render-pass end,
  advances the same timeline value, dispatches the same command chunk, and submits the same Vulkan
  command buffer, but does not call `MasterSemaphore::Wait()`. The graphics queue therefore keeps
  guest work ordered while the CPU and Adreno can overlap it.
- Lifetime safety does not depend on the removed wait. Command buffers and descriptor sets are
  tagged with `CurrentTick()` and reused only after known GPU completion; stream-buffer wrap calls
  `Scheduler::Wait()` for the exact recorded watch; and rasterizer garbage collection deletes a
  sentenced surface only after the completed tick advances beyond its sentence tick. Stale-low
  completion merely delays reuse or deletion. Explicit `Finish()` calls remain for screenshot CPU
  readback, resized render-frame recreation, presentation-window destruction, and renderer teardown.
- The full `arm64-v8a` native target passed in 1 minute 43 seconds, compiling the changed Vulkan
  renderer and linking both the ELF64/AArch64 Catch2 executable and production
  `libcitra-android.so`. The final ThinLTO `RendererVulkan::SwapBuffers()` is `0x408` bytes. Its
  `screenRendered == false` branch calls `Scheduler::SubmitExecution()` directly (inlined
  `Flush()`), with no call to `Scheduler::Finish()` or `MasterSemaphore::Wait()`. The later
  `WaitWorker()` drains CPU command recording only; it is not a GPU-completion wait.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 2 minutes
  46 seconds. The APK contains only `arm64-v8a`, is 28,970,191 bytes, and has SHA-256
  `C41B14B848561271869A977748E248B8653C0782E23064179777BFCA606520C5`.
- This removes exactly one GPU-completion wait from every Vulkan `SwapBuffers()` that performs no
  host presentation. The actual saved time ranges from nearly zero when Adreno is already caught
  up to the outstanding GPU backlog when it is not. No device, ADB, install, launch, game, FPS,
  battery, wattage, temperature, or visual run was used, so this is a proven synchronization
  removal and a strong power/overlap candidate rather than a measured whole-game percentage.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake/Ninja cache while removing
  2,471,517,253 logical bytes of Gradle staging, the ARM64 test ELF, and native helper executables.
  R8 held one generated `classes.dex` open until the Gradle daemons were stopped; the exact second
  pass removed it. Reported C: free space increased by 2,030,428,160 bytes to 109,023,707,136.
  The retained `.cxx` cache is 2,781,525,815 bytes and `app/build` contains only the final APK.

## 2026-08-17 Native Vulkan Frame-Worker Overlap

- `RasterizerVulkan::TickFrame()` inherited an unconditional `scheduler.WaitWorker()` from
  Azahar commit `e8c75b410` (`libretro: vulkan: wait before ticking`). At that time rasterizer-cache
  garbage collection used frame age, so an old surface could be destroyed while a lagging worker
  command still referenced it. The later upstream completion-tick conversion in `b34de55b5`, plus
  this fork's corrected strict comparison in `dcd3a58a0`, removed that lifetime dependency:
  sentenced resources are retained while `completed_tick <= retirement_tick` and deleted only
  after completion advances beyond their tick.
- Native threaded presentation had one additional ordering dependency on the frame-end join. It
  records the present-queue callback after `Flush()` dispatches the render submission. The callback
  is now explicitly dispatched behind that submission in the scheduler's FIFO, so the presentation
  thread is notified after `vkQueueSubmit` without forcing the emulation thread to wait for worker
  completion. The synchronous presentation fallback keeps its explicit `WaitWorker()`, and LibRetro
  keeps the original wait before its cache tick.
- Removing the join lets the producer start the next frame while prior worker lambdas execute.
  Presentation clear color is therefore captured by value rather than read through mutable
  renderer state. Descriptor updates still finish on the producer before queuing; command and
  descriptor pools remain timeline-tagged; stream-buffer wrap waits its exact watch tick; resize,
  readback, window destruction, and renderer teardown retain explicit synchronization.
- A focused constexpr regression covers completion older than, equal to, and newer than a resource
  retirement tick. The full `arm64-v8a` native build compiled and linked that test plus production
  `libcitra-android.so` successfully in 1 minute 29 seconds. The ARM64 test executable was not run
  on this x64 host because device use remains forbidden.
- Linked AArch64 inspection shows `RasterizerVulkan::TickFrame()` is 12 bytes and branches directly
  to `RasterizerCache::TickFrame()` with no `Scheduler::WaitWorker()` call. Threaded
  `PresentWindow::Present()` ends in a tail call to `Scheduler::DispatchWork()`; its only linked
  `WaitWorker()` branch is the preserved non-threaded fallback.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 2 minutes
  23 seconds. The APK contains only `arm64-v8a`, is 28,969,839 bytes, and has SHA-256
  `A2DC7360808C53E871C66D9CCF35E0A2C0D570B7761DE7C13F33C86154C9D8E1`.
- This removes exactly one CPU-side Vulkan worker join from every normal native threaded frame and
  permits command recording/submission to overlap the next emulation-frame setup. The saved time
  depends on worker backlog and the title's CPU/GPU balance. No device, ADB, install, launch, game,
  FPS, battery, wattage, temperature, or visual run was used, so no whole-game percentage is
  claimed.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake/Ninja cache while removing
  2,471,565,068 logical bytes of Gradle staging, the ARM64 test ELF, and native helper executables.
  Reported C: free space increased by 2,025,144,320 bytes to 100,816,732,160. The retained `.cxx`
  cache is 2,781,916,118 bytes and `app/build` contains only the final APK.

## 2026-08-17 Single-Dispatch Vulkan Presentation Handoff

- After the native frame-end join was removed, normal threaded presentation still used two worker
  chunks per frame. `RenderToWindow()` called `Flush(frame->render_ready)`, which recorded and
  dispatched the Vulkan submission; `PresentWindow::Present()` then recorded a second frame-queue
  callback and called `DispatchWork()` again. That second dispatch repeated the descriptor
  `on_dispatch` callback, scheduler queue push/pop, `queue_mutex`, `event_cv.notify_all()`, reserve
  chunk acquisition/recycling, and worker execution-lock handoff without recording more GPU work.
- `Scheduler::FlushWithCallback()` now records one typed command containing both operations. It
  performs the exact existing timeline-tick preparation and `MasterSemaphore::SubmitWork()` first,
  releases `submit_mutex`, then runs the supplied callback before the submitted chunk is recycled.
  `PresentWindow` owns both the threaded combined route and the synchronous `Flush()` plus
  `WaitWorker()` route, so callers cannot accidentally separate submission from presentation.
- The ordering remains strict: the present callback cannot expose `frame->render_ready` before its
  signal submission has reached `vkQueueSubmit`; the presentation thread's copy waits on that
  binary semaphore; descriptor updates still flush on the producer before the combined chunk is
  queued; and the chunk retains its submit marker so the worker allocates a fresh render command
  buffer afterward. If the current 32 KiB command chunk lacks 56 bytes for the combined command,
  the unchanged capacity fallback first dispatches prior work and records the combined operation
  in a fresh chunk.
- Both the worker-to-present frame notification and the present-to-render free-frame notification
  now release their predicate mutex before `notify_one()`. The queue mutation remains protected,
  and predicate testing under the same mutex prevents lost wakes, while the awakened thread no
  longer immediately contends on a lock deliberately held by the notifier. The separate
  queue-to-swapchain lock exchange in `PresentThread()` is ordering-sensitive and remains unchanged.
- The final full `arm64-v8a` native rebuild compiled and ThinLTO-linked the test ELF and production
  `libcitra-android.so` successfully in 1 minute 23 seconds. Linked AArch64 has one 56-byte
  `FlushWithCallback` typed command whose `0x118`-byte execute body locks/submits/unlocks, then
  locks/pushes/unlocks and tail-notifies. The capacity-fit threaded `Present()` path has one final
  `Scheduler::DispatchWork()`; `WaitWorker()` remains only on its synchronous branch.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 1 minute
  50 seconds. The APK contains only `arm64-v8a`, is 28,970,707 bytes, and has SHA-256
  `43873337BB20AB212810E8536ECD275EF7469872875B98806E172532AD6745EA`.
- This removes one complete CPU scheduler dispatch and two notify-under-lock handoffs from every
  normal native threaded Vulkan frame. It should reduce scheduler CPU time, wakeup/lock traffic,
  and presentation handoff latency, but no device, ADB, install, launch, game, FPS, battery,
  wattage, temperature, or visual run was used, so no whole-game percentage is claimed.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake/Ninja cache while removing
  2,471,604,766 logical bytes of Gradle staging, the ARM64 test ELF, and native helper executables.
  Reported C: free space increased by 2,025,955,328 bytes to 88,974,700,544. The retained `.cxx`
  cache is 2,782,171,675 bytes and `app/build` contains only the final APK.

## 2026-08-17 Vulkan Isotropic Sampler Fidelity and Power

- The initial Vulkan backend commit `dfa2fd0e0` enabled the physical device's maximum supported
  anisotropy on every guest texture sampler, even though PICA sampler state exposes only
  nearest/linear magnification, nearest/linear minification, nearest/linear mip selection, LOD
  bounds/bias, and wrap modes. There is no guest anisotropy control in `regs_texturing.h` or
  `SamplerParams`, and the OpenGL backend does not add anisotropy. Vulkan therefore changed the
  emulated filter and potentially added texture work that the game never requested.
- Vulkan's two final-screen samplers also requested device-maximum anisotropy for both the linear
  and nearest choices. The current Khronos
  [Vulkan sampling specification](https://docs.vulkan.org/spec/latest/chapters/textures.html) makes
  nearest filtering with anisotropy implementation-dependent, so the old nearest screen choice did
  not have deterministic nearest semantics across drivers.
- Guest and final-screen Vulkan samplers now set `anisotropyEnable = false` and
  `maxAnisotropy = 1.0f`. Guest nearest/linear, mip, LOD, wrap, border-color, and comparison state is
  otherwise unchanged. The device feature remains enabled when supported, so a future explicit and
  measured option can still use it without changing device creation.
- Qualcomm Adreno Game Developer Guide 80-78185-2 AL page 84 recommends minimizing texture fetches
  and cache misses and notes that bilinear or nearest can outperform trilinear or anisotropic
  filtering. Its upper bound is sixteen samples for a 16x anisotropic lookup on an affected
  fragment, while adaptive behavior usually makes average application cost much lower and commonly
  under twice isotropic filtering. This change removes that unrequested adaptive work; it does not
  assume every old sample consumed sixteen taps.
- A complete `arm64-v8a` native rebuild passed in 1 minute 40 seconds and linked both the ARM64 test
  ELF and production `libcitra-android.so`. Final AArch64 for `Vulkan::Sampler::Sampler` writes zero
  to the `VkSamplerCreateInfo::anisotropyEnable` word and `0x3f800000` (`1.0f`) to
  `maxAnisotropy`. `RendererVulkan::CompileShaders()` emits the same field pair for both final
  presentation samplers, including the nearest sampler.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed with JDK 17 in 3 minutes
  48 seconds. The APK contains only `arm64-v8a`, is 28,970,223 bytes, and has SHA-256
  `6C21171E5534618D9D96DB5E3E47E9B2F114912F4B8E1563C918C46C9EE188FE`.
- This affects every Vulkan guest texture sampler plus the final screen samplers, so it is a broader
  texture-pipe and power candidate than a one-per-frame CPU synchronization micro-optimization.
  Exact benefit depends on texture angle, minification, cache behavior, scene, and driver. No
  device, ADB, install, launch, game, FPS, battery, wattage, temperature, or visual run was used, so
  no whole-game percentage is claimed. A future allowed matched Thor A/B should hold title, scene,
  save, caches, driver, resolution, layout, performance/fan mode, brightness, and duration fixed;
  compare image correctness and record GPU texture-pipe utilization, frametimes, battery power,
  temperature, and thermal slope.
- Cleanup retained that APK and the active ARM64 RelWithDebInfo CMake/Ninja cache while removing
  2,471,546,637 logical bytes of Gradle staging, the ARM64 test/helper bin directory, and native
  helper tools. Reported C: free space increased by 2,024,697,856 bytes to 88,759,250,944. The
  retained `.cxx` cache is 2,782,216,533 bytes and `app/build` contains only the final APK.

## 2026-08-17 AArch64 PICA Vertex-Cache Lookup

- `PicaCore::LoadVertices()` keeps a fully associative 64-entry circular cache for indexed draws
  that reach CPU-side vertex processing. The baseline AArch64 ThinLTO loop tested a valid byte and
  then one `u16` ID at a time, advancing through 256-byte attribute records. A fully valid miss
  repeated about nine instructions for each of 64 entries, roughly 579 lookup instructions with
  setup and exit. Draws that succeed through hardware vertex acceleration bypass this path.
- The replacement-state proof allows the valid-byte array to disappear. Before the cache fills,
  misses insert sequentially and `[0, vertex_cache_count)` is exactly the valid prefix; hits do not
  advance either count or position. Once count reaches 64, every slot is valid and the original
  circular replacement order continues. Searching only that prefix therefore preserves the old
  fully associative behavior, including the first matching slot if duplicate IDs ever occur.
- AArch64 now compares sixteen IDs per band. Final ThinLTO uses `LDP Q`, two `.8h` `CMEQ`, `UZP1`
  to form the byte mask, `ORN` to select lane indices or `0xff`, and one `.16b` `UMINV` to recover
  the first match. The compiler's `UZP1`/`ORN` are exact simplifications of the source
  `XTN`/`XTN2` and `BSL`; there is no lookup spill. A complete 64-entry miss executes 74 lookup
  instructions including setup and exit instead of about 579, an 87.2% path-local reduction.
  `LoadVertices()` grows from 1,344 to 1,436 bytes, a 92-byte or 6.8% code-size tradeoff.
- This shape follows the checked Snapdragon 8 Gen 2 core manuals. Ordinary vector loads are listed
  on Cortex-X3 page 23, A715 page 26, A710 page 39, and A510 page 32; integer reductions on pages
  26, 29, 43, and 36 respectively; and the select/narrow operations on X3 pages 31-32, A715 pages
  34-35, A710 pages 52-53, and A510 pages 43-44. One reduction covers a broad sixteen-ID band,
  avoiding repeated horizontal dependencies on the reduction-sensitive A510 cores.
- Permanent Catch2 coverage compares the helper with an independent scalar first-match loop for
  all 65 valid-prefix lengths and all 65,536 `u16` values, then checks duplicate and miss cases.
  The full ARM64 build compiled and ThinLTO-linked both the test ELF and production library. A
  stripped 25,849,176-byte test executable was pushed over USB to AYN Thor `c3ca0370`; the focused
  test passed all two assertions in one case in about 250 ms of host-observed time. The device and
  host temporary binaries were removed immediately afterward.
- `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed in 10 minutes 58 seconds.
  This was an accidentally clean 2,199-action native rebuild after two local Ninja versions reset
  the generated build log, not evidence that the source change itself requires a full rebuild.
  After commit `0bcb6a8e8`, an incremental version-stamped build passed in 1 minute 32 seconds. The
  final APK is ARM64-only, 28,970,267 bytes, v2-signed with the test certificate, reports
  `0bcb6a8e8-vanilla-thor`, and has SHA-256
  `6C30FC4673EE74E3C6A5236D27EA2C8E519EEF05C5001B700E4E83C026488A63`.
- The final APK installed successfully over `org.azahar_emu.azahar.debug` on USB Thor `c3ca0370`.
  Package inspection reports `primaryCpuAbi=arm64-v8a` and the expected version; the package was
  explicitly force-stopped afterward. No game or app UI was launched and no FPS, battery power,
  temperature, or sustained-speed measurement was taken. A matched A/B should use an indexed,
  CPU-vertex-fallback-heavy scene and hold title, save, caches, renderer, driver, resolution,
  layout, performance/fan mode, brightness, and duration fixed before assigning a whole-game or
  wattage benefit.

## 2026-08-17 Per-Game Cached-Data Manager

- Android's game long-press sheet now exposes **Manage Cached Data** instead of a backend picker
  labeled only as shader-cache deletion. The manager is keyed by the selected title ID and reports
  separate human-readable Vulkan and OpenGL shader-cache totals before offering either action.
- Native size accounting covers the same persistent per-title files as the existing deletion
  paths: OpenGL separable/conventional precompiled binaries and its transferable binary; Vulkan
  vertex, fragment, geometry, and pipeline transferable files plus matching pipeline-cache files.
  Filesystem work runs on `Dispatchers.IO`, not the Android UI thread.
- Each backend opens a second confirmation naming the game, backend, and measured size and warns
  that the next run may stutter while the cache rebuilds. Downloaded custom textures remain user
  content managed through **Open › Textures Folder** and are not included in or deleted by this
  manager.
- The dialog explains the distinct lifetime and cost models: a texture-filter result stays in its
  rasterizer surface's scaled GPU image until guest invalidation or upload and clears with the game
  session, while screen-filter Anime4K processes the changing final presentation each frame. This
  avoids promising a disk cache whose hashing, I/O, synchronization, VRAM duplication, and stale
  invalidation costs have not shown a Thor power or speed benefit.
- The dirty-tree release-style ARM64 build passed in 1 minute 37 seconds. After commit
  `38cecd56c`, the incremental version-stamped build passed in 1 minute 23 seconds. The final APK is
  ARM64-only, 28,975,708 bytes, v2-signed with the test certificate, reports
  `38cecd56c-vanilla-thor`, and has SHA-256
  `C86C1A5B4CCA4A061D8FC6C4D7CFF1B5BD3246ECA9363186874F626DDDC22395`.
- The final APK installed successfully on USB Thor `c3ca0370`; package inspection reports
  `primaryCpuAbi=arm64-v8a` and the expected version. In the live library UI, long-pressing 7th
  Dragon III exposed the manager and reported a 1.41 MB Vulkan cache and 225 kB OpenGLES cache.
  Opening the Vulkan action showed the expected title/backend/size warning; it was canceled without
  confirming deletion. Azahar was force-stopped and all temporary UI hierarchy dumps were removed.
  No game was launched and no cache or custom texture was deleted.
- A follow-up through wireless ADB `192.168.1.33:5555` identified the same device as `AYN Thor`
  and reconfirmed the installed ARM64 ABI and `38cecd56c-vanilla-thor` version before force-stop.
  `dumpsys battery` reported AC powered true, USB and wireless powered false, charging status, and
  21% battery. Use Wi-Fi ADB for subsequent Thor work as requested; do not interpret wall-powered
  runs as battery-discharge watt measurements.
- Post-verification cleanup removed 2,470,040,421 logical bytes of Gradle intermediates, native/JNI
  staging, reports, local Gradle state, and the 445,571,808-byte test ELF. The final APK plus its
  476-byte metadata and the active 2,785,959,354-byte ARM64 RelWithDebInfo CMake/Ninja cache remain;
  the final C: free-space check reported 87,739,273,216 bytes.

## 2026-08-17 AArch64 PICA Matching-Lane Compare

- PICA `CMP` writes two conditional-code booleans from the X and Y components. The old AArch64 JIT
  always emitted two scalar `FCMP`/`CSET` pairs plus two lane moves: six instructions after source
  swizzling, even when both lanes selected the same comparison operation.
- Matching operations now use one vector `FCMEQ`, `FCMGT`, or `FCMGE`, move the low 64-bit mask to
  a general register, and extract lane-zero and lane-one sign bits. Equal, less/less-equal, and
  greater/greater-equal therefore use four instructions; `NotEqual` uses five because it inverts
  the equality mask. Mixed operators retain the old scalar path.
- This preserves PICA's prior ordered comparison behavior. An unordered/NaN operand produces false
  for equality and ordered relational operations; inverted equality makes `NotEqual` true. New
  regression coverage exercises all six operations with less, greater, equal, and NaN inputs in
  both the interpreter and AArch64 JIT.
- The checked Cortex-X3, A715, A710, and A510 manuals list AdvSIMD floating compares on pages 28,
  30, 46, and 39 respectively. The operation is available across all Thor core classes rather than
  relying on an optional X3-only extension. Documented compare latency is two cycles on X3/A715/A710
  and three on A510.
- A clean 2,199-action ARM64 native build passed and linked the test ELF and production library.
  Over Wi-Fi ADB `192.168.1.33:5555`, AYN Thor ran the exact `PICA State Access` interpreter and JIT
  cases with 39/39 assertions passing in each. A broader `[shader]` run had 49 of 50 cases pass and
  exposed the existing unrelated `LG2 - ShaderJitTest` mismatch; this change's focused suites pass.
  The stripped device test binary was removed from both host and Thor immediately afterward.
- Commit `2e26caa9f` was pushed before the final build. The release-style
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build passed in three minutes.
  The ARM64-only, v2-signed APK is 28,975,540 bytes, reports
  `2e26caa9f-vanilla-thor`, and has SHA-256
  `3BA2EFB83034ECF1770F5F27E7C1D26CD9D28846EE1FBD909C68EC409167B903`.
- The APK installed successfully over `org.azahar_emu.azahar.debug` on the same Wi-Fi Thor and
  reports `primaryCpuAbi=arm64-v8a`. The device was AC-powered at 52% battery; Azahar was
  force-stopped before and after installation, and no app UI or game was launched. This is a local
  generated-instruction reduction, not a measured whole-game FPS or battery-watt improvement.
- Post-verification cleanup removed 2,022,138,323 logical bytes of Gradle/native staging, test
  output, and rendered manual-page PNGs. `app/build` now contains only the 28,975,540-byte APK and
  its 476-byte metadata; the 3,229,579,753-byte active ARM64 RelWithDebInfo CMake/Ninja cache is
  retained. C: reported 87,276,048,384 bytes free after cleanup.

## 2026-08-17 AArch64 PICA LG2 Signed Exponent Repair

- The complete ARM64 shader suite exposed an old x86-to-AArch64 porting error in the PICA `LG2`
  helper. After extracting the IEEE-754 exponent and subtracting bias 127, AArch64 moved the signed
  bits into a SIMD lane and executed unsigned `UCVTF`. For `0.5`, exponent `-1` was therefore
  interpreted as `0xffffffff` and converted to `4294967296.0`; x64 correctly uses signed
  `cvtsi2ss`.
- AArch64 now converts the unbiased 32-bit GPR exponent directly with scalar `SCVTF`. This restores
  negative exponents and replaces the old `MOV` plus `UCVTF` pair with one instruction, removing
  one generated instruction from every normal positive-input `LG2` helper execution. Polynomial
  coefficients, Horner/FMA order, mantissa reduction, and NaN/zero/negative branches are unchanged.
- The Cortex-X3, A715, A710, and A510 software optimization guides list signed and unsigned FP
  conversion forms in the conversion tables spanning pages 28-29, 30-31, 46-47, and 39-40
  respectively. This is baseline hardware available on every Thor core class; the signed form is
  required by the algorithm rather than an optional X3-only acceleration.
- Permanent regression coverage now checks every exact power-of-two exponent from `-32` through
  `+32`, plus the existing NaN, negative, zero, fractional-mantissa, and large-value cases. On the
  wall-powered Wi-Fi Thor, the exact interpreter and JIT cases each passed 70 assertions, and the
  complete `[shader]` suite passed all 2,276 assertions across 50 test cases. The prior LG2 failure
  is gone; the 25,852,440-byte stripped test ELF was removed from host and device immediately.
- The ARM64 native compile/link passed in 1 minute 17 seconds. Commit `7dd086fad` was pushed before
  the final `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build, which passed in
  2 minutes 1 second. The ARM64-only, v2-signed APK is 28,974,880 bytes, reports
  `7dd086fad-vanilla-thor`, and has SHA-256
  `55404C7006CAD4AEDF225C5AE641BC03BF31E1CA8B1C595BBB4318B27EC97242`.
- The APK installed successfully over `org.azahar_emu.azahar.debug` through Wi-Fi ADB
  `192.168.1.33:5555` and reports `primaryCpuAbi=arm64-v8a`. The device reported AC power, no USB
  power, and 71% battery. Azahar was force-stopped before and after installation; no UI or game was
  launched, so this is correctness plus a local one-instruction reduction, not a whole-game FPS or
  wattage measurement.
- Post-verification cleanup removed 2,022,514,627 logical bytes of Gradle/native staging and manual
  render PNGs. `app/build` now contains only the APK and its 476-byte metadata; the
  3,229,693,469-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains. C: reported
  87,055,941,632 bytes free after cleanup.

## 2026-08-17 AArch64 PICA Reciprocal-Square-Root Refinement

- The x64 PICA shader backend implements `RCP` and `RSQ` with approximate host instructions, while
  AArch64 used exact scalar `FDIV` for `RCP` and exact `FSQRT` followed by `FDIV` for `RSQ`. The
  Cortex-X3, A715, and A710 timing tables list F32 divide/square-root latency at 7-10 cycles,
  reciprocal estimates at three, and refinement steps at four; A510 lists divide at 13, square root
  at 12, and estimate/refinement/multiply at four cycles.
- A temporary ARM64 Android benchmark compared exact operations with one- and two-step hardware
  estimate sequences over 8,388,608 operations, taking the best of five runs with `FPCR=0`. Pinned
  results for exact versus one-step `RSQ` were 8.1428 versus 5.9943 ns/op on CPU 0 (26.4% faster),
  1.4515 versus 0.8241 on CPU 3 (43.2%), 1.3848 versus 0.8300 on CPU 5 (40.1%), and 0.6619 versus
  0.5547 on CPU 7 (16.2%). One-step `RCP` was slower on all four cores, and two-step sequences were
  slower than exact, so `RCP` stays exact and `RSQ` uses exactly one refinement.
- Over 1,000,000 positive-normal values, the selected `RSQ` sequence had maximum relative error
  `1.6128e-5` and maximum error 195 ULP. Positive and negative zero, positive infinity, negative
  infinity, NaN, and negative finite inputs retained the prior result classes and bit patterns.
  Squaring the estimate before `FRSQRTS` is intentional: rearranging the operand as input times
  estimate would disturb the architecture's infinity-times-zero special handling.
- The AArch64 JIT now emits scalar `FRSQRTE`, squares the estimate, applies `FRSQRTS` with the
  original source, and performs the final `FMUL` before broadcasting the lane. Permanent tests cover
  zero plus 8,000 dense positive-normal inputs across exponents `-62..62`; both template backends
  therefore execute 16,000 dense assertions.
- A fresh 2,199-action ARM64 native build passed in 11 minutes 12 seconds. Over Wi-Fi ADB
  `192.168.1.33:5555`, the focused `RSQ*` suite passed all 16,028 assertions in two cases, and the
  complete `[shader]` suite passed all 18,278 assertions in 50 cases. The stripped test executable
  was removed from both host and device. Source commit `10a238446` was pushed to `master`.
- Final release-style packaging passed in 2 minutes 54 seconds. The ARM64-only, v2-signed APK is
  28,975,596 bytes, reports `10a238446-vanilla-thor`, and has SHA-256
  `1A9BD9C26782526D7F5D39FD8EDF8E8F432226EB99D93D8AB4241F82FDABA028`. It installed successfully
  over `org.azahar_emu.azahar.debug` and reports `primaryCpuAbi=arm64-v8a`; Azahar was force-stopped
  before and after installation, and no app UI or game was launched. The Thor reported USB power,
  no AC or wireless power, 80% battery, 4.214 V, and 25.0 C. USB power is not a battery-discharge
  watt measurement.
- Post-verification cleanup removed 2,024,213,566 logical bytes of Gradle/native staging, native
  helper binaries, and the local Gradle cache. `app/build` now contains only the 28,975,596-byte APK
  and its 476-byte metadata; the 3,224,935,167-byte active ARM64 RelWithDebInfo CMake/Ninja cache
  remains. C: reported 86,472,650,752 bytes free after cleanup.
- These are local operation timings and correctness results, not a whole-game FPS or power claim.
  A matched game A/B is still required with title, save, caches, renderer, driver, resolution,
  layout, performance/fan mode, brightness, and duration held constant.

## 2026-08-17 AArch64 PICA DP3 and MOVA Narrowing

- The remaining arithmetic audit found two x64-originated AArch64 costs. `MOVA` converted all four
  float lanes with Q-form `FCVTZS` even though the PICA instruction can consume only X/Y. `DP3`
  zeroed W through a general-register-to-vector lane insertion, then serialized two pairwise adds.
  The x64 backend instead groups the live dot product as `(X + Y) + Z`.
- The Cortex-X3 guide lists normal/pairwise FP add at two-cycle latency and D-form F32 versus Q-form
  F32 conversion at three versus four cycles on pages 28-29. A715 lists normal `FADD` at two versus
  pairwise `FADDP` at three and D/Q conversion at three/four on pages 30-31. A710 lists add forms at
  two and D/Q conversion at three/four on pages 46-47. A510 lists add forms at four on page 39; its
  conversion table spans pages 39-40. These are baseline AdvSIMD operations on every Thor core.
- A self-contained ARM64 benchmark executed 67,108,864 independent conversions and 33,554,432
  four-way interleaved DP3 reductions per sample, taking the best of five runs. Q-form versus D-form
  `FCVTZS` measured 0.501534 versus 0.250587 ns/op on CPU 0, 0.741592 versus 0.370985 on CPU 3,
  0.741922 versus 0.370354 on CPU 5, and 0.339053 versus 0.169341 on CPU 7: essentially 2.00x
  throughput on every core class. Current versus dependency-shortened `DP3` measured 6.948113
  versus 5.391890 ns/op on CPU 0 (22.4% faster), 1.112044 versus 0.926644 on CPU 3 (16.7%),
  1.233326 versus 0.926003 on CPU 5 (24.9%), and 0.612165 versus 0.452924 on CPU 7 (26.0%). The
  benchmark executable was removed from host and device, and rendered manual pages were removed
  from the host.
- AArch64 `MOVA` now emits D-form `.2S` `FCVTZS`, extracts the low 64-bit pair once, and preserves
  the existing per-lane sign extension and destination masks. `DP3` keeps the four-lane sanitized
  multiply, forms X+Y in a scratch scalar while broadcasting Z independently, performs one scalar
  add, and broadcasts the result. W is never part of the reduction, and no FMA or reassociation is
  introduced. New tests cover both MOVA lanes, negative/fractional truncation, ignored exceptional
  Z/W inputs, untouched loop state, DP3 result broadcast, and NaN W inputs.
- The complete ARM64 native target compiled and linked the test ELF plus production library in
  1 minute 48 seconds. On the Wi-Fi Thor, `DP3*` passed 8 assertions in two interpreter/JIT cases,
  `PICA State Access*` passed 96 assertions in two cases, and the full `[shader]` suite passed all
  18,298 assertions in 50 cases. Every earlier zero-match filter invocation was explicitly discarded
  rather than counted. Source commit `20687daae` was pushed to `master`.
- Final release-style packaging passed in 3 minutes 19 seconds. The ARM64-only, v2-signed APK is
  28,975,592 bytes, reports `20687daae-vanilla-thor`, and has SHA-256
  `7F2FA912E6F4DAD6EFBC25417A0E858C2A5B8E956D9712FE9EA8C0B117315A3A`. It installed successfully
  over `org.azahar_emu.azahar.debug`, reports `primaryCpuAbi=arm64-v8a`, and remained force-stopped;
  no Azahar UI or game was launched. At final install the Thor reported USB power, no AC/wireless
  power, 80% battery, 4.228 V, and 22.0 C, which is not a battery-discharge watt measurement.
- Post-verification cleanup removed 2,023,799,217 logical bytes of Gradle/JNI/native staging, native
  helper binaries, and the local Gradle cache. `app/build` retains only the 28,975,592-byte APK and
  its 476-byte metadata; the 3,225,184,280-byte active ARM64 RelWithDebInfo CMake/Ninja cache
  remains. C: reported 86,234,206,208 bytes free after cleanup.
- These are isolated JIT-operation measurements, not whole-game FPS or wattage. A matched title,
  scene, cache, renderer, driver, resolution, layout, mode, fan, brightness, and duration A/B is
  still required before attributing a sustained system-level gain.

## 2026-08-17 AArch64 PICA Partial MOVA Extraction

- The narrowed AArch64 `MOVA` still converted X/Y with one D-form `.2S` `FCVTZS`, but partial
  X-only and Y-only masks then transferred the complete low 64-bit pair to a GPR before a separate
  `SXTW` or `ASR`. A signed element transfer can select either 32-bit lane and sign-extend it into
  the destination GPR in one instruction, removing one generated instruction from every partial
  `MOVA` while preserving the existing truncating conversion.
- The Cortex-X3, A715, and A710 guides list element-to-GPR `UMOV`/`SMOV` at two-cycle latency and
  one-per-cycle throughput on pages 32, 35, and 53 respectively. The A510 guide lists three-cycle
  latency and one-per-cycle throughput on page 44. This is baseline AdvSIMD functionality on all
  Snapdragon 8 Gen 2 core classes, not an X3-only extension.
- A disassembly-checked ARM64 benchmark compared the exact generated sequences over 67,108,864
  partial conversions per sample, alternating A/B order across ten rounds and taking each best
  result. Current versus direct-`SMOV` X extraction measured 2.392305 versus 0.879373 ns/op on CPU 0
  (63.24% faster), 0.463409 versus 0.463997 on CPU 3 (effectively tied), 0.463285 versus 0.370489 on
  CPU 5 (20.03%), and 0.338756 versus 0.338707 on CPU 7 (tied). Y results were 2.399199 versus
  0.882965 (63.20%), 0.463819 versus 0.463686 (tied), 0.463347 versus 0.370460 (20.05%), and
  0.338737 versus 0.338700 ns/op (tied).
- Replacing the packed XY extraction with two `SMOV`s was rejected. Although it helped A510, it was
  26.10% slower on CPU 3, 71.45% slower on CPU 5, and 97.46% slower on CPU 7 because two element
  transfers contend for the documented transfer path. The shipped hybrid therefore uses one
  `SMOV` for X-only or Y-only and retains one packed transfer plus `SXTW`/`ASR` for XY.
- Correctness is exact for the selected path: `SMOV Xd, Vn.S[lane]` produces the same signed
  32-to-64-bit value as the removed packed `UMOV` plus lane extraction. Disabled address registers
  remain untouched. Permanent coverage now includes an explicit negative Y-only case alongside
  X-only preservation, XY truncation, exceptional ignored Z/W inputs, and initial-state checks.
- The full ARM64 native build passed in 1 minute 37 seconds. On Wi-Fi ADB `192.168.1.33:5555`, the
  focused `PICA State Access*` suite passed all 102 assertions in two interpreter/JIT cases and the
  complete `[shader]` suite passed all 18,304 assertions in 50 cases. The 25,862,424-byte stripped
  test ELF and the 8,536-byte benchmark were removed from both host and Thor immediately. Source
  commit `ef555210d` was pushed to `master` before packaging.
- Release-style packaging passed in 2 minutes 55 seconds. The ARM64-only, v2-signed APK is
  28,975,900 bytes, reports `ef555210d-vanilla-thor`, and has SHA-256
  `7474747FA816752AD669E2E7017AFE55759CAC0EEC2A39ADB8623F5D06558EE3`. It installed successfully
  over `org.azahar_emu.azahar.debug`, reports `primaryCpuAbi=arm64-v8a`, and remains force-stopped;
  no UI or game was launched. The Thor reported USB power, no AC/wireless power, 80% battery,
  4.211 V, and 23.0 C, so this is not a battery-discharge watt measurement.
- Post-verification cleanup removed about 2.02 GB of Gradle/JNI/native staging plus the repo-local
  Gradle cache and rendered manual pages. `app/build` retains only the APK and its 476-byte metadata;
  the 3,225,378,046-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains. C: reported
  86,221,090,816 bytes free after cleanup.
- This is an isolated generated-instruction and throughput improvement, not a whole-game FPS or
  wattage result. A matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B remains required before claiming a sustained system-level gain.

## 2026-08-17 AArch64 PICA Conditional Flow

- The AArch64 PICA condition evaluator still inherited an x64-shaped boolean-materialization
  strategy. OR inverted up to two canonical condition flags into scratch registers, combined them,
  then compared with zero, taking two to four generated instructions. AND used one to three.
  Every caller then assumed that guest-true was host `NE`, even though the truth tables can be
  represented directly by other AArch64 condition codes.
- `COND0` and `COND1` are canonical zero/one values: shader entry loads their C++ `bool` bytes,
  same-operation `CMP` extracts single bits, and mixed comparisons use `CSET`. The sixteen OR/AND
  reference/input combinations therefore reduce exactly to one flag-setting instruction. OR uses
  `CMN/NE` for refs 1/1, `TST/EQ` for 0/0, and `CMP/GE` or `CMP/LE` for mixed refs. AND uses
  `TST/NE`, `CMN/EQ`, or `CMP/GT/LT`. JustX/JustY compare the selected flag with its reference and
  return `EQ`. IFC and CALLC branch on the inverse returned condition; BREAKC and JMPC use it
  directly. Uniform-controlled flow retains its prior zero/nonzero `CMP` behavior.
- The Cortex-X3, A715, A710, and A510 basic arithmetic/logical timing tables on pages 15, 17, 17,
  and 14 respectively list the relevant flag-setting operations as one-cycle-latency instructions.
  A disassembly-checked ARM64 benchmark executed eight evaluations per loop, alternated A/B order
  across seven rounds, and took the best result on every Thor core class. Current versus selected
  one-instruction sequences measured, in ns/evaluation:
  - A510 CPU 0: OR11 0.560538 -> 0.373591 (33.35%), OR00 0.902216 -> 0.372051
    (58.76%), OR10 0.747175 -> 0.372039 (50.21%), AND00 0.750218 -> 0.373479
    (50.22%), and AND10 0.561308 -> 0.373529 (33.45%).
  - A715 CPU 3: 0.292621 -> 0.196484 (32.85%), 0.509576 -> 0.196484 (61.44%),
    0.385394 -> 0.196484 (49.02%), 0.382662 -> 0.196497 (48.65%), and
    0.294664 -> 0.196484 (33.32%).
  - A710 CPU 5: 0.302723 -> 0.212689 (29.74%), 0.476968 -> 0.212677 (55.41%),
    0.387151 -> 0.212739 (45.05%), 0.384922 -> 0.212689 (44.74%), and
    0.302618 -> 0.212739 (29.70%).
  - X3 CPU 7: 0.243131 -> 0.158492 (34.81%), 0.429650 -> 0.156431 (63.59%),
    0.353369 -> 0.159157 (54.96%), 0.341156 -> 0.156394 (54.16%), and
    0.232713 -> 0.161858 (30.45%).
- Permanent IFC coverage already exhausts all sixteen `refx/refy/COND0/COND1` combinations for
  JustX, JustY, OR, and AND in both the interpreter and JIT. New control-flow coverage deliberately
  uses a `CMP/GE` condition whose equal true case would fail an old hard-coded EQ/NE assumption,
  and checks both true and false CALLC, JMPC, and BREAKC paths. The focused `Conditional*` device
  run passed all 140 assertions in four cases; the complete `[shader]` run passed all 18,316
  assertions in 52 cases. The ARM64 native build passed in 1 minute 34 seconds. Temporary test and
  benchmark executables were removed from Thor and host, and rendered manual pages were removed.
  Source commit `341bfc574` was pushed to `master` before packaging.
- Release-style packaging passed in 2 minutes 51 seconds. The ARM64-only, v2-signed APK is
  28,976,928 bytes, reports `341bfc574-vanilla-thor`, and has SHA-256
  `09E2459C73B18A7C70096DF56F200D5BD203ADEC889EFE7651735E0E513E9680`. It installed successfully
  over `org.azahar_emu.azahar.debug`, reports `primaryCpuAbi=arm64-v8a`, and remains force-stopped;
  no app UI or game was launched. Thor reported USB power, no AC/wireless power, 79% battery,
  4.188 V, and 23.0 C. Charging telemetry is not a battery-discharge watt measurement.
- Post-verification cleanup removed 2,052,971,095 logical bytes of Gradle/JNI/native staging,
  native helper binaries, and the repo-local Gradle cache. `app/build` retains only the APK and
  metadata; the 3,225,922,442-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains. C:
  reported 86,168,379,392 bytes free after cleanup.
- This is an isolated generated-condition speedup of 29.7-63.6% in the measured cases, not a
  whole-game FPS or wattage claim. A matched title/scene/cache/renderer/driver/resolution/layout/
  mode/fan/brightness/duration A/B remains required before attributing sustained system-level gain.

## 2026-08-17 Dynarmic Direct A32 NZCV Capture

- Command-line Git over SSH refreshed Azahar `upstream/master` to `fb1c1a710` and found the fork
  133 commits ahead with no upstream-only commit. The official Azahar Dynarmic `master` was fetched
  directly into this repository's object store and still resolves to
  `e77b1ba0b7da7cbe93021b01a663acfe7c4dd516` from 2026-06-24. It retains six ARM64 `RMIF` TODOs;
  the vendored Oaknut has no `RMIF` mnemonic and Dynarmic exposes no FlagM host capability gate, so
  emitting that optional instruction globally would be incorrect.
- `A32SetCpsrNZCV` previously asked the allocator for a temporary GPR. For a flags-backed arithmetic
  result this generated `MRS Xtemp, NZCV` followed by `MOV W23, Wtemp`, even though the fork already
  reserves callee-saved `W23` for guest NZCV. `ReadIntoFixedRegister()` now copies an IR argument
  directly into a fixed register through the existing immediate/GPR/FPR/spill/flags loader. It
  asserts that the destination is absent from the allocator order; the A32 call site targets only
  reserved `X23`. Normal argument-use accounting and `SpillFlags()` behavior remain intact.
- The A710, A715, and X3 special-purpose-register tables on pages 86, 63, and 60 say NZCV is fully
  renamed and its read is neither non-speculative, in-order, nor flush-producing. The A510 guide
  does not publish the comparable table, so all four real core classes were measured. A
  disassembly-checked ARM64 benchmark evaluated 16,777,216 flag results per case, consumed each with
  `TBZ`, alternated current/direct order across nine rounds, and took the best result:
  - A510 CPU 0: 3.087629 -> 2.586969 ns/evaluation, 16.22% faster.
  - A715 CPU 3: 0.504820 -> 0.403201 ns/evaluation, 20.13% faster.
  - A710 CPU 5: 0.516344 -> 0.416782 ns/evaluation, 19.28% faster.
  - X3 CPU 7: 0.389681 -> 0.380737 ns/evaluation, 2.30% faster.
- A nearby two-instruction idea was deliberately rejected. Replacing uniform `LDRB; CMP; B.cond`
  with `LDRB; CBZ/CBNZ` removed an instruction but made the two taken patterns 24.18%/22.71% slower
  on A510 and 45.99%/38.93% slower on A715. A510 fallthrough improved about 44%; A715 fallthrough
  tied, and A710/X3 tied or moved only within noise. The manual throughput rows did not capture the
  branch-direction cost, so the production PICA sequence remains unchanged.
- Permanent `[core][arm][dynarmic]` coverage runs real guest `ADDS R0, R0, #1` followed by MI, VS,
  CS, and EQ conditional moves across linked A32 blocks. N/V, Z/C, no-flags, and N-only inputs passed
  all 24 assertions. The broader `[core]~[file_sys]` device run passed 2,891 assertions in 16 cases;
  the excluded Android path-parser case is a pre-existing host-filesystem fixture that expects a
  generated `get_build_flavor` file. Native ARM64 builds linked the test ELF and
  `libcitra-android.so`; source commit `2d39584f4` was pushed to `origin/master` before packaging.
- A complete 2,200-action ARM64 native rebuild and release-style package passed in 14 minutes 3
  seconds. The ARM64-only, v2-signed APK is 28,976,840 bytes, reports
  `2d39584f4-vanilla-thor`, and has SHA-256
  `141F9311E5B9910031674508F4A1BE1269A8F54EEBC28837F9E6FB08969683E6`. It installed over
  `org.azahar_emu.azahar.debug` by wireless ADB, reports `primaryCpuAbi=arm64-v8a`, and remains
  force-stopped; no UI or game was launched. The Thor reported USB power, no AC/wireless power,
  78% battery, 4.154 V, and 23.0 C, so this is not battery-discharge watt evidence.
- Post-verification cleanup removed 2,056,113,736 logical bytes of Gradle/JNI/native staging, the
  repo-local Gradle cache, benchmark/test helpers, and rendered manual pages. C: free space rose by
  1,608,404,992 bytes. `app/build` retains only the 28,976,840-byte APK and 476-byte metadata; the
  3,230,823,924-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains for incremental work.
- This is optimization 79 in the current Thor work tally and a 2.3%-20.1% win for one recurring
  generated sequence, not the whole emulator. The 79 changes overlap, trigger in different games,
  and include rejected/UX/power-oriented work; their percentages must not be added. Whole-game FPS,
  sustained power, frametime, and thermal gains still require a matched title/scene A/B.

## 2026-08-17 Dynarmic ARM64 Final-Use Read/Write Coalescing

- Dynarmic's x64 allocator already updates an eligible final-use read/write operand in its current
  host register, but the ARM64 allocator always allocated a new output register and copied the old
  value first. The ARM64 path now transfers that physical location to the output when the input is
  non-immediate, has the same host-register class, has exactly one remaining IR use and one active
  lock, and the output has not already been realized. Shared or otherwise ineligible values retain
  the original allocate-and-copy path.
- `HostLocInfo::ReplaceLastUseWith()` changes only the final value owner. `RAReg` records a reused
  read location so its destructor unlocks the output value without erasing the physical location it
  now owns. This removes copies from eligible SHA-256, saturating vector accumulate, vector-element
  insertion, VTBX default, vector FMA, and FP16 absolute-value lowerings without changing their
  emitted operation or the allocator's spill fallback.
- A disassembly-checked benchmark compared the old explicit full-vector copy with the coalesced
  form over 16,777,216 useful operations, alternated order for nine rounds, and used the best sample.
  An initial version accidentally serialized four nominal chains through one temporary and its
  numbers were discarded. The corrected benchmark kept four independent chains. Nanoseconds per
  useful read/write operation, throughput multiple, and time reduction were:

  | Thor core | FMLA old -> new | FMLA gain | BIC old -> new | BIC gain |
  | --- | --- | --- | --- | --- |
  | A510 CPU 0 | 2.756224 -> 1.250332 | 2.204x; 54.6% less time | 2.501638 -> 1.001578 | 2.498x; 60.0% less time |
  | A715 CPU 3 | 0.543852 -> 0.189565 | 2.869x; 65.1% less time | 0.353695 -> 0.190552 | 1.856x; 46.1% less time |
  | A710 CPU 5 | 0.648188 -> 0.189363 | 3.423x; 70.8% less time | 0.389752 -> 0.187444 | 2.079x; 51.9% less time |
  | X3 CPU 7 | 0.592520 -> 0.169103 | 3.504x; 71.5% less time | 0.338578 -> 0.169240 | 2.001x; 50.0% less time |

- A complete 2,200-action ARM64 native rebuild and release-style package passed in 14 minutes 8
  seconds. A permanent A32 VTBX regression then rebuilt in 38 seconds and passed on Thor together
  with the existing linked-block flag case: 32 assertions in two `[core][arm][dynarmic]` cases. The
  broader `[core]~[file_sys]` run passed 2,899 assertions in 17 cases. Source commit `a9aada95d`
  and test commit `6ca666b71` were pushed to `origin/master` through command-line Git over SSH.
- The ARM64-only, v2-signed APK is 28,979,556 bytes, reports
  `a9aada95d-vanilla-thor`, and has SHA-256
  `54EB796EE6854BDD3FB4AD1623A79706F7E0E7D5FA295314D64011489D00AC09`. It installed over
  `org.azahar_emu.azahar.debug` by wireless ADB, reports `primaryCpuAbi=arm64-v8a`, and remains
  force-stopped; no app UI or game was launched. Thor reported USB power, no AC/wireless power,
  78% battery, 4.126 V, and 23.0 C, so this is not battery-discharge watt evidence.
- Temporary benchmark/test executables were removed from host and device. Post-verification cleanup
  removed 2,017,514,496 logical bytes of reproducible Gradle/JNI/native staging and increased C:
  free space by 1,579,499,520 bytes. `app/build` retains only the APK and 476-byte metadata; the
  3,243,275,791-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains for incremental work.
- This is optimization 80 in the current Thor tally. The measured result is a 1.86x-3.50x synthetic
  throughput gain, or 46.1%-71.5% less time, only when these recurring read/write sequences and
  final-use lifetimes occur. It cannot be added to the other 79 items. Whole-game FPS, sustained
  watts, frametimes, and thermals still require a matched title/scene A/B.

## 2026-08-17 Dynarmic ARM64 Packing and Select Move Elimination

- Three remaining ARM64 lowerings still paid for x86-shaped result materialization after final-use
  coalescing landed. `Pack2x32To1x64` copied its low word before `BFI`, `LeastSignificantWord`
  copied the low 32 bits of an existing 64-bit value, and `PackedSelect` copied its GE mask before
  destructive `BSL`. The first and third now use the conservative final-use read/write allocator;
  the low-word operation is a zero-code `DefineAsExisting()` alias. Shared or otherwise ineligible
  values retain the allocate-and-copy fallback.
- The Arm manuals confirm that the surviving operations use baseline pipelines on every Thor core.
  `BFM`/`BFI` is documented on X3 page 18, A715 page 20, A710 page 27, and A510 page 22; `BSL` is
  on X3 page 31, A715 page 34, A710 page 52, and A510 page 43. Removing the preceding move avoids
  real dependency/issue work and assumes no optional ISA extension.
- A disassembly-checked AArch64 benchmark ran 16,777,216 useful operations over four independent
  chains, alternated old/new order across nine rounds, took the best sample, and verified equal
  checksums. An initial packed-select version violated AAPCS64 by clobbering callee-saved SIMD
  registers; those numbers were discarded, the helper was corrected to caller-saved registers,
  and final disassembly verified the exact old and new sequences. Results were:

  | Thor core | Pack 32x2 old -> new | Low word old -> new | Packed SEL old -> new |
  | --- | --- | --- | --- |
  | A510 CPU 0 | 0.625756 -> 0.249660 ns/op; 2.506x | 0.624926 -> 0.294328; 2.123x | 2.763567 -> 2.008003; 1.376x |
  | A715 CPU 3 | 0.225835 -> 0.159822; 1.413x | 0.244629 -> 0.165706; 1.476x | 0.539208 -> 0.370929; 1.454x |
  | A710 CPU 5 | 0.208508 -> 0.184927; 1.128x | 0.209658 -> 0.162218; 1.292x | 0.556094 -> 0.370510; 1.501x |
  | X3 CPU 7 | 0.178597 -> 0.169376; 1.054x | 0.148312 -> 0.102176; 1.452x | 0.254074 -> 0.231546; 1.097x |

- Permanent tests execute real A32 `UMLAL` to cover packed low/high results and real A32 `SEL`
  across all 16 GE masks, including GE preservation. The focused device suite passed 66 assertions
  in four cases; the broader `[core]~[file_sys]` run passed 2,933 assertions in 19 cases. Two
  incremental ARM64 native builds passed in 1 minute 28 seconds and 1 minute 7 seconds. Temporary
  benchmark/test sources and binaries were removed from both host and Thor. Source/test commit
  `fecae1a30` was pushed directly to `origin/master` over command-line Git SSH before packaging.
- The JDK 17 release-style package passed in 2 minutes 36 seconds. The ARM64-only, v2-signed APK is
  28,976,828 bytes, reports `fecae1a30-vanilla-thor`, and has SHA-256
  `DE4D278DCD87BB81056990CEC6FEED366CEB4C2261EA3A8FBC79838130293157`. Wireless ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reports `primaryCpuAbi=arm64-v8a`, `stopped=true`, and
  no process ID. No UI or game was launched. Thor reported USB power, no AC/wireless power, 79%
  battery, 4.145 V, and 22.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Post-verification cleanup removed 2,017,554,358 logical bytes from `app/build` and increased C:
  free space by 1,589,624,832 bytes. The build directory retains only the APK and its 476-byte
  metadata; the 3,243,488,163-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains for
  incremental work.
- This is optimization 81 in the current Thor work tally. Its measured 1.05x-2.51x result applies
  only to these exact generated sequences. The 81 items overlap, trigger in different workloads,
  and include rejected, UX, and power-oriented work; their gains cannot be added. Whole-game FPS,
  sustained watts, frametimes, and thermals still require a matched title/scene A/B.

## 2026-08-18 Dynarmic ARM64 Signed-Narrow Fusion

- The A32 frontend contains 133 byte/halfword low-part construction sites and at least 59 direct
  textual narrow-plus-sign-extension expressions. Guest `SXTB`, `SXTH`, signed DSP operations, and
  halfword multiplies could therefore lower to `UXTB; SXTB` or `UXTH; SXTH`: the unsigned narrow
  canonicalized a value whose sole next consumer immediately discarded the same upper bits again.
- The ARM64 emitter now aliases the narrow result only when it has exactly one use, the immediately
  following IR instruction is the matching byte/halfword signed extension, and argument zero points
  directly to that producer. `SXTB`/`SXTH` then performs truncation and sign extension in one
  instruction. This O(1) check adds no block scan or lookup allocation. Shared, non-adjacent, zero-
  extension, store, shift, and unknown-consumer paths retain `UXTB`/`UXTH`.
- The X3 page 18, A715 page 20, A710 pages 27-28, and A510 pages 22-23 tables cover the baseline
  `UBFM`/`SBFM` family underlying these aliases. Removing one bitfield operation reduces a true
  dependency and integer-pipeline work on every Thor core without an optional ISA assumption.
- A disassembly-checked benchmark compared four independent old and fused chains over 16,777,216
  iterations, alternated order for nine rounds, selected the best samples, and verified equal
  checksums:

  | Thor core | Byte `UXTB; SXTB` -> `SXTB` | Half `UXTH; SXTH` -> `SXTH` |
  | --- | --- | --- |
  | A510 CPU 0 | 0.627656 -> 0.250822 ns/op; 2.502x; 60.04% less time | 1.132231 -> 0.251382; 4.504x; 77.80% less time |
  | A715 CPU 3 | 0.236033 -> 0.141632; 1.667x; 39.99% | 0.235977 -> 0.141353; 1.669x; 40.10% |
  | A710 CPU 5 | 0.273183 -> 0.155802; 1.753x; 42.97% | 0.276155 -> 0.152790; 1.807x; 44.67% |

  A710 CPU 6 independently measured 1.856x/1.778x for byte/halfword. CPU 7 reported online but
  rejected both the benchmark and `/system/bin/true` with a single-bit affinity mask as `EINVAL`;
  no X3 measurement is claimed for this run.
- Permanent guest coverage executes A32 `SXTB`, `SXTH`, and `SMULBB` with dirty upper bits, plus a
  register-controlled `LSL` whose shift register is `0xffff0001`. That last result proves narrowing
  remains on the non-sign-extension path. Thor passed 70 assertions in five focused cases and 2,937
  assertions in 20 broader core cases. The ARM64 native build passed in 1 minute 45 seconds;
  temporary opcode-check, test, and benchmark files were removed from host and device. Source/test
  commit `fc067c02f` was pushed directly to `origin/master` through command-line Git SSH.
- JDK 17 release packaging passed in 2 minutes 28 seconds. The ARM64-only, v2-signed APK is
  28,977,844 bytes, reports `fc067c02f-vanilla-thor`, and has SHA-256
  `CBF8900D2E85268BA4AB19713C55F9E7D4FC08C5880986A493E754B95D9D9894`. It installed over
  `org.azahar_emu.azahar.debug`; Android reports `primaryCpuAbi=arm64-v8a`, `stopped=true`, and no
  process ID. No UI or game was launched. Thor reported USB power, no AC/wireless power, 80%
  battery, 4.156 V, and 21.0 C, so this is not a matched battery-discharge watt measurement.
- Cleanup removed 2,017,571,882 logical bytes from `app/build` and raised C: free space by
  1,581,228,032 bytes. Only the APK and 476-byte metadata remain there; the 3,244,305,971-byte
  active ARM64 RelWithDebInfo CMake/Ninja cache remains for incremental work.
- This is optimization 82 in the Thor work tally. The 1.67x-4.50x figures apply only to the exact
  fused sequences when those IR patterns occur. The 82 items overlap and cannot be added; matched
  title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness A/B runs remain necessary
  for whole-game FPS, sustained watts, frametime, or thermal claims.

## 2026-08-18 Dynarmic ARM64 Register-Shift Mask Elision

- A32 register-controlled data processing constructs its shift count with
  `LeastSignificantByte(GetRegister(...))` at 21 ARM/Thumb translation sites. On ARM64 that
  producer emits `UXTB`, but the no-carry LSL, LSR, and ASR lowerings immediately masked the same
  host register again with `AND #0xff`. The second operation could not change the value.
- The emitter now recognizes only a shift argument that resolves through identities to
  `LeastSignificantByte` and uses that register directly for the variable shift and range compare.
  This is deliberately not a general U8 invariant: callback-returned bytes and future producers
  retain the old mask. Carry-producing shift paths also remain unchanged. A shift consumer cannot
  trigger the adjacent signed-extension fusion, so the recognized producer necessarily emitted
  its canonicalizing `UXTB`.
- The checked Cortex tables put the removed logical operation and surviving variable shift on the
  same integer resources. `AND` has latency/throughput 1/6 on X3 page 15, 1/4 on A715/A710 page 17,
  and 1/3 on A510 page 14. `LSLV`/`LSRV`/`ASRV` have 1/6 on X3 page 18, 1/4 on A715 page 20 and A710
  page 27, and 1/3 on A510 page 22. Removing the duplicate therefore saves one dependency and one
  integer issue without assuming an optional extension.
- A disassembly-checked benchmark retained the frontend `UXTB` and compared the exact old and new
  LSL and clamped-ASR sequences over four independent chains and 16,777,216 iterations. Nine
  rounds alternated old/new order, selected each best sample, and required equal nonzero checksums:

  | Thor core | LSL old -> new | ASR old -> new |
  | --- | --- | --- |
  | A510 CPU 0 | 2.136812 -> 1.510237 ns/op; 1.415x; 29.32% less time | 2.640345 -> 2.135542; 1.236x; 19.12% |
  | A715 CPU 3 | 0.535285 -> 0.417087; 1.283x; 22.08% | 0.499109 -> 0.388098; 1.286x; 22.24% |
  | A710 CPU 5 | 0.501662 -> 0.422252; 1.188x; 15.83% | 0.485471 -> 0.390124; 1.244x; 19.64% |

  CPU 6 and CPU 7 reported online but rejected harmless single-bit affinity probes during the
  final run, so no second-A710 or X3 measurement is claimed.
- Permanent guest coverage executes non-flags LSL, LSR, ASR, and ROR plus carry-producing LSLS for
  dirty-upper-bit shift registers whose low bytes are 0, 1, 31, 32, 33, and 255. It verifies the
  complete result and carry semantics at every ARM edge. Thor passed 106 assertions in six focused
  Dynarmic cases and 2,973 assertions in 21 broader core cases. The ARM64 native build passed in
  85.49 seconds. Temporary opcode, test, benchmark, disassembly, and rendered-manual files were
  removed from host and device. Source/test commit `169306159` was pushed directly to
  `origin/master` through command-line Git SSH.
- JDK 17 release packaging passed in 2 minutes 33 seconds. The ARM64-only, v2-signed APK is
  28,977,464 bytes, reports `169306159-vanilla-thor`, and has SHA-256
  `EBD13F4D4493F8415BF4358242B413CBC733AA0B0221EA0367EBA04D24851619`. It installed over
  `org.azahar_emu.azahar.debug`; Android reports `primaryCpuAbi=arm64-v8a`, `stopped=true`, and no
  process ID. No UI or game was launched. Thor reported USB power, no AC/wireless power, 80%
  battery, 4.155 V, and 21.0 C, so this is not battery-discharge watt evidence.
- Cleanup removed 2,017,617,383 logical bytes from `app/build` and raised C: free space by
  1,576,497,152 bytes. Only the APK and 476-byte metadata remain there; the 3,244,522,777-byte
  active ARM64 RelWithDebInfo CMake/Ninja cache remains for incremental work.
- This is optimization 83 in the Thor work tally. Its 1.19x-1.42x LSL and 1.24x-1.29x ASR results
  apply only to these exact generated sequences. The 83 items overlap, trigger in different title
  workloads, and cannot be added. Whole-game FPS, sustained watts, frametime, and thermal claims
  still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness
  A/B run.

## 2026-08-18 Dynarmic ARM64 Sole-Consumer Shift-Byte Fusion

- Optimization 83 still materialized the frontend `LeastSignificantByte` as `UXTB`. For an A32
  register shift, that byte value normally has exactly one eventual use as the shift instruction's
  count, with only register/flag reads between producer and consumer. The ARM64 allocator can keep
  the raw source live through those reads, so the byte result can alias it without a host
  instruction when the sole consumer is 32-bit LSL, LSR, or ROR.
- AArch64 variable shifts consume only bits 4:0. That directly matches A32 ROR's low-byte count
  modulo 32. For no-carry LSL/LSR, `TST Wcount, #0xe0` examines only low-byte bits 7:5: EQ means
  the A32 count is 0..31 and the variable-shift result is valid; non-EQ means 32..255 and selects
  zero. Dirty source bits above bit 7 affect neither operation. Existing carry paths retain their
  low-byte zero/range checks, while their variable shifts also need only bits 4:0.
- The gate requires one use, finds that eventual consumer, accepts only its shift-count argument,
  and recognizes only LSL/LSR/ROR. Shared values, stores, extensions, unknown consumers, and generic
  U8 producers retain `UXTB` and their masks. ASR deliberately retains optimization 83's canonical
  path: a candidate `MOV 31; TST #0xe0; CSEL; ASRV` sequence helped A510 by roughly 23%, but repeated
  A715 runs were 0.9%-4.9% slower and A710 improved by only 0.6%-1.1%.
- The checked manual tables list basic/flag-setting logical operations on X3 page 15, A715/A710
  page 17, and A510 page 14. `UBFM`/`UXTB` and variable shifts are on X3 page 18, A715 page 20,
  A710 page 27, and A510 page 22. These are real integer/ALU operations on every Thor core class;
  removing one reduces instruction fetch/decode/issue work without an optional ISA feature.
- A disassembly-checked benchmark used four independent chains, 16,777,216 iterations, nine
  alternating-order rounds, best samples, and equal nonzero checksums. It compared optimization
  83's exact sequence with the accepted raw-count sequence:

  | Thor core | LSL old -> new | LSR old -> new | ROR old -> new |
  | --- | --- | --- | --- |
  | A510 CPU 0 | 1.509530 -> 0.752534 ns/op; 2.006x; 50.15% less time | 1.506653 -> 0.627943; 2.399x; 58.32% | 1.132371 -> 0.250024; 4.529x; 77.92% |
  | A715 CPU 3 | 0.392147 -> 0.314307; 1.248x; 19.85% | 0.392278 -> 0.314244; 1.248x; 19.89% | 0.209037 -> 0.160285; 1.304x; 23.32% |
  | A710 CPU 5 | 0.388550 -> 0.320123; 1.214x; 17.61% | 0.388614 -> 0.320318; 1.213x; 17.57% | 0.204095 -> 0.150064; 1.360x; 26.47% |

  CPU 6 and CPU 7 reported online but rejected harmless single-bit affinity probes with `EINVAL`,
  so no second-A710 or X3 result is claimed.
- Permanent guest coverage now checks carry-producing LSLS, LSRS, ASRS, and RORS separately for
  dirty-upper-bit count registers whose low bytes are 0, 1, 31, 32, 33, and 255, in addition to
  the no-flags coverage. The final ARM64 build passed in one minute. Thor passed 154 assertions in
  seven focused Dynarmic cases and 3,021 assertions in 22 broader core cases. Temporary test,
  benchmark, disassembly, and rendered-manual files were removed from host and device. Source/test
  commit `e9aa683d4` was pushed directly to `origin/master` through command-line Git SSH.
- JDK 17 release packaging passed in 2 minutes 28 seconds. The 28,977,696-byte APK is ARM64-only,
  v2-signed, reports `e9aa683d4-vanilla-thor`, and has SHA-256
  `F3EA150A076C0682D70A7D24DE37EC3559D29CF360433550DC2E6C7927F34A50`. It installed over
  `org.azahar_emu.azahar.debug` via Wi-Fi and was force-stopped with no process ID; no UI or game
  was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.155 V, and 20.0 C,
  so this is not a battery-discharge watt measurement.
- Cleanup removed 2,017,658,292 logical bytes from `app/build` and raised C: free space by
  1,577,951,232 bytes. Only the APK and 476-byte metadata remain there; the 3,244,726,683-byte
  active ARM64 RelWithDebInfo CMake/Ninja cache remains for incremental work.
- This is optimization 84 in the Thor work tally. Its 1.21x-4.53x figures apply only to these exact
  generated sequences when the sole-consumer gate fires. The 84 items overlap and cannot be added.
  Whole-game FPS, sustained watts, frametime, and thermal claims still require a matched title,
  scene, cache state, renderer, driver, resolution, layout, performance mode, fan, and brightness
  A/B run.

## 2026-08-18 Dynarmic A32 Scalar Long-Multiply Lane Broadcast

- A32 scalar `VMULL`, `VMLAL`, and `VMLSL` previously constructed their scalar operand as
  `VectorGetElement()` followed immediately by `VectorBroadcast()`. On the ARM64 backend that
  crossed from SIMD to a general register with `UMOV` and then crossed back with the general-
  register form of `DUP`. The other scalar NEON multiply families already used the direct
  `VectorBroadcastElement()` form.
- The long-multiply translator now uses that direct element broadcast too. The selected 16-bit or
  32-bit lane and the replicated vector are bit-identical; the signed/unsigned widening multiply
  and optional vector add/subtract remain unchanged. Generated preparation falls from
  `UMOV; DUP (general register)` to one `DUP (element)`, eliminating a cross-register-bank
  dependency and one host instruction without an optional ISA feature.
- The complete relevant timing-table pages were rendered and visually checked. X3 pages 31-32,
  A715 pages 34-35, and A710 pages 52-53 list element `DUP` at two-cycle latency, while the old
  route adds a two-cycle `UMOV` and uses the three-cycle, one-per-cycle general-register `DUP`.
  A510 pages 43-44 list three cycles for element `DUP`, `UMOV`, and general-register `DUP`, with
  the direct element form also avoiding the second instruction and general-register handoff.
- A disassembly-checked benchmark executed eight independent broadcasts per loop for 16,777,216
  iterations, alternated old/new order across nine rounds, selected each best sample, and required
  equal nonzero checksums. Nanoseconds per useful broadcast were:

  | Thor core | 16-bit `UMOV; DUP` -> element `DUP` | 32-bit `UMOV; DUP` -> element `DUP` |
  | --- | --- | --- |
  | A510 CPU 0 | 3.016938 -> 0.502268; 6.007x; 83.35% less time | 3.019914 -> 0.503150; 6.002x; 83.34% |
  | A715 CPU 3 | 0.358605 -> 0.179209; 2.001x; 50.03% | 0.358586 -> 0.179214; 2.001x; 50.02% |
  | A715 CPU 4 | 0.358444 -> 0.179187; 2.000x; 50.01% | 0.358347 -> 0.179237; 1.999x; 49.98% |

  CPUs 5 and 7 reported online at 2.8032 and 3.1872 GHz but rejected both direct and Android
  `taskset` single-bit affinity with `EINVAL`; no A710 or X3 benchmark number is claimed for this
  run.
- Permanent guest coverage executes `VMULL.S16`, `VMLAL.U16`, `VMLSL.S32`, and `VMULL.U32` with
  different scalar-lane indices, signed extremes, unsigned accumulator wrap, subtraction, and
  complete 64-bit results. Thor passed 166 assertions in eight focused `[core][arm][dynarmic]`
  cases and 3,033 assertions in 23 broader `[core]~[file_sys]` cases. The ARM64 native build linked
  the test ELF and `libcitra-android.so` in 1 minute 21 seconds. Source/test commit `f63697ee0` was
  pushed directly to `origin/master` over command-line Git SSH before packaging.
- JDK 17 release packaging passed in 2 minutes 32 seconds. The ARM64-only, v2-signed APK is
  28,976,576 bytes, reports `f63697ee0-vanilla-thor`, and has SHA-256
  `0132573765AAAB8E4D188AE3FE43F836137300D5EEAD79213270406D58AD5FAF`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.154 V, and
  20.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Temporary test/benchmark binaries and rendered manual pages were removed from host and Thor.
  Post-verification cleanup removed 2,017,682,102 logical bytes of Gradle/JNI/native staging and
  raised C: free space by 1,608,740,864 bytes. `app/build` retains only the 28,976,576-byte APK and
  476-byte metadata; the 3,238,891,722-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains
  for incremental work.
- This is optimization 85 in the Thor work tally. Its 2.00x-6.01x result applies only to the scalar
  lane-broadcast preparation used by these guest long multiplies. The 85 items overlap, trigger in
  different workloads, and cannot be added. Whole-game FPS, sustained watts, frametime, and thermal
  gains still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness A/B.

## 2026-08-18 Dynarmic A32 VZIP D-Register SIMD Retention

- A32 D-register `VZIP.8` and `VZIP.16` already formed both results in one U128
  `VectorInterleaveLower()`, but then extracted its two 64-bit halves with `VectorGetElement()` and
  wrote them with `SetExtendedRegister()`. On ARM64, each extraction emitted an element-to-GPR
  `UMOV`; each D-register write immediately reconstructed the same SIMD value with a GPR-to-D
  `FMOV`. Four cross-register-bank transfers surrounded values that never needed to leave SIMD.
- The D form now writes the interleave result's low half with `SetVector()` and uses
  `VectorRotateWholeVectorRight(..., 64)` before writing the high half. The ARM64 backend consumes
  the first D value directly and lowers the rotation to one `EXT #8`. Guest lane order is unchanged,
  the existing Q-register path is untouched, and the decoder's existing rejection of the undefined
  D-form 32-bit size remains intact. Result preparation falls from
  `ZIP1; UMOV; UMOV; FMOV; FMOV` to `ZIP1; EXT`: five host instructions to two, with no optional
  ISA feature.
- The complete relevant timing-table pages were rendered and visually checked. X3 pages 31-32 list
  `EXT` at two-cycle latency and throughput four, versus throughput one for element-to-general-
  register `UMOV`. A715 pages 34-35 and A710 pages 52-53 list `EXT` at two-cycle latency and
  throughput two, again versus throughput one for `UMOV`. A510 pages 43-44 place `EXT`, `UMOV`, and
  the unzip/zip family on its VALU paths; the new sequence removes the two `UMOV`s and the two
  reverse-bank `FMOV`s there as well.
- A disassembly-checked benchmark compared eight independent old and new result paths, retained two
  identical D-width consumers per operation, ran 8,388,609 iterations, alternated order over nine
  rounds, selected each best sample, and required equal nonzero `0x81` checksums:

  | Thor core | Old -> new | Local result-path gain |
  | --- | --- | --- |
  | A510 CPU 0 | 6.665553 -> 4.020711 ns/op | 1.658x; 39.68% less time |
  | A715 CPU 3 | 1.066110 -> 0.728358 ns/op | 1.464x; 31.68% less time |
  | A715 CPU 4 | 1.066017 -> 0.733805 ns/op | 1.453x; 31.16% less time |

  Only the currently usable CPU 0/3/4 single-bit affinity masks were measured; no A710 or X3
  number is claimed for this run.
- Permanent guest coverage executes low-register `VZIP.8 D0, D1` and high-register
  `VZIP.16 D30, D31` with non-repeating lanes and verifies all four complete 64-bit outputs. Thor
  passed 174 assertions in nine focused `[core][arm][dynarmic]` cases and 3,041 assertions in 24
  broader `[core]~[file_sys]` cases. The ARM64 native build passed in 1 minute 18 seconds. Temporary
  assembler, benchmark, stripped-test, disassembly, and rendered-manual files were removed from
  both host and device. Source/test commit `18b35d600` was made directly on `master`.
- JDK 17 release packaging passed in 2 minutes 39 seconds. The ARM64-only, v2-signed APK is
  28,977,540 bytes, reports `18b35d600-vanilla-thor`, and has SHA-256
  `5FD34C294FA02032BB21C5B83FB4CDABF97C7BBD2BD8FCF3E43A644CF93A713A`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and
  20.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Post-verification cleanup removed 2,017,695,829 logical bytes from `app/build` and raised C: free
  space by 1,576,329,216 bytes. `app/build` retains only the 28,977,540-byte APK and 476-byte
  metadata; the 3,245,100,241-byte active ARM64 RelWithDebInfo CMake/Ninja cache remains for
  incremental work.
- This is optimization 86 in the Thor work tally. Its 1.45x-1.66x result applies only when an A32
  guest executes the D-register VZIP forms and only to this exact result path. The 86 items overlap,
  trigger at different frequencies, and cannot be added. Whole-game FPS, sustained watts,
  frametime, and thermal gains still require a matched title/scene/cache/renderer/driver/resolution/
  layout/mode/fan/brightness A/B run.

## 2026-08-18 Dynarmic Native Widening Absolute Difference

- A32 guest `VABDL`/`VABAL` first extracted each 64-bit source from its SIMD register with
  `VectorGetElement(64)`, reconstructed a D value with `ZeroExtendToQuad()`, and widened it with
  `VectorZeroExtend()` before taking the absolute difference. The ARM64 emitter therefore paid
  `UMOV; FMOV; UXTL` for each source plus `SABD`/`UABD`: seven host instructions with two round
  trips across the SIMD/GPR register banks. The equivalent A64 guest path used two separate
  extensions plus the difference.
- Dynarmic now has signed and unsigned 8/16/32-bit widening-absolute-difference IR operations. A32
  feeds the original D-register values directly; A64 feeds the selected low/high 64-bit part; and
  accumulation remains a widened-lane `VectorAdd()` after the difference. The ARM64 backend emits
  one baseline `SABDL`/`UABDL`. x64 expands the new operation back into the established same-width
  difference plus zero extension, preserving portability without requiring an x64-specific ISA.
- The actual Snapdragon core guides were rendered and visually checked. The low-half
  `SABDL`/`UABDL` form is listed at latency 2 / throughput 4 on Cortex-X3 issue 4.0 page 25,
  latency 2 / throughput 2 on Cortex-A715 issue 5.0 page 28 and Cortex-A710 issue 4.0 page 42, and
  latency 3 with the table's low/high-form throughput notation `2,1` on Cortex-A510 issue 6.0 page
  35. The implementation uses the faster low-half form because the selected guest D value is
  already in the low 64 bits.
- A disassembly-checked benchmark compared eight independent repetitions of the exact old
  `UMOV; FMOV; UXTL; UMOV; FMOV; UXTL; UABD` path with eight native `UABDL` instructions. It ran
  2,000,000 loop iterations per sample (16,000,000 operations), alternated A/B order over seven
  rounds, selected each best sample, and required identical nonzero `0x00fe000000fe0001` results:

  | Thor core | Old -> new | Local widening-difference gain |
  | --- | --- | --- |
  | A510 CPU 0 | 7.998265 -> 0.500492 ns/op | 15.981x; 93.74% less time |
  | A715 CPU 3 | 1.030244 -> 0.178369 ns/op | 5.776x; 82.69% less time |
  | A715 CPU 4 | 1.020482 -> 0.178372 ns/op | 5.721x; 82.52% less time |
  | A710 CPU 6 | 1.457829 -> 0.178369 ns/op | 8.173x; 87.76% less time |

  The device MIDRs identified CPU 0 as part `0xd46`, CPUs 3/4 as `0xd4d`, CPU 6 as `0xd47`, and
  CPU 7 as `0xd4e`. The current ADB-shell scheduler mask rejected single-bit affinity for CPU 7,
  so no X3 benchmark number is claimed.
- Permanent A32 guest tests cover `VABDL.S8`, `VABDL.U16`, `VABDL.S32`, `VABAL.U8`, `VABAL.S16`,
  and `VABAL.U32` with signed extremes, every widening size, complete destination lanes, and
  accumulator wraparound. Thor passed 198 assertions in 11 focused `[core][arm][dynarmic]` cases
  and 3,065 assertions in 26 broader `[core]~[file_sys]` cases. The full ARM64 native build passed
  in 1 minute 46 seconds. Source/test commit `1907b5129` was pushed directly to `origin/master`
  over command-line Git SSH before packaging.
- JDK 17 release packaging passed in 1 minute 54 seconds. The ARM64-only, v2-signed APK is
  28,980,344 bytes, reports `1907b5129-vanilla-thor`, and has SHA-256
  `B678724C5811203E83E64EEF9377E7615017748092A086FBED75D58971D46223`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and
  20.0 C, so the charging snapshot is not battery-discharge watt evidence.
- Temporary test/benchmark binaries and rendered manual pages were removed from host and Thor.
  Post-verification cleanup removed 2,021,390,819 logical bytes of Gradle/JNI/native staging and
  raised C: free space by 1,580,883,968 bytes to 82,019,672,064. `app/build` retains only the
  28,980,344-byte APK and 476-byte metadata; the 3,246,345,900-byte active ARM64 RelWithDebInfo
  CMake/Ninja cache remains for incremental work.
- This is optimization 87 in the Thor work tally. Its 5.72x-15.98x result applies only to the
  widening-absolute-difference host sequence used by guest `VABDL`/`VABAL`; instruction frequency
  is title-dependent. The 87 items overlap and cannot be added. Whole-game FPS, sustained watts,
  frametime, and thermal gains still require a matched title/scene/cache/renderer/driver/resolution/
  layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Long/Wide Add and Subtract

- A32 guest `VADDL`/`VSUBL` and A64 `SADDL`/`UADDL`/`SSUBL`/`USUBL` widened both 64-bit narrow
  inputs separately and then used generic vector add/sub IR. The ARM64 host therefore emitted two
  `SXTL`/`UXTL` instructions plus `ADD`/`SUB`. A32 `VADDW`/`VSUBW` and A64
  `SADDW`/`UADDW`/`SSUBW`/`USUBW` kept one input wide but still extended the narrow input before a
  generic add/sub, costing two host instructions.
- Dynarmic now retains these guest operations as signed/unsigned, long/wide add/sub IR. The ARM64
  backend emits one baseline `SADDL`/`UADDL`/`SSUBL`/`USUBL` for the long forms and one
  `SADDW`/`UADDW`/`SSUBW`/`USUBW` for the wide forms. The selected A64 high half is already placed
  in the IR value's low 64 bits. x64 polyfills the new IR back to the established extension plus
  add/sub sequence, and RISC-V retains its existing unimplemented vector-backend status.
- The actual Snapdragon core guides were rendered and visually checked again. Cortex-X3 issue 4.0
  page 26 lists all eight instructions in basic ASIMD arithmetic at latency 2 / throughput 4.
  Cortex-A715 issue 5.0 page 28 and Cortex-A710 issue 4.0 page 42 list latency 2 / throughput 2.
  Cortex-A510 issue 6.0 page 35 lists the long/basic group at latency 3 with the table's `2,1`
  throughput notation. All use the normal vector arithmetic pipeline; no optional ISA extension or
  unsafe whole-binary core targeting is required.
- A disassembly-checked benchmark compared eight independent exact old/new sequences over
  2,000,000 loop iterations per sample, alternated order for seven rounds, and selected the best
  samples:

  | Thor core | Long: `2x extend + add` -> `UADDL` | Wide: `extend + add` -> `UADDW` |
  | --- | --- | --- |
  | A510 CPU 0 | 2.256113 -> 0.500260 ns/op; 4.510x | 2.005768 -> 0.500319; 4.009x |
  | A715 CPU 3 | 0.716790 -> 0.178369 ns/op; 4.019x | 0.357243 -> 0.178372; 2.003x |
  | A715 CPU 4 | 0.716087 -> 0.178844 ns/op; 4.004x | 0.357285 -> 0.178369; 2.003x |
  | A710 CPU 6 | 0.715846 -> 0.178747 ns/op; 4.005x | 0.357139 -> 0.178372; 2.002x |

  CPU 7/X3 still rejects the ADB shell's single-bit affinity request, so its manual throughput is
  recorded but no direct X3 timing is claimed. These results measure only the fused host sequences;
  their guest frequency and whole-game impact remain title/scene dependent.
- Permanent A32 guest tests cover signed and unsigned `VADDL`, `VADDW`, `VSUBL`, and `VSUBW`
  across 8/16/32-bit sizes, signed extremes, full-width results, and modular lane wraparound. Thor
  passed 206 assertions in 13 focused `[core][arm][dynarmic]` cases and 3,073 assertions in 28
  broader `[core]~[file_sys]` cases. The final ARM64 native rebuild passed in 1 minute 28 seconds.
  Source/test commit `852e7ef8e` was pushed directly to `origin/master` over command-line Git SSH.
- JDK 17 release packaging passed in 2 minutes 41 seconds. The ARM64-only, v2-signed APK is
  28,985,020 bytes, reports `852e7ef8e-vanilla-thor`, and has SHA-256
  `648F3286CFD5F8A471B3F9E582E4E40E6BFD1A8B164BE72D161F17403B351717`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.154 V, and
  20.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Temporary benchmark/test binaries and rendered manual pages were removed from host and Thor.
  Post-verification cleanup removed 2,018,486,183 logical bytes of Gradle/JNI/native staging and
  raised C: free space by 1,576,734,720 bytes to 81,981,497,344. `src/android/app/build` retains
  only the 28,985,020-byte APK and 476-byte metadata; the 3,248,592,186-byte active ARM64
  RelWithDebInfo CMake/Ninja cache remains for bounded incremental work.
- This is optimization 88 in the Thor work tally. Its 4.00x-4.51x long-form and 2.00x-4.01x
  wide-form results apply only to these exact recurring host sequences. The 88 items overlap and
  cannot be added. Whole-game FPS, sustained watts, frametime, and thermal gains still require a
  matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Widening Multiply-Accumulate

- A32 vector and scalar-by-lane `VMLAL`/`VMLSL`, plus the corresponding A64 vector and indexed
  forms, previously produced a signed/unsigned widening multiply IR followed by generic vector
  add/sub IR. The ARM64 host therefore executed `SMULL`/`UMULL` and `ADD`/`SUB`. Scalar-by-lane
  forms also retain their already optimized direct SIMD `DUP`, but paid the same two-instruction
  arithmetic cost after that broadcast.
- Dynarmic now retains signed/unsigned widening multiply-accumulate/subtract as one IR operation.
  ARM64 consumes the accumulator with `ReadWriteQ()` and emits one baseline
  `SMLAL`/`UMLAL`/`SMLSL`/`UMLSL` for 8-, 16-, and 32-bit narrow lanes. x64 polyfills the new IR
  to the established extend, multiply, and modular add/sub operations; direct Windows-target
  syntax checks covered the x64 emitter and A32/A64 interface configuration. RISC-V keeps explicit
  unimplemented handlers consistent with its existing vector backend. Both modified A64 frontend
  files also passed direct Android-target syntax checks even though Azahar builds only A32 guest
  support.
- The Snapdragon core manuals were rendered and visually checked rather than assuming fewer
  instructions always meant more throughput. Cortex-X3 issue 4.0 page 27 lists long multiply at
  latency 3 / throughput 2 and long multiply-accumulate at latency `4(1)` / throughput 2, with page
  28 explaining late forwarding of the accumulate operand. Cortex-A715 issue 5.0 page 29 and
  Cortex-A710 issue 4.0 page 43 list long multiply at latency 3 / throughput 2 but long
  multiply-accumulate at latency `4(1)` / throughput 1. Cortex-A510 issue 6.0 page 36 lists both at
  latency 4 with the table's `2,1` throughput notation on VMAC. Those A710/A715 issue-rate tables
  made a real-device regression check mandatory.
- A disassembly-checked benchmark compared eight independent exact old `SMULL; ADD` chains with
  eight native `SMLAL` chains. It ran 2,000,000 loop iterations per sample, alternated order for
  seven rounds, selected the best sample, and required the same nonzero checksum (`400420`):

  | Thor core | `SMULL + ADD` -> `SMLAL` | Result |
  | --- | --- | --- |
  | A510 CPU 0 | 2.510622 -> 0.500417 ns/op | 5.017x; 80.07% less time |
  | A715 CPU 3 | 0.357171 -> 0.357210 ns/op | 1.000x; tied |
  | A715 CPU 4 | 0.358467 -> 0.357314 ns/op | 1.003x; tied |
  | A710 CPU 5 | 0.357298 -> 0.357217 ns/op | 1.000x; tied (1.003x repeat) |
  | X3 CPU 7 | 0.157848 -> 0.156878 ns/op | 1.006x; tied |

  CPU 6 rejected the harmless single-bit affinity request during this run. The fused path has no
  measured throughput regression and halves recurring arithmetic instructions on all core classes;
  reduced decode/rename/temporary-register work is a credible power-efficiency direction, but no
  watt reduction is claimed without a matched game-scene battery-discharge test.
- Permanent A32 tests cover signed and unsigned full-vector `VMLAL`/`VMLSL` across every widening
  size, extremes, and modular wraparound. Existing scalar-by-lane tests cover unsigned accumulate
  and signed subtract. Thor passed 228 assertions in 14 focused `[core][arm][dynarmic]` cases and
  3,095 assertions in 29 broader `[core]~[file_sys]` cases. The initial full ARM64 release build
  passed in 3 minutes 30 seconds, and the exact committed revision rebuilt in 1 minute 37 seconds.
  Source/test commit `edeb3bb7c` was pushed directly to `origin/master` over command-line Git SSH.
- The ARM64-only, v2-signed APK is 28,985,156 bytes, reports `edeb3bb7c-vanilla-thor`, and has
  SHA-256 `70556050B64F810CAFFC365F8C1E27186635A8DB739E5CD1541B21008C42BDDE`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and
  20.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Post-build cleanup preserved that hash-verified APK plus `output-metadata.json` and retained the
  reusable `.cxx` compiler cache, while reducing `app/build` from 2,047,775,686 to 28,985,632
  logical bytes. That removed 2,018,790,054 logical bytes of intermediates and Windows reported
  1,576,771,584 additional free bytes on C:.
- This is optimization 89 in the Thor work tally. Its 5.017x figure applies only to the exact A510
  multiply-accumulate sequence; the measured larger cores were ties. The 89 items overlap and
  cannot be added. Whole-game FPS, sustained watts, frametime, and thermal gains still require a
  matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Mixed Halving Add/Subtract

- A32 ARM11 `SHASX`/`SHSAX`/`UHASX`/`UHSAX` previously widened both halfword inputs, exchanged the
  second operand with `EXT`, synthesized add-versus-subtract signs with an immediate mask plus
  `EOR`/`SUB`, performed a 32-bit subtraction, shifted for halving, and narrowed. The recurring
  ARM64 path was nine instructions per guest operation.
- The ARM64 backend now exchanges halfwords with `REV32`, computes both exact lane-wise candidates
  with native `SHADD`/`SHSUB` or `UHADD`/`UHSUB`, and inserts the required low halfword. This is four
  baseline AdvSIMD instructions, preserves signed floor rounding and unsigned underflow, and leaves
  x64 and other backend fallbacks unchanged.
- The complete relevant manual pages were rendered and visually checked. Cortex-X3 issue 4.0 pages
  26 and 31-32 list halving arithmetic, element `INS`, and `REV32` at latency 2 / throughput 4.
  Cortex-A715 issue 5.0 pages 28 and 34 and Cortex-A710 issue 4.0 pages 42 and 52 list the same
  operations at latency 2 / throughput 2. Cortex-A510 issue 6.0 pages 35 and 43 list latency 3 with
  the guide's `2,1` throughput notation. All are normal AdvSIMD operations available on every Thor
  core class; no global X3 target or optional ISA feature is used.
- A disassembly-checked benchmark reproduced eight recurring exact old/new sequences, ran 2,000,000
  loop iterations per sample, alternated order over seven rounds, selected the best sample, and
  required identical nonzero checksum `0040003f`:

  | Thor core | Nine instructions -> four instructions | Result |
  | --- | --- | --- |
  | A510 CPU 0 | 10.061357 -> 4.015094 ns/op | 2.506x; 60.09% less time |
  | A715 CPU 3 | 1.671787 -> 0.715713 ns/op | 2.336x; 57.19% less time |
  | A715 CPU 4 | 1.672887 -> 0.722370 ns/op | 2.316x; 56.82% less time |
  | A710 CPU 6 | 1.670498 -> 0.715609 ns/op | 2.334x; 57.16% less time |

  CPUs 5 and 7 rejected the harmless single-bit affinity request, so no second-A710 or X3 timing is
  claimed. The source commit is `118b6beaa`, pushed directly to `origin/master` over command-line
  Git SSH.
- Thor passed 232 assertions in 15 focused `[core][arm][dynarmic]` cases and 3,099 assertions in 30
  broader `[core]~[file_sys]` cases. The permanent edge-case test covers both ASX/SAX layouts,
  negative signed halving, and unsigned subtraction underflow. The initial JDK 17 release build
  passed in 2 minutes 53 seconds; the exact committed revision rebuilt in 1 minute 31 seconds.
- The ARM64-only, v2-signed APK is 28,985,040 bytes, reports `118b6beaa-vanilla-thor`, and has
  SHA-256 `19EC345F297656964C3866F096EE5AE326929BEE303912319A5997F64500350A`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and 20.0 C,
  so this charging snapshot is not battery-discharge watt evidence.
- Cleanup preserved only that hash-verified APK plus its 476-byte metadata in `app/build`, retained
  the 3,234,326,241-byte active ARM64 `.cxx` cache, and removed 2,018,784,591 logical bytes of
  reproducible Gradle/JNI staging. Windows reported 1,576,751,104 additional free bytes on C:, to
  81,986,957,312 bytes. Temporary benchmark/test binaries and rendered manual pages were deleted
  from both host and Thor.
- This is optimization 90 in the Thor work tally. Its 2.316x-2.506x result applies only to this exact
  recurring host sequence. The 90 items overlap and cannot be added. Whole-game FPS, sustained
  watts, frametime, and thermal gains still require a matched title/scene/cache/renderer/driver/
  resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Mixed Saturated Add/Subtract

- ARM and Thumb-2 `QASX`/`QSAX`/`UQASX`/`UQSAX` previously expanded into four halfword extracts,
  signed or unsigned extensions, scalar add/subtract, two generic saturation clamps, shifts,
  masks, and repacking. The recurring signed ARM64 result path was 21 instructions before any
  one-time guest-flag spill required by its scalar `CMP` operations.
- Four packed IR operations now preserve the exchanged-halfword semantics through the backends.
  ARM64 emits `REV32`, both signed `SQADD`/`SQSUB` or unsigned `UQADD`/`UQSUB` candidates, and one
  element insert: four recurring instructions. Lazy host FPSR state is spilled before native
  saturating arithmetic so its QC side effect cannot contaminate guest FP state. The x64 backend
  uses saturated SSE word arithmetic plus `PBLENDW`, with an SSE2 `PEXTRW`/`PINSRW` fallback.
- The complete relevant manual pages were rendered and visually checked. Cortex-X3 issue 4.0 page
  26 lists these saturating AdvSIMD operations at latency 2 / throughput 4. Cortex-A715 issue 5.0
  page 28 and Cortex-A710 issue 4.0 page 42 list latency 2 / throughput 2. Cortex-A510 issue 6.0
  page 35 lists the complex saturated group at latency 4 with the guide's `2,1` throughput
  notation. The A510 latency explains why the dependency-chain improvement is smaller there.
- A disassembly-checked benchmark compared the 21-instruction signed scalar clamp/repack sequence
  with the four-instruction native operation, used an identical loop-carried dependency, ran
  8,000,000 operations per sample over four alternating-order rounds, selected the best samples,
  and required equal nonzero checksum `7fff8000`:

  | Thor core | 21 scalar instructions -> four AdvSIMD instructions | Result |
  | --- | --- | --- |
  | A510 CPU 0 | 5.018444 -> 4.523125 ns/op | 1.110x; 9.87% less time |
  | A715 CPU 3 | 3.124603 -> 1.472897 ns/op | 2.121x; 52.86% less time |
  | A715 CPU 4 | 3.125534 -> 1.459720 ns/op | 2.141x; 53.30% less time |
  | A710 CPU 5 | 2.796231 -> 1.546693 ns/op | 1.808x; 44.69% less time |

  CPU 6 and X3 CPU 7 rejected the harmless single-bit affinity request despite `0-7` being online,
  so no timing is claimed for those cores. The source/test commit is `5c8820635`, pushed directly
  to `origin/master` over command-line Git SSH.
- Thor passed all 237 assertions in 16 focused `[core][arm][dynarmic]` cases. The new permanent test
  saturates both directions for signed and unsigned ASX/SAX layouts and confirms guest NZCV, Q,
  and GE flags remain unchanged. The full native binary executed 187,784 assertions: 187,780 passed;
  the four unrelated device-environment failures were three missing build-flavor/DSP hooks and the
  existing Vulkan resource-pool device mismatch. An x86_64 Android syntax compile also accepted the
  new SSE backend. The full ARM64 compile/link passed in 1 minute 47 seconds.
- The JDK 17 release build passed in 2 minutes 47 seconds. Its ARM64-only APK is 28,984,420 bytes,
  reports `5c8820635-vanilla-thor`, and has SHA-256
  `91D496D5898718597AD73EB993E426C289D3422988AA6981D7787B6FA172ABBA`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.151 V, and 20.0 C,
  so this charging snapshot is not battery-discharge watt evidence.
- Cleanup retained the hash-verified APK and active 2,793,703,011-byte ARM64 `.cxx` cache, removed
  2,018,782,277 logical bytes of reproducible Gradle/JNI staging plus the 447,513,344-byte test ELF,
  and deleted temporary benchmark/test binaries and rendered manual pages from host and Thor.
  `app/build` now contains only the 28,984,420-byte APK and its 476-byte metadata.
- This is optimization 91 in the Thor work tally. Its 1.110x-2.141x result applies only to the
  recurring mixed-saturation host sequence and cannot be added to the other 90 items. Whole-game
  FPS, sustained watts, frametimes, and thermals still require a matched title/scene/cache/
  renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Mixed Add/Subtract With GE

- ARM and Thumb-2 `SASX`/`SSAX`/`UASX`/`USAX` already reached packed mixed-halfword IR, but ARM64
  widened both inputs, exchanged 32-bit lanes, synthesized per-lane add/sub signs with an immediate
  mask, subtracted, generated GE in the wide lanes, and narrowed. The recurring path was 10
  instructions for signed operations and 11 for unsigned operations when GE was live.
- ARM64 now uses `REV32`, narrow `ADD` and `SUB` candidates, and one halfword insert for the wrapped
  result. Signed GE uses the sign of `SHADD`/`SHSUB`, which matches the sign of the full mathematical
  result; unsigned addition uses `CMHI` for carry and unsigned subtraction uses the sign of `UHSUB`
  for no-borrow. The live-GE path is eight instructions for signed and unsigned operations. If GE
  is dead, the result-only path returns after four instructions.
- A first seven-instruction widening candidate was compiled, correctness-tested, disassembled, and
  rejected. It measured 1.384x faster on A510 CPU 0 but regressed A715 CPU 3/4 by 5.4%/6.1% and
  A710 CPU 5 by 10.9%. Its lane insert followed by `XTN` lengthened the loop-carried dependency.
  The retained eight-instruction path keeps the recurring result in halfword lanes and removes that
  final narrow.
- The complete relevant manual pages were rendered and visually checked. Cortex-X3 issue 4.0 page
  26 lists basic arithmetic and compare at latency 2 / throughput 4; pages 31-32 list element insert
  and `REV32` at latency 2 / throughput 4. Cortex-A715 issue 5.0 pages 28 and 34 list these groups at
  latency 2 / throughput 2. Cortex-A710 issue 4.0 pages 42-43 and 52 likewise list latency 2 /
  throughput 2. Cortex-A510 issue 6.0 pages 35-36 and 43 list the basic arithmetic, compare, insert,
  and reverse groups at latency 3 with the guide's `2,1` throughput notation, while `XTN` is latency
  4; this supports retaining the narrow result path and explains the rejected widening candidate.
- A disassembly-checked benchmark compared the exact old 10-instruction signed sequence with the
  retained eight-instruction sequence, unrolled eight dependency-linked operations per loop, ran
  8,000,000 operations per sample over four alternating-order rounds, selected the best samples,
  and required equal nonzero low-32-bit checksum `92009200`:

  | Thor core | 10 old instructions -> eight narrow/GE instructions | Result |
  | --- | --- | --- |
  | A510 CPU 0 | 10.063119 -> 7.542331 ns/op | 1.334x; 25.05% less time |
  | A715 CPU 3 | 2.407200 -> 1.858789 ns/op | 1.295x; 22.78% less time |
  | A715 CPU 4 | 2.365501 -> 1.842767 ns/op | 1.284x; 22.10% less time |
  | A710 CPU 5 | 2.327070 -> 2.085534 ns/op | 1.116x; 10.38% less time |
  | A710 CPU 6 | 2.327468 -> 2.272181 ns/op | 1.024x; 2.38% less time |
  | X3 CPU 7 | 2.064089 -> 1.819824 ns/op | 1.134x; 11.83% less time |

  This measures the signed live-GE sequence. The unsigned path has the same eight recurring
  instructions but different flag operations, so no unmeasured unsigned speed ratio is claimed.
  The source/test commit is `01a24248f`, pushed directly to `origin/master` over command-line Git
  SSH.
- Thor passed all 301 assertions in 17 focused `[core][arm][dynarmic]` cases. The permanent test
  covers all four instructions across 32 zero, signed-extreme, unsigned carry/borrow, and mixed-bit
  input combinations, checking wrapped results, every GE pair, and unchanged NZCV/Q. The full
  native binary executed 187,848 assertions: 187,844 passed; the same four unrelated device-
  environment failures remain (three missing build-flavor/DSP hooks and the Vulkan resource-pool
  device mismatch). The final ARM64 compile/link passed in 1 minute 2 seconds.
- The exact committed JDK 17 release build passed in 1 minute 43 seconds. Its ARM64-only APK is
  28,984,020 bytes, reports `01a24248f-vanilla-thor`, and has SHA-256
  `F7F18F9D42E8FB8A2011BD916313009193182D9C5782CCE5D4177EE0330BCA7D`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and was force-stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.155 V, and 20.0 C,
  so this charging snapshot is not battery-discharge watt evidence.
- Cleanup retained only the hash-verified APK and its 476-byte metadata in `app/build`, retained the
  2,793,887,598-byte active ARM64 `.cxx` cache, removed 2,018,862,040 logical bytes of reproducible
  Gradle/JNI staging plus the 447,537,968-byte test ELF, and deleted temporary benchmark/test
  binaries and rendered manual pages from host and Thor. Windows reported 82,422,992,896 free bytes
  on C: afterward.
- This is optimization 92 in the Thor work tally. Its 1.024x-1.334x result applies only to the
  recurring signed mixed add/subtract host sequence and cannot be added to the other 91 items.
  Whole-game FPS, sustained watts, frametimes, and thermals still require a matched title/scene/
  cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Signed Dual Multiply-Long

- ARM and Thumb-2 `SMLALD`/`SMLALDX`/`SMLSLD`/`SMLSLDX` previously expanded each guest operation
  into four signed-halfword extracts, two 32-bit `MUL`, two `SXTW`, a product add/subtract, and a
  separate 64-bit accumulator add. The recurring arithmetic path was ten AArch64 instructions.
  Dynarmic now retains signed multiply-add-long and multiply-subtract-long in IR. ARM64 emits the
  same four extracts followed by two `SMADDL`/`SMSUBL` operations: six instructions, no SIMD/GPR
  transfers, and no intermediate product materialization. The portable x64 lowering keeps the
  exact modular semantics with signed extension, multiply, and add/subtract.
- A packed AdvSIMD candidate was implemented in a temporary benchmark and rejected. `FMOV`,
  `SMULL`, `SADDLV` or `REV64`/`SUB`, and the result transfer passed 400,128 edge/random comparisons,
  but measured only 0.184x-0.840x the existing scalar speed across accessible Thor cores. The SIMD
  register crossing and horizontal reduction cost more than the removed scalar instructions.
- The complete relevant manual pages were extracted, rendered, and visually checked. Cortex-X3
  issue 4.0 page 16, Cortex-A715 issue 5.0 page 18, Cortex-A710 issue 4.0 page 21, and Cortex-A510
  issue 6.0 page 18 all list scalar `SMADDL`/`SMSUBL` at latency 2 and throughput 1. The X3/A715/A710
  tables show accumulator latency 1 in parentheses, and the notes describe late forwarding; A510
  also documents a dedicated accumulator-forwarding path. This supports two dependent widening
  MACs while avoiding the non-free general/SIMD transfers documented elsewhere in the same guides.
- The retained benchmark compared the exact old ten-instruction arithmetic sequence with the new
  six-instruction sequence. It kept four independent 64-bit accumulator chains, ran 8,000,000
  operations per sample over nine alternating-order rounds, selected the best samples, required
  equal nonzero checksums, and passed 1,001,536 signed-edge and random candidate comparisons:

  | Thor core | SMLALD | SMLALDX | SMLSLD | SMLSLDX |
  | --- | --- | --- | --- | --- |
  | A510 CPU 0 | 5.143698 -> 2.635684 ns/op; 1.952x | 4.629154 -> 2.127480; 2.176x | 5.147044 -> 2.632943; 1.955x | 4.636250 -> 2.125957; 2.181x |
  | A715 CPU 3 | 1.168119 -> 0.837760; 1.394x | 1.162038 -> 0.833157; 1.395x | 1.167949 -> 0.840397; 1.390x | 1.162246 -> 0.844349; 1.376x |
  | A715 CPU 4 | 1.165501 -> 0.839030; 1.389x | 1.158522 -> 0.840540; 1.378x | 1.167936 -> 0.837031; 1.395x | 1.162383 -> 0.838600; 1.386x |
  | A710 CPU 5 | 0.918275 -> 0.714225; 1.286x | 0.916009 -> 0.714245; 1.282x | 0.916348 -> 0.714245; 1.283x | 0.916465 -> 0.715606; 1.281x |

  CPUs 6 and 7 rejected the harmless single-bit affinity request, so no timing is claimed for
  those cores. The source/test commit is `e78d99f8c`, pushed directly to `origin/master` over
  command-line Git SSH.
- Thor passed all 685 assertions in 18 focused `[core][arm][dynarmic]` cases. The new permanent test
  contributes 384 assertions across ARM and Thumb add/subtract/exchange forms, six signed-extreme
  and 64-bit-wrap inputs, unchanged NZCV/Q/GE state, and source/destination accumulator aliasing.
  The exact committed JDK 17 ARM64 release build passed in 3 minutes 34 seconds.
- The ARM64-only APK is 28,987,612 bytes, reports `e78d99f8c-vanilla-thor`, and has SHA-256
  `3E88E73E9E93C557DB00C97F89E4886F38D6496435BA7B943291971FAF3FD307`. It installed over
  `org.azahar_emu.azahar.debug` by Wi-Fi ADB and remained stopped with no process ID; no app UI or
  game was launched. Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and
  20.0 C, so this charging snapshot is not battery-discharge watt evidence.
- Cleanup retained only the hash-verified APK and its 476-byte metadata in `app/build`, retained the
  2,794,713,569-byte active ARM64 `.cxx` cache, removed 2,018,871,738 logical bytes of reproducible
  Gradle/JNI staging plus the 447,574,992-byte test ELF and 55,154,008 bytes of temporary host
  benchmarks/manual renders, and deleted all temporary benchmark/test binaries from Thor. Windows
  recovered 2,079,604,736 physical bytes and reported 82,166,231,040 bytes free on C: afterward.
- This is optimization 93 in the Thor work tally. Its 1.281x-2.181x result applies only to the
  affected signed dual multiply-long host sequence and cannot be added to the other 92 items.
  Whole-game FPS, sustained watts, frametimes, and thermals still require a matched title/scene/
  cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Signed Multiply-Accumulate-Long

- ARM and Thumb-2 plain `SMLAL` previously expanded its signed 32x32-bit accumulation into two
  `SXTW`, one 64-bit `MUL`, and one 64-bit `ADD`. Dynarmic now retains the operation as generic
  signed multiply-add-long IR, which ARM64 emits as one `SMADDL`. ARM and Thumb-2
  `SMLALBB`/`SMLALBT`/`SMLALTB`/`SMLALTT` retain their two required signed-halfword extracts but
  replace `MUL`, product `SXTW`, and accumulator `ADD` with `SMADDL`: five arithmetic instructions
  become three. The generic backend continues to preserve exact modulo-64-bit accumulation.
- The complete relevant manual pages were text-extracted, rendered, and visually checked.
  Cortex-X3 issue 4.0 page 16, Cortex-A715 issue 5.0 page 18, Cortex-A710 issue 4.0 page 21, and
  Cortex-A510 issue 6.0 page 18 all list `SMADDL` at latency 2 and throughput 1. X3/A715/A710 show
  accumulator latency 1 and describe late forwarding. A510 documents a dedicated MAC accumulator
  forwarding path; its table also lists X-form `MUL` at latency 4 and throughput 1/2, making removal
  of the old 64-bit multiply especially valuable on the efficiency cluster.
- `llvm-objdump` verified the temporary benchmark's exact repeated bodies: plain baseline had
  `SXTW`, `SXTW`, `MUL`, `ADD`, while the candidate had one `SMADDL`; halfword baseline had `SXTH`,
  `SXTH`, `MUL`, `SXTW`, `ADD`, while the candidate had `SXTH`, `SXTH`, `SMADDL`. Each sample used
  four independent accumulators and 5,000,000 loop iterations, or 20,000,000 affected guest
  operations, over nine alternating-order rounds. Baseline and fused checksums matched before
  timing. Median results were:

  | Thor core | Plain `SMLAL` | Halfword `SMLALxy` |
  | --- | --- | --- |
  | A510 CPU 0 | 2.648721 -> 0.505107 ns/op; 5.244x | 3.146227 -> 1.509789 ns/op; 2.084x |
  | A715 CPU 3 | 0.449122 -> 0.358919 ns/op; 1.251x | 0.554815 -> 0.358698 ns/op; 1.547x |
  | A715 CPU 4 | 0.448365 -> 0.358456 ns/op; 1.251x | 0.558607 -> 0.358576 ns/op; 1.558x |
  | A710 CPU 5 | 0.381510 -> 0.358453 ns/op; 1.064x | 0.471432 -> 0.358344 ns/op; 1.316x |

  CPUs 6 and 7 rejected the single-bit affinity request with `EINVAL`, so no X3 timing is claimed.
  Thor reported USB power, no AC/wireless power, 80% battery, 4.153 V, and 20.0 C; this is not a
  battery-discharge watt measurement.
- A permanent regression covers ARM and Thumb plain and BB/TT halfword forms, ARM flag-setting,
  source/destination aliases, six signed-extreme and accumulator-wrap inputs, and unchanged
  C/V/Q/GE state. It passed 282 assertions on Thor. The complete focused `[core][arm][dynarmic]`
  run passed 967 assertions in 19 cases. The source/test commit is `5afbf2dc6`, pushed directly to
  `origin/master` over command-line Git SSH. The exact JDK 17 ARM64 release build passed in 1 minute
  32 seconds.
- The installed ARM64 APK is 28,986,836 bytes, reports `5afbf2dc6-vanilla-thor`, and has SHA-256
  `4D72233FA3DB0BBD04D0639049556E843BC6F76D6C44E5A95E4A78CF45314D58`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; the app remained stopped and no game was launched.
- Cleanup removed the temporary manual renders, benchmark source/binary, stripped Thor test copy,
  device copies, 447,592,144-byte native test ELF, and reproducible Gradle/JNI/R8/native-symbol
  staging. It retained the APK plus its 476-byte metadata and the 2,788,792,017-byte active ARM64
  CMake/Ninja cache. Total logical removal was 2,493,334,236 bytes; C: recovered 2,051,178,496
  physical bytes and reported 82,094,518,272 bytes free afterward.
- This is optimization 94 in the Thor work tally. Its 1.064x-5.244x figures apply only to the
  affected signed multiply-accumulate-long host sequence and cannot be added to the other 93 items.
  Whole-game FPS, sustained watts, frametimes, and thermals still require a matched title/scene/
  cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Unsigned Widening Multiply

- ARM and Thumb-2 `UMULL` and `UMLAL` previously zero-extended two 32-bit inputs into 64-bit IR and
  then used generic `Mul64`. ARM64 consequently emitted X-form `MUL`, even though the guest
  operation is exactly a 32x32-to-64-bit unsigned widening multiply. The new generic
  `UnsignedMultiplyLong(U32, U32) -> U64` IR operation emits native `UMULL Xd, Wn, Wm` on ARM64.
  The x64 backend zeroes both 32-bit scratch registers before its 64-bit `IMUL` polyfill. ARM and
  Thumb `UMLAL` add the packed accumulator after `UMULL`; `UMULL` consumes the result directly.
  `UMAAL` was deliberately left unchanged.
- The complete relevant manual pages were text-extracted, rendered, and visually checked.
  Cortex-X3 issue 4.0 page 16, Cortex-A715 issue 5.0 page 18, and Cortex-A710 issue 4.0 page 21 list
  `MUL`/the `UMULL` alias at throughput 2 while `UMADDL` has throughput 1 and accumulator latency 1.
  Cortex-A510 issue 6.0 page 18 lists X-form `MUL` at latency 4 and throughput 1/2, versus latency 2
  and throughput 1 for the widening long-multiply form. That predicts the selected lowering's large
  A510 win and big-core parity.
- `llvm-objdump` verified the exact temporary benchmark bodies: the baseline used four independent
  X-form `MUL` results; the candidate used four independent `UMULL X, W, W` results. `UMLAL` added
  one 64-bit `ADD` to each chain, while the rejected `UMAAL` candidate added two. Each sample ran
  20,000,000 loop iterations, or 80,000,000 affected operations, for nine alternating-order rounds;
  all baseline/candidate checksums matched. Median accepted-path ratios were:

  | Thor core | `UMULL`: X-form `MUL` -> native `UMULL` | `UMLAL`: X-form `MUL` + `ADD` -> `UMULL` + `ADD` |
  | --- | --- | --- |
  | A510 CPU 0 | 1.9965x | 1.7940x |
  | A715 CPU 3 | 1.0004x | 0.9987x |
  | A715 CPU 4 | 1.0007x | 0.9977x |
  | A710 CPU 6 | 1.0003x | 1.0007x |
  | X3 CPU 7 | 0.9998x | 1.0005x |

  A510's raw medians were 80,629,271 -> 40,385,781 ns for `UMULL` and 90,624,532 ->
  50,515,729 ns for `UMLAL`. The sub-0.31% movements on accepted big-core paths are treated as
  parity/noise, not speed claims.
- Direct `UMADDL` was rejected despite being about 2.24x faster than the old `UMLAL` sequence on
  A510: it measured only about 0.766x-0.769x on A715, 0.664x on A710, and 0.516x on X3. Fused
  `UMAAL` variants had the same asymmetric problem. Merely reassociating `UMAAL` helped A510 by
  about 44%, A715 by 5.7%-6.9%, and A710 by 9.4%, but a longer X3 run measured 0.9498x. Applying
  native `UMULL` to `UMAAL` itself measured 0.9776x on X3, so `UMAAL` remains unchanged.
- The permanent regression covers ARM `UMLAL`/`UMLALS`/`UMULL`/`UMULLS`, Thumb-2
  `UMLAL`/`UMULL`, source/destination aliases, six unsigned-extreme and 64-bit-wrap inputs, ARM N/Z
  updates, and unchanged C/V/Q/GE state. The complete Thor `[core][arm][dynarmic]` run passed 1,217
  assertions in 19 cases. The source/test commit is `dd02d1b5b`, pushed directly to
  `origin/master` over command-line Git SSH. The clean JDK 17 release-style build passed in 3 minutes
  24 seconds; the exact post-commit rebuild passed in 1 minute 42 seconds.
- The installed ARM64 APK is 28,985,276 bytes, reports `dd02d1b5b-vanilla-thor`, and has SHA-256
  `FBB82CB04CF865E2F31B9F395501C3FBA5A1036B22AC72C46B9E3E4A821D69AF`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, and no game was launched.
- Cleanup removed the temporary benchmark source/binary, four manual renders, stripped Thor test
  copy, both device copies, 447,611,496-byte native test ELF, and reproducible Gradle/JNI/R8/native-
  symbol staging. It retained the 28,985,276-byte APK plus 476-byte metadata and the
  2,795,665,346-byte active ARM64 CMake/Ninja cache. Total logical removal was 2,493,397,879 bytes;
  C: recovered 2,053,197,824 physical bytes and reported 82,027,966,464 bytes free afterward.
- This is optimization 95 in the Thor work tally. Its 1.794x-1.997x figures apply only to the
  affected unsigned widening-multiply host sequences on A510; the accepted big-core paths were
  parity. It cannot be added to the other 94 items. Whole-game FPS, sustained watts, frametimes,
  and thermals still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Signed Widening Multiply

- ARM and Thumb-2 `SMULL` previously sign-extended both 32-bit operands into 64-bit IR and then
  used generic `Mul64`. ARM64 emitted `SXTW`, `SXTW`, and X-form `MUL` for each guest operation.
  The new generic `SignedMultiplyLong(U32, U32) -> U64` IR operation emits one native
  `SMULL Xd, Wn, Wm`. The x64 backend preserves the same semantics with two `MOVSXD` operations
  and 64-bit `IMUL`; the generic modulo-64-bit result remains unchanged.
- The complete relevant manual pages were rendered and visually checked. Cortex-X3 issue 4.0 page
  16 and Cortex-A710 issue 4.0 page 21 list multiply-long `SMULL` at latency 2 and throughput 2 on
  the M pipelines. Cortex-A715 issue 5.0 page 18 states that the `SMULL` zero-accumulator alias can
  execute on M at throughput 2. Cortex-A510 issue 6.0 page 18 lists X-form `MUL` at latency 4 and
  throughput 1/2, versus latency 2 and throughput 1 for the long multiply-accumulate form that
  encodes `SMULL` when its accumulator is zero.
- `llvm-objdump` verified the exact temporary loop bodies. The baseline repeated four independent
  groups of `SXTW`, `SXTW`, `MUL`; the candidate repeated four independent `SMULL X, W, W`
  instructions. Each sample ran 20,000,000 iterations, or 80,000,000 signed products, for nine
  alternating-order rounds. All warmup and timed checksums matched. Median results were:

  | Thor core | Baseline -> native `SMULL` | Result |
  | --- | --- | --- |
  | A510 CPU 0 | 141,038,021 -> 40,297,917 ns | 3.499884x |
  | A715 CPU 3 | 30,355,469 -> 16,418,489 ns | 1.848859x |
  | A715 CPU 4 | 30,369,948 -> 16,633,959 ns | 1.825780x |
  | A710 CPU 6 | 23,290,677 -> 14,327,969 ns | 1.625539x |
  | X3 CPU 7 | 20,128,438 -> 12,582,187 ns | 1.599757x |

  MIDRs were re-read immediately before the run: CPU 0 `0x411fd461`, CPUs 3-4 `0x411fd4d0`, CPU 6
  `0x412fd470`, and CPU 7 `0x411fd4e0`. Thor reported USB power, no AC/wireless power, 80% battery,
  4.160 V, and 21.0 C. That is useful thermal context, not a battery-discharge watt measurement.
- The permanent regression covers ARM `SMULL`/`SMULLS`, Thumb-2 `SMULL`, source/destination aliases,
  seven signed-extreme/zero inputs, ARM N/Z updates, unchanged C/V/Q/GE, and complete 64-bit
  results. The complete Thor `[core][arm][dynarmic]` run passed 1,364 assertions in 20 cases. The
  source/test commit is `f511c52f8`, pushed directly to `origin/master` over command-line Git SSH.
  The clean JDK 17 release-style build passed in 3 minutes 7 seconds; the exact post-commit rebuild
  passed in 1 minute 35 seconds.
- The installed ARM64 APK is 28,984,800 bytes, reports `f511c52f8-vanilla-thor`, and has SHA-256
  `2E68C3E83CF13AB444C7321DE749B98D8DE9617EFD84983B9DE107FD57944F99`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, and no game was launched.
- Cleanup removed the benchmark/encoding source and binaries, four manual renders, stripped Thor
  test copy, both device copies, 447,640,208-byte native test ELF, and reproducible Gradle/JNI/R8/
  native-symbol staging. It retained the 28,984,800-byte APK plus 476-byte metadata and the
  2,796,499,773-byte active ARM64 CMake/Ninja cache. Total logical removal was 2,493,530,904 bytes;
  C: recovered 2,053,332,992 physical bytes and reported 81,954,062,336 bytes free afterward.
- This is optimization 96 in the Thor work tally. Its 1.600x-3.500x result applies only to the
  affected signed widening-multiply host sequence and cannot be added to the other 95 items.
  Whole-game FPS, sustained watts, frametimes, and thermals still require a matched title/scene/
  cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Signed High-Word Multiply

- ARM and Thumb-2 `SMMUL{R}`, `SMMLA{R}`, and `SMMLS{R}` still expanded signed 32-bit operands to
  64-bit IR and used generic `Mul64`. ARM64 emitted `SXTW`, `SXTW`, and X-form `MUL`. The
  accumulate/subtract forms also built `(Ra << 32)` with a zero register plus `BFI` before a
  generic 64-bit `ADD`/`SUB`. The rounding forms then consumed intermediate bit 31 as before.
- The frontend now keeps these operations native without adding backend-specific semantics.
  `SMMUL` uses `SignedMultiplyLong(U32, U32) -> U64`; `SMMLA` and `SMMLS` zero-extend `Ra`, shift it
  left 32 bits, and use the established `SignedMultiplyAddLong`/`SignedMultiplySubtractLong` IR.
  ARM64 consequently emits `SMULL`, or `LSL` plus `SMADDL`/`SMSUBL`, before the existing high-word
  extraction. x64 retains the established exact signed-extend/multiply/add-subtract polyfill.
- `llvm-objdump` verified all exact temporary loop bodies. Each function repeated four independent
  guest-equivalent operations for 10,000,000 iterations, or 40,000,000 affected operations per
  sample, across nine rotating-order rounds. Warmup and timed checksums matched for every
  baseline, split, and fused form. Median speedups for the selected native paths were:

  | Thor core | `SMMUL`: old -> `SMULL` | `SMMLA`: old -> fused | `SMMLS`: old -> fused |
  | --- | --- | --- | --- |
  | A510 CPU 0 | 2.000438x | 2.120779x | 2.129672x |
  | A715 CPU 3 | 1.745992x | 1.588502x | 1.589313x |
  | A715 CPU 4 | 1.748796x | 1.608305x | 1.597712x |
  | A710 CPU 5 | 1.586090x | 1.573429x | 1.575366x |

  Fused `SMADDL`/`SMSUBL` also beat the native split `SMULL` plus `ADD`/`SUB` candidate by
  6.0%-6.4% on A510, 9.4% on A710, and 14.6%-16.1% on A715. CPU 5's MIDR was re-read as
  `0x412fd470`, confirming Cortex-A710. CPU 6 and X3 CPU 7 rejected the harmless single-bit
  affinity request during this run despite being online and clocked, so no fused timing is claimed
  for them. The prior isolated native-`SMULL` sprint already measured its exact component 1.600x
  on X3.
- The permanent regression covers ARM and Thumb plain/rounded multiply, accumulate, and subtract,
  source/destination aliases, six signed-extreme and wrap inputs, exact intermediate-bit-31
  rounding with 32-bit result wrap, and unchanged NZCV/Q/GE. Thor passed all 1,580 assertions in 21
  focused `[core][arm][dynarmic]` cases. The source/test commit is `78639ae10`, pushed directly to
  `origin/master` over command-line Git SSH. The initial JDK 17 ARM64 release build passed in 2
  minutes 50 seconds; the exact post-commit rebuild passed in 1 minute 38 seconds.
- The installed ARM64 APK is 28,985,496 bytes, reports `78639ae10-vanilla-thor`, and has SHA-256
  `71F66CD938E55CD13C674548EA9AFEBE86BD74ED751D05E3E0D420171E78B93E`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; the app remained stopped and no game was launched. Thor
  reported USB power, no AC/wireless power, 80% battery, 4.160 V, and 21.0 C. That charging context
  is not a battery-discharge watt measurement.
- Cleanup removed the 25,983,736-byte stripped Thor test copy, 11,568-byte device benchmark, local
  benchmark/encoding/test artifacts, 447,654,976-byte native test ELF, `.cxx` tool metadata, and
  reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,985,496-byte APK plus
  476-byte metadata and the 2,790,604,448-byte active ARM64 CMake/Ninja cache. Total logical host
  removal was 2,498,682,926 bytes; C: recovered 2,056,519,680 physical bytes and reported
  81,892,618,240 bytes free afterward.
- This is optimization 97 in the Thor work tally. Its 1.573x-2.130x figures apply only when the
  guest executes these signed high-word multiply instructions; they cannot be added to the other
  96 items. Whole-game FPS, sustained watts, frametimes, and thermals still require a matched
  title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Signed Word-by-Halfword Multiply

- ARM and Thumb-2 `SMULWB`/`SMULWT` previously sign-extended the full-word operand and selected
  signed halfword into 64-bit IR, multiplied them with generic `Mul64`, shifted right 16 bits, and
  kept the low word. ARM64 emitted `SXTH`, two `SXTW`, X-form `MUL`, and `LSR` for the bottom form;
  the top form also needed `LSR` plus `SXTH` to select its halfword. The frontend now keeps the
  selected halfword as a signed `U32` and uses established `SignedMultiplyLong(U32, U32) -> U64`.
  ARM64 emits `SXTH + SMULL + LSR` for `SMULWB` and `ASR + SMULL + LSR` for `SMULWT`, removing two
  or three recurring host instructions without adding backend-specific semantics. The portable
  x64 signed-extend/multiply polyfill remains exact.
- The recorded Cortex manuals support testing this candidate rather than assuming it: X3 and A710
  list multiply-long `SMULL` at latency 2 and throughput 2, A715 lists its zero-accumulator alias at
  throughput 2, and A510 lists X-form `MUL` at latency 4/throughput 1/2 versus latency 2/throughput
  1 for the long form. `llvm-objdump` then verified every exact baseline and candidate loop body.
  Each sample ran four independent guest-equivalent operations for 10,000,000 iterations, or
  40,000,000 affected operations, over nine alternating-order rounds. Warmup and timed checksums
  matched. Median results were:

  | Thor core | `SMULWB`: old -> native | `SMULWT`: old -> native |
  | --- | --- | --- |
  | A510 CPU 0 | 171,532,917 -> 105,741,511 ns; 1.622191x | 191,485,833 -> 85,609,896 ns; 2.236725x |
  | A715 CPU 3 | 22,748,073 -> 15,600,573 ns; 1.458156x | 26,480,625 -> 15,606,146 ns; 1.696807x |
  | A710 CPU 5 | 18,885,781 -> 12,078,125 ns; 1.563635x | 22,650,260 -> 12,087,500 ns; 1.873858x |
  | X3 CPU 7 | 17,290,261 -> 10,059,115 ns; 1.718865x | 20,440,364 -> 10,067,187 ns; 2.030395x |

- The same candidate was benchmarked but deliberately rejected for `SMLAWB`/`SMLAWT`. Its full
  path included the architectural `ADDS`, overflow extraction, guest-Q load/OR/store, and the same
  checksum lock. It improved A510 by 1.393200x/1.704470x but repeated high-sample medians were
  0.997959x/0.998901x on A715 and 0.994340x/0.989918x on X3; A710 was effectively tied at
  1.001064x/1.000844x. The accumulate forms therefore retain their existing generic lowering
  instead of trading an efficiency-core win for a measurable larger-core regression.
- The permanent regression covers ARM and Thumb-2 top/bottom forms, destination/source aliases,
  six signed-extreme and distinct-halfword inputs, exact product bits 16-47, unchanged NZCV/Q/GE,
  and untouched source registers. Thor passed all 1,748 assertions in 22 focused
  `[core][arm][dynarmic]` cases. The source/test commit is `50e746101`, pushed directly to
  `origin/master` over command-line Git SSH. The initial JDK 17 ARM64 release build passed in 2
  minutes 50 seconds; the exact post-commit rebuild passed in 1 minute 39 seconds.
- The installed ARM64 APK is 28,984,936 bytes, reports `50e746101-vanilla-thor`, and has SHA-256
  `594854C8486FD4AF6A9CB8F9E8B8B96E880AC5442E854FA805926FE8E2449D31`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; the empty process-ID check confirmed that it remained stopped,
  and no app UI or game was launched. Thor reported USB power, no AC/wireless power, 80% battery,
  4.158 V, and 21.0 C. That charging context is not a battery-discharge watt measurement.
- Cleanup deleted both device binaries, all local benchmark/encoding/stripped-test artifacts, the
  447,676,816-byte native test ELF, and reproducible Gradle/JNI/R8/native-symbol staging. It
  retained the APK plus its 476-byte metadata and the 2,796,808,993-byte active ARM64 CMake/Ninja
  cache. Total logical host removal was 2,466,676,050 bytes; C: recovered 2,020,790,272 physical
  bytes and reported 81,818,632,192 bytes free afterward.
- This is optimization 98 in the Thor work tally. Its 1.458x-2.237x figures apply only when the
  guest executes these signed word-by-halfword multiply instructions; they cannot be added to the
  other 97 items. Whole-game FPS, sustained watts, frametimes, and thermals still require a matched
  title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Vector Widening Shift

- A32 NEON `VSHLL.S/U8`, `.S/U16`, and `.S/U32` previously became a signed or unsigned vector
  extension followed by a generic logical left shift. ARM64 therefore emitted `SXTL`/`UXTL`
  (the zero-shift `SSHLL`/`USHLL` aliases) and then `SHL`, even though AArch64 directly encodes the
  complete operation as one `SSHLL`/`USHLL` with the guest immediate.
- The recorded Cortex manuals support the candidate without substituting manual tables for device
  evidence. The X3 guide lists the basic AdvSIMD immediate-shift family, including `SHL`, `SHLL`,
  `SSHLL`, `SXTL`, `USHLL`, and `UXTL`, at latency 2 and throughput 2. A715 and A710 list latency 2
  and throughput 1; A510 lists latency 3 with its dual throughput notation. This made instruction
  fusion plausible on every Thor core class, but the exact sequences were still benchmarked on the
  physical device before source changed.
- The ARM64 backend now aliases an extension to its narrow source only when it has exactly one use,
  the immediately following matching-width logical shift consumes it as argument zero, and the
  immediate is smaller than the original narrow element width. The shift consumer then emits one
  signed or unsigned native widening shift. The extension-side and consumer-side predicates are
  deliberately symmetrical: shared, non-adjacent, mismatched-width, non-immediate, or out-of-range
  IR retains `SXTL`/`UXTL` plus `SHL`, so a rejected fusion can never feed raw narrow data into the
  generic wide-shift fallback.
- `llvm-objdump` verified twelve exact loop bodies: signed/unsigned 8-to-16 at shift 3,
  signed/unsigned 16-to-32 at shift 11, and signed/unsigned 32-to-64 at shift 19. Each timed sample
  ran four independent operations for 5,000,000 iterations, or 20,000,000 affected operations,
  across nine alternating-order rounds. Warmup and every timed baseline/candidate checksum matched;
  the final checksum lock was nonzero for all three widths. Median speedups were:

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VSHLL.S8` | 4.020344x | 2.004667x | 2.003952x | 2.804748x |
  | `VSHLL.U8` | 4.136257x | 1.999144x | 1.997490x | 2.802131x |
  | `VSHLL.S16` | 4.011369x | 1.999516x | 2.000597x | 2.805524x |
  | `VSHLL.U16` | 4.026792x | 2.001173x | 2.000801x | 2.795824x |
  | `VSHLL.S32` | 4.015906x | 2.002867x | 2.000681x | 2.805015x |
  | `VSHLL.U32` | 4.034313x | 2.001538x | 2.000752x | 2.804717x |

- The permanent A32 regression covers all six signed/unsigned widths, shifts 1 through the maximum
  legal immediate, low and high D/Q registers, complete destination/source overlap, partial overlap,
  preserved non-overlapping sources, untouched unrelated SIMD state, and unchanged CPSR N/C/Q/GE.
  Thor passed all 1,760 assertions in 23 focused `[core][arm][dynarmic]` cases. The source/test
  commit is `5a538cee2`, pushed directly to `origin/master` over command-line Git SSH. The first
  release-style source/test build passed in 3 minutes; the final refined native build passed in 1
  minute 21 seconds; and the exact post-commit release rebuild passed in 1 minute 41 seconds.
- The installed ARM64 APK is 28,986,288 bytes, reports `5a538cee2-vanilla-thor`, and has SHA-256
  `EEB75684F0F965AFDAE95C7043CD7CAD09298DA6A828D2AB628713964440A01F`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was empty,
  and no app UI or game was launched. Thor reported USB power, no AC/wireless power, 79% battery,
  4.150 V, and 23.0 C. That charging context is not a battery-discharge watt measurement.
- Cleanup deleted the local benchmark/encoding/stripped-test artifacts, four rendered manual pages,
  both temporary device binaries, the 447,734,920-byte native test ELF, `.cxx` tool metadata, and
  reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,986,288-byte APK plus its
  476-byte metadata and the 2,791,133,813-byte active ARM64 CMake/Ninja cache. Total logical host
  removal was 2,499,799,755 bytes; C: recovered 2,051,805,184 physical bytes and reported
  81,704,198,144 bytes free afterward.
- This is optimization 99 in the Thor work tally, not 78. Its 1.997x-4.136x figures apply only when
  the guest executes these widening-shift forms and cannot be added to the other 98 items. Whole-game
  FPS, sustained watts, frametimes, and thermals still require a matched title/scene/cache/renderer/
  driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Maximum-Width Vector Shift-Long

- A32 `VSHLL.I8`, `VSHLL.I16`, and `VSHLL.I32`, plus A64 `SHLL`, use the maximum shift equal to the
  original element width. Their frontend IR is an adjacent zero extension followed by a logical
  shift by 8, 16, or 32. Optimization 99 deliberately required a smaller immediate because native
  `SSHLL`/`USHLL` only encode 0 through width-minus-one, so these maximum forms still emitted
  `UXTL` plus `SHL`.
- The recorded Cortex-X3, A715, A710, and A510 software optimization guides explicitly group
  `SHLL` with the basic AdvSIMD immediate-shift family. They list latency/throughput as 2/2 on X3,
  2/1 on A715 and A710, and latency 3 with A510's `2,1` throughput notation. Exact AArch64 assembly
  and `llvm-objdump` then verified that the candidate loop contained one `SHLL` where the baseline
  contained `UXTL` plus `SHL`, with otherwise identical loop control.
- The benchmark ran four independent vector operations per loop for 5,000,000 iterations, or
  20,000,000 affected operations per sample, across nine alternating-order rounds. Warmup and timed
  checksums matched and remained nonzero. Median results were:

  | Guest form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VSHLL.I8 #8` | 43,724,323 -> 10,239,271 ns; 4.270257x | 2.001835x | 1.999951x | 2.803131x |
  | `VSHLL.I16 #16` | 43,645,052 -> 10,329,427 ns; 4.225312x | 1.998722x | 2.000696x | 2.801387x |
  | `VSHLL.I32 #32` | 43,650,364 -> 10,739,583 ns; 4.064438x | 2.000654x | 1.999606x | 2.801009x |

- ARM64 Dynarmic now accepts equality only for the adjacent, sole-use zero-extension shape and emits
  native `SHLL`; signed extension remains limited to a smaller immediate, and larger, shared,
  non-adjacent, mismatched, or non-immediate forms retain the generic path. The extension alias and
  consumer predicates use the same rule so a rejected fusion cannot expose an unextended operand.
  Permanent A32 cases cover all three encodings, exact results, source preservation, unrelated SIMD
  state, CPSR state, high registers, and partial source/destination overlap.
- The release build passed and Thor passed all 1,766 assertions in 23 focused
  `[core][arm][dynarmic]` cases. Source/test commit `03e97ef1e` was pushed directly to
  `origin/master` over command-line Git SSH. The initial release build passed in 2 minutes 57 seconds;
  the exact post-commit release rebuild passed in 1 minute 42 seconds.
- The installed ARM64 APK is 28,986,332 bytes, reports `03e97ef1e-vanilla-thor`, and has SHA-256
  `26A636CBCB7532E0B40D6EEC4FF3A864C382B01DA5FAB3D639B344EFC661FA46`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was empty,
  and no app UI or game was launched. Thor reported USB power, no AC/wireless power, 78% battery,
  4.126 V, and 23.0 C. That charging context is not a battery-discharge watt measurement.
- Cleanup deleted both device binaries, all local benchmark/object/stripped-test artifacts, four
  rendered manual pages, the 447,741,832-byte native test ELF, `.cxx` tool metadata, and reproducible
  Gradle/JNI/R8/native-symbol staging. It retained the 28,986,332-byte APK plus its 476-byte metadata
  and the 2,791,288,080-byte active ARM64 CMake/Ninja cache. Total logical host removal was
  2,499,901,607 bytes; C: recovered 2,059,575,296 physical bytes and reported 81,604,730,880 bytes
  free afterward.
- This is optimization 100 in the overlapping Thor work tally. Its 1.999x-4.270x figures apply only
  to these maximum-width widening shifts and cannot be added to the other 99 items. Whole-game FPS,
  sustained watts, frametimes, and thermals still require a matched title/scene/cache/renderer/
  driver/resolution/layout/mode/fan/brightness/duration A/B run.

## 2026-08-18 Dynarmic Native Shift-Right Narrowing

- Before this sprint, `upstream/master` was fetched and merged. The only new upstream commit was
  `db15d78fe`, a runtime-neutral copyright-header sweep across 764 files; merge commit `fe9136656`
  was pushed to `origin/master`. It changes no emulator hot path, so no speed or power result is
  attributed to the sync.
- A32 NEON `VSHRN`, `VQSHRN.S`, `VQSHRN.U`, and `VQSHRUN.S` for 16-to-8, 32-to-16, and 64-to-32-bit
  elements previously lowered to a vector right shift followed by a separate narrow. ARM64 emitted
  `USHR + XTN`, `SSHR + SQXTN`, `USHR + UQXTN`, or `SSHR + SQXTUN`. AArch64 directly represents
  these exact non-rounding pairs as `SHRN`, `SQSHRN`, `UQSHRN`, and `SQSHRUN`.
- The recorded Cortex manuals support measuring this fusion on each Thor core class. A510 lists
  basic and saturating fused shift-narrow families at latency 4 with `2,1` throughput notation.
  A710 and A715 list basic `SHRN` at latency 2/throughput 1 and the saturating family at latency
  4/throughput 1. X3 lists the basic family at latency 2/throughput 2 and the saturating family at
  latency 4/throughput 2. These tables motivated the experiment; the physical-device results below
  determine the claim.
- `llvm-objdump` verified all 24 exact loop bodies: the baseline has the expected shift plus narrow,
  the candidate has one fused instruction, and loop control is otherwise identical. Each timed
  sample ran four independent vector operations for 5,000,000 iterations, or 20,000,000 affected
  operations, across nine alternating-order rounds. Warmup and timed checksums matched and remained
  nonzero. Median speedups were:

  | Guest form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VSHRN.I16/I32/I64` | 4.029602x / 4.002076x / 3.988138x | 0.999242x / 1.002312x / 0.999635x | 1.000478x-1.001940x | 0.999432x-1.000630x |
  | `VQSHRN.S16/S32/S64` | 4.009519x / 3.994325x / 4.178109x | 1.9977x-2.0031x | 1.995907x-2.002566x | 2.798938x-2.804810x |
  | `VQSHRN.U16/U32/U64` | 3.731329x / 4.357201x / 3.858466x | 1.9977x-2.0031x | 1.995907x-2.002566x | 2.798938x-2.804810x |
  | `VQSHRUN.S16/S32/S64` | 3.773881x / 4.001312x / 4.404425x | 1.9977x-2.0031x | 1.995907x-2.002566x | 2.798938x-2.804810x |

  The grouped ranges are retained where the individual captured values were not needed to select
  the same all-width lowering. Plain `VSHRN` is throughput-neutral on A715/A710/X3 but still halves
  its vector instruction count; its roughly 4x A510 result makes it worthwhile without sacrificing
  larger-core throughput.
- ARM64 Dynarmic now aliases the shift to its raw source only when it has one use, the immediately
  following exact-width narrow consumes it as argument zero, and the constant shift is 1 through
  half the source width. The consumer emits the matching fused instruction. Saturating forms load
  FPSR before emission so the guest sticky FPSCR.QC behavior remains intact. Shared, non-adjacent,
  mismatched, non-immediate, zero, or out-of-range IR retains the original two-instruction path.
  Rounding forms are deliberately excluded because their frontend includes a different rounding-
  correction DAG and needs its own correctness proof.
- The permanent regression covers all four instruction families and all three widths, low and high
  registers, partial destination/source overlap at D31/Q15, unrelated-register preservation, exact
  results, CPSR preservation, initial FPSCR state, and sticky QC. The release build passed in 7
  minutes 58 seconds after the broad upstream header rebuild, Thor passed all 1,823 assertions in
  24 focused `[core][arm][dynarmic]` cases, and the exact post-commit rebuild passed in 2 minutes 14
  seconds. Source/test commit `83483cbbd` was pushed directly to `origin/master` with command-line
  Git over SSH.
- The installed ARM64 APK is 28,988,724 bytes, reports `83483cbbd-vanilla-thor`, and has SHA-256
  `CA833539D408CD92631115F6F16E86328ACD4D1E8A8A793F642F08B4E1810992`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor reported USB power, no AC/wireless power, 77%
  battery, 3.982 V, and 23.0 C. That charging context is not a battery-discharge watt measurement.
- Cleanup deleted both temporary device binaries, the 26,007,784-byte stripped host test copy,
  local benchmark/object/source scratch, eight rendered manual pages, the 447,866,752-byte native
  test ELF, `.cxx` tool metadata, and reproducible Gradle/JNI/R8/native-symbol staging. It retained
  the 28,988,724-byte APK plus 476-byte metadata and the 2,794,679,241-byte active ARM64 CMake/Ninja
  cache. Total logical host removal was 2,500,714,051 bytes; C: recovered 2,060,034,048 physical
  bytes and reported 81,544,265,728 bytes free afterward.
- This is optimization 101 in the overlapping Thor work tally. The approximately 1.00x-4.40x
  figures apply only when the guest executes these exact shift-right-narrow forms and cannot be
  added to the other 100 items. Whole-game FPS, sustained watts, frametimes, and thermals still
  require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B run.

## ARM64 Rounding Shift-Right Narrow Fusion (2026-08-18)

- A32 NEON `VRSHRN`, `VQRSHRN.S`, `VQRSHRN.U`, and `VQRSHRUN.S` for 16-to-8, 32-to-16, and
  64-to-32-bit elements previously used an overflow-safe rounding correction before narrowing.
  The frontend emitted a right shift, broadcast rounding bit, AND, equality mask, subtract-as-add,
  and the selected narrow. ARM64 materialized that as `MOV + DUP + USHR/SSHR + AND + CMEQ + SUB`
  followed by `XTN`, `SQXTN`, `UQXTN`, or `SQXTUN`. AArch64 directly represents the exact operation
  as `RSHRN`, `SQRSHRN`, `UQRSHRN`, or `SQRSHRUN`.
- The recorded Cortex manuals explicitly list the native rounding forms. A510 lists the A64
  complex immediate-shift family at latency 4 with `2,1` throughput notation; its A32 table lists
  `VRSHRN` at latency 3 and the saturating rounding forms at latency 4. A710 and A715 list the A64
  complex family at latency 4/throughput 1, while X3 lists latency 4/throughput 2. These tables
  justified an exact all-core experiment; they are not substitutes for the physical result.
- `llvm-objdump` verified every baseline and candidate body. Each sample ran four independent
  vector operations for 1,000,000 iterations, or 4,000,000 affected guest operations, across nine
  alternating-order rounds. Warmup and timed checksums matched and remained nonzero. Median
  speedups were:

  | Guest form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VRSHRN.I16` | 13.885256x | 3.001884x | 3.472421x | 3.598723x |
  | `VRSHRN.I32` | 14.116638x | 3.004390x | 3.435994x | 3.507308x |
  | `VRSHRN.I64` | 14.715745x | 2.911880x | 3.310005x | 3.520017x |
  | `VQRSHRN.S16` | 14.225325x | 3.343469x | 3.587415x | 3.771264x |
  | `VQRSHRN.S32` | 14.667758x | 3.419528x | 3.472222x | 3.823051x |
  | `VQRSHRN.S64` | 14.499985x | 3.160713x | 3.250409x | 3.959625x |
  | `VQRSHRN.U16` | 13.547762x | 3.316225x | 3.398665x | 3.778699x |
  | `VQRSHRN.U32` | 13.654684x | 3.528960x | 3.560860x | 3.847900x |
  | `VQRSHRN.U64` | 13.132014x | 3.541497x | 3.389130x | 3.712468x |
  | `VQRSHRUN.S16` | 13.846615x | 3.097625x | 3.475383x | 3.904464x |
  | `VQRSHRUN.S32` | 14.809991x | 3.456508x | 3.234291x | 3.695996x |
  | `VQRSHRUN.S64` | 14.697800x | 2.807415x | 3.491155x | 3.776349x |

- Dynarmic now represents the four rounding modes with first-class, exact-width IR operations.
  ARM64 lowers them directly to the matching native instruction and loads FPSR for saturating
  forms. x64 and RISC-V request a polyfill that reconstructs the established overflow-safe DAG,
  preserving non-ARM64 behavior without making the ARM64 backend recognize a fragile multi-node
  pattern.
- Permanent A32 coverage executes all four families and all three source widths. It covers low and
  high registers, partial destination/source overlap at D31/Q15, exact positive and negative
  rounding, unrelated SIMD state, CPSR/FPSCR preservation, saturation, and sticky QC. The first
  release-style ARM64 build passed in 3 minutes 36 seconds; Thor then passed all 1,880 assertions
  in 24 focused `[core][arm][dynarmic]` cases. Source/test commit `596a28aab` was pushed directly to
  `origin/master` over SSH, and the exact post-commit APK build passed in 1 minute 43 seconds.
- The ARM64-only APK is 28,992,796 bytes, reports `596a28aab-vanilla-thor`, and has SHA-256
  `DC5E4F03165E7F7161CD66123468B5E4DFEE85682B476AD6B6846926AD23EF4D`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Both temporary Thor test/benchmark binaries were
  removed immediately after use.
- Cleanup removed 2,469,307,555 logical bytes: the 448,016,304-byte native test ELF, benchmark and
  A32-encoding scratch, eight rendered manual pages, copied validation layers, and reproducible
  Gradle/JNI/R8/native-symbol staging. It retained the 28,992,796-byte APK plus 476-byte metadata
  and the 2,802,108,385-byte active ARM64 CMake/Ninja cache. C: recovered 2,026,508,288 physical
  bytes and reported 81,532,805,120 bytes free afterward.
- This is optimization 102 in the overlapping Thor work tally. The 2.81x-14.81x measurements apply
  only while executing these exact rounding shift-right-narrow forms. They cannot be added to the
  other 101 items or treated as a whole-game FPS, battery-watt, or thermal result; those require a
  matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/duration A/B.

## ARM64 Vector Rounding Shift-Right Fusion (2026-08-18)

- A32/A64 vector `VRSHR`/`SRSHR`/`URSHR` previously became an overflow-safe right shift, rounding-
  bit broadcast, AND, equality mask, and subtract-as-add. `VRSRA`/`SRSRA`/`URSRA` then appended a
  separate modular vector add. ARM64 can express the exact operations as one `SRSHR`/`URSHR` or
  `SRSRA`/`URSRA`, without touching FPSR.
- The Cortex guides made this a per-core measurement question rather than an automatic fusion.
  A510 lists A64 `SRSHR`/`URSHR` at latency 4 and A32 `VRSRA` at latency 7, while A710/A715 list
  basic immediate shifts at latency 2, rounding immediate shifts and shift-accumulates at latency
  4, and X3 lists the same latency classes at higher throughput. `llvm-objdump` verified all 24
  baseline/candidate bodies. Each sample ran four independent vector operations for 1,000,000
  iterations, or 4,000,000 affected operations, across nine alternating-order rounds. Warmup and
  timed checksums matched and remained nonzero.

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VRSHR.S8` | 10.168814x | 2.503628x | 2.717059x | 3.644689x |
  | `VRSHR.S16` | 10.113846x | 2.504299x | 2.708425x | 4.464165x |
  | `VRSHR.S32` | 9.983189x | 2.504528x | 2.714135x | 4.767936x |
  | `VRSHR.S64` | 10.595018x | 2.512617x | 2.713818x | 4.721130x |
  | `VRSHR.U8` | 10.433867x | 2.505041x | 2.710081x | 3.758589x |
  | `VRSHR.U16` | 10.605283x | 2.510857x | 2.714629x | 3.518261x |
  | `VRSHR.U32` | 10.049815x | 2.505534x | 2.718222x | 3.793438x |
  | `VRSHR.U64` | 9.879887x | 2.504598x | 2.713218x | 3.624434x |
  | `VRSRA.S8` | 5.579025x | 2.991158x | 3.440428x | 3.327763x |
  | `VRSRA.S16` | 5.311777x | 3.005532x | 3.491094x | 3.342980x |
  | `VRSRA.S32` | 5.076427x | 3.004477x | 3.486183x | 3.354109x |
  | `VRSRA.S64` | 5.183191x | 3.005112x | 3.496335x | 2.543372x |
  | `VRSRA.U8` | 5.149252x | 3.008707x | 3.471114x | 3.372724x |
  | `VRSRA.U16` | 5.646550x | 3.004865x | 3.481740x | 3.176134x |
  | `VRSRA.U32` | 5.412307x | 2.991859x | 3.503312x | 3.304029x |
  | `VRSRA.U64` | 5.173864x | 3.004616x | 3.386183x | 3.285604x |

- Plain non-rounding `VSRA` was measured in the same harness and rejected. Native `SSRA`/`USRA`
  improved A510 by 3.94x-4.10x and was effectively neutral on A715 (0.996033x-1.000846x) and A710
  (0.996907x-1.003349x), but X3 fell to 0.777230x-0.942683x, a 5.7%-22.3% regression. The frontend
  deliberately retains its existing shift plus add for this family.
- Dynarmic now carries signed/unsigned, 8/16/32/64-bit rounding right shift and rounding right-
  shift-accumulate operations in first-class IR. ARM64 emits the matching native instruction.
  x64 and RISC-V request a polyfill that reconstructs the prior overflow-safe DAG plus optional
  modular add, so non-ARM64 behavior is unchanged.
- Permanent A32 coverage executes all 16 signed/unsigned forms across every lane width. It includes
  D/Q widths, low and high registers, maximum legal shifts, full source/destination overlap, exact
  signed and unsigned rounding, accumulator wraparound, unrelated SIMD state, and unchanged
  CPSR/FPSCR. The release-style ARM64 test binary built successfully and Thor passed all 1,928
  assertions in 25 focused `[core][arm][dynarmic]` cases. Source/test commit `f8dfcb115` was pushed
  directly to `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 2 minutes 42 seconds. Its ARM64-only APK is 28,997,240 bytes, reports
  `f8dfcb115-vanilla-thor`, and has SHA-256
  `8B3649C5E6E5F0CC1AA57CD9E2424D9C672C1800B0BE61EABB074402658F246A`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 52%, 3.754 V, and 25.0 C, so
  this is not battery-discharge watt evidence. Both temporary device binaries were removed.
- Cleanup removed 2,503,006,450 logical bytes: the 448,224,232-byte native test ELF, stripped test
  copy, benchmark/encoding scratch, eight rendered manual pages, copied validation layers, tool
  metadata, and reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,997,240-byte
  APK plus 476-byte metadata and the 2,797,637,092-byte active ARM64 CMake/Ninja cache. C: recovered
  2,059,730,944 physical bytes and reported 81,490,550,784 bytes free afterward.
- This is optimization 103 in the overlapping Thor work tally. The 2.50x-10.61x measurements apply
  only while executing these exact rounding shift-right or rounding shift-right-accumulate forms;
  they cannot be added to the other 102 items or treated as a whole-game FPS, sustained battery-
  watt, frametime, or thermal result. Those still require a matched title/scene/cache/renderer/
  driver/resolution/layout/mode/fan/brightness/duration A/B run.

## ARM64 Vector Shift-Insert Fusion (2026-08-18)

- A32/A64 `VSLI`/`SLI` and `VSRI`/`SRI` previously expanded on ARM64 to five host instructions: a
  vector shift, scalar immediate materialization, `DUP`, `BIC`, and `ORR`. AArch64 expresses the
  exact destination-preserving operation in one native `SLI` or `SRI`, reducing this affected path
  by four instructions, or 80%.
- The complete Arm guide pages were rendered and visually checked before measurement. A510 lists
  A64 `SLI`/`SRI` and A32 `VSLI`/`VSRI` at latency 3 and throughput `2,1` on VALU; A710 lists both
  forms at latency 2 and throughput 1 on V1; A715 lists the A64 forms at latency 2 and throughput 1
  on V1; X3 lists the A64 forms at latency 2 and throughput 2 on V13. These tables supported testing
  every Thor core class rather than assuming the five-to-one instruction reduction scaled equally.
- `llvm-objdump` verified the intended five-instruction baseline and one-instruction candidate for
  every lane width and direction, with identical loop control. Each sample used four independent
  vector chains for 1,000,000 iterations, or 4,000,000 affected operations, over nine alternating-
  order rounds. Warmup and timed checksums matched and remained nonzero.

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `VSLI.8` | 6.939407x | 2.005499x | 2.203525x | 2.420771x |
  | `VSLI.16` | 7.102336x | 2.007047x | 2.210151x | 2.421235x |
  | `VSLI.32` | 7.011041x | 2.006873x | 2.191220x | 2.426934x |
  | `VSLI.64` | 8.322861x | 1.998352x | 2.174261x | 2.421775x |
  | `VSRI.8` | 7.838822x | 1.993065x | 2.204195x | 2.419850x |
  | `VSRI.16` | 7.043788x | 2.006766x | 2.195724x | 2.420463x |
  | `VSRI.32` | 7.107420x | 2.009022x | 2.195096x | 2.422545x |
  | `VSRI.64` | 7.040607x | 2.005004x | 2.202777x | 2.425394x |

- Dynarmic now carries left and right vector shift-insert as first-class IR. ARM64 emits the native
  instruction, while x64 and RISC-V request an exact polyfill, preserving their established output.
  Permanent A32 coverage executes all 16 min/max-immediate operations across 8/16/32/64-bit lanes.
  It covers D/Q forms, low/high registers, source/destination overlap, preserved destination bits,
  unrelated SIMD state, and unchanged CPSR/FPSCR.
- The full native ARM64 build completed successfully in 13 minutes 33 seconds, including the
  production emitter and library. Thor then passed all 1,976 assertions in 26 focused
  `[core][arm][dynarmic]` cases. Source/test commit `ce1500209` was pushed directly to
  `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 3 minutes 6 seconds. Its ARM64-only APK is 28,998,432 bytes, reports
  `ce1500209-vanilla-thor`, and has SHA-256
  `2E66A94B4F804ED795BAA0CF360158C6E4AB8E51BCB276B688079D53A186C444`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the corrected process-ID
  check was empty, and no app UI or game was launched. Thor was USB-powered at 49%, 3.828 V, and
  22.0 C, so this is not battery-discharge watt evidence. Both temporary device binaries were
  removed.
- Cleanup removed 2,498,961,084 logical bytes: the 447,340,912-byte native test ELF, stripped test
  copy, benchmark/encoding scratch, six rendered manual pages, copied JNI dependencies, and
  reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,998,432-byte APK plus
  476-byte metadata and the 2,784,158,328-byte active ARM64 CMake/Ninja cache. C: recovered about
  2,057,101,312 physical bytes and reported 81,502,978,048 bytes free immediately afterward. No
  PDF or rendered manual artifact was committed.
- This is optimization 104 in the overlapping Thor work tally. The 1.99x-8.32x measurements apply
  only while executing these exact vector shift-insert forms. They cannot be added to the other 103
  items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result. Those
  still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B run.

## ARM64 Packed Unsigned Byte Difference Sum (2026-08-18)

- A32 ARMv6 `USAD8` and `USADA8` are part of the actual 3DS ARM11 guest instruction set, unlike
  guest AdvSIMD-only experiments. Dynarmic's ARM64 `PackedAbsDiffSumU8` lowering previously emitted
  `MOVI` for a four-byte lane mask, `UABD`, `AND`, and `UADDLV`. It now emits `UABDL H8` followed by
  `UADDLV H4`, reducing the affected guest operation from four host instructions to two, or 50%.
- The semantic shortcut is exact: `UABDL` widens all eight unsigned byte differences, placing the
  four defined guest lanes in the low four halfwords. Reducing only `H4` ignores the packed
  operand's undefined upper word without a mask. The largest sum is 4 * 255 = 1020, and `USADA8`
  retains its normal 32-bit modular accumulator addition in the surrounding IR.
- The complete Arm guide pages were rendered and visually checked before measurement. A510 lists
  `UABDL` at latency 3 and throughput `2,1` on VALU and `UADDLV 4H` at latency 4 and throughput 1
  on VALU. A710 lists latency/throughput 2/2 on V for `UABDL` and 2/1 on V1 for `UADDLV 4H`.
  A715 lists 2/2 on V and 3/1 on V1 respectively. X3 lists 2/4 on V and 2/2 on V13 respectively.
  These differences supported measuring every Thor core class instead of extrapolating from X3.
- `llvm-objdump` verified the intended four-instruction baseline and two-instruction candidate with
  identical loop control. The harness used four independent packed operations for 1,000,000
  iterations, or 4,000,000 affected operations, over nine alternating-order rounds per core.
  Warmup and timed checksums matched and remained nonzero at 1432.

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `USAD8` four-byte difference sum | 1.759435x | 2.515585x | 2.505252x | 2.806593x |

- Permanent A32 tests execute ARM and Thumb `USAD8`/`USADA8`, including normal and source/
  accumulator-alias encodings, maximum byte differences, an accumulator at `UINT32_MAX`, patterned
  edge values, unrelated-register preservation, and unchanged NZCV/Q/GE flags. The complete native
  ARM64 build passed in 11 minutes 42 seconds, and Thor passed all 2,176 assertions in 27 focused
  `[core][arm][dynarmic]` cases. Source/test commit `928eae934` was pushed directly to
  `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 2 minutes 48 seconds. Its ARM64-only APK is 28,997,788 bytes, reports
  `928eae934-vanilla-thor`, and has SHA-256
  `DCF8B4F89B683FD45F70A986E6773B122C7F048284880ED2878E993BCC6F3B57`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 49%, 3.849 V, and 21.0 C, so
  this is not battery-discharge watt evidence. Both temporary device binaries were removed.
- Cleanup removed 2,496,891,355 logical bytes: the 448,367,400-byte native test ELF, stripped test
  copy, benchmark/encoding scratch, five rendered manual pages, copied JNI dependencies, and
  reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,997,788-byte APK plus
  476-byte metadata and the 2,787,840,399-byte active ARM64 CMake/Ninja cache. C: recovered
  2,054,905,856 physical bytes and reported 81,254,731,776 bytes free immediately afterward. No
  PDF or rendered manual artifact was committed.
- This is optimization 105 in the overlapping Thor work tally. The 1.76x-2.81x measurements apply
  only while executing these exact packed byte-difference forms. They cannot be added to the other
  104 items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result.
  Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness/duration A/B run.

## ARM64 A32 Halfword Packing (2026-08-18)

- A32 ARMv6 `PKHBT` and `PKHTB` are part of the 3DS ARM11 guest instruction set. The ARM and Thumb
  frontends previously expanded each operation into an immediate shift, two masks, and an OR.
  Dynarmic now retains `PackHalfwordBottom` and `PackHalfwordTop` as first-class IR operations.
  ARM64 emits one `BFXIL` for bottom shift 0, one `BFI` for bottom shift 16, and `LSL` plus `BFXIL`
  for other bottom shifts. Top shifts 1-16 use one `BFXIL`; shifts 17-32 use `ASR` plus `BFXIL`,
  with ASR #32 represented exactly by ASR #31. This changes the measured forms from three or four
  host instructions to one or two. x64 and RISC-V reconstruct the established exact DAG through a
  polyfill.
- The semantic shortcut is exact. `BFXIL` replaces only the low destination halfword, while `BFI`
  at bit 16 replaces only the high destination halfword. For a top shift no greater than 16, the
  low 16 bits of arithmetic shift right are the source field beginning at that shift, so sign
  extension is irrelevant. Larger shifts first materialize the sign-extended low field; shift 32
  is all copies of bit 31. Dynarmic's read-write allocator copies a source when it is still live,
  so reusing it as the result does not alter shared IR values.
- The complete instruction-characteristics pages in the Cortex-A510, A710, A715, and X3 software
  optimization guides were rendered and visually checked before measurement. A510 lists bitfield
  moves at latency 2 and throughput 3, with immediate LSL/ASR aliases at latency 1 and throughput
  3. A710 lists bitfield moves at 2/2 on M and immediate shifts at 1/4 on I. A715 lists both at
  latency 1 and throughput 4 on I. X3 lists bitfield moves at 2/2 on M and immediate shifts at 1/6
  on I. These different pipelines supported measuring every Thor core class instead of
  extrapolating from the prime core.
- `llvm-objdump` verified identical loop control around the intended candidates: `PKHBT` shift 0
  changed 3 -> 1 instructions, shift 7 changed 4 -> 2, and shift 16 changed 4 -> 1;
  `PKHTB` shifts 7 and 16 changed 4 -> 1, while shifts 24 and 32 changed 4 -> 2. The harness used
  four independent operations for 1,000,000 iterations, or 4,000,000 affected operations, over
  nine alternating-order rounds per core. Every warmup and timed checksum matched and remained
  nonzero.

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `PKHBT`, LSL #0 | 2.545486x | 1.884437x | 1.803578x | 1.489624x |
  | `PKHBT`, LSL #7 | 1.209718x | 1.750912x | 1.344066x | 1.499972x |
  | `PKHBT`, LSL #16 | 3.014697x | 2.460514x | 2.162188x | 2.232581x |
  | `PKHTB`, ASR #7 | 2.969528x | 2.443599x | 2.155619x | 2.005063x |
  | `PKHTB`, ASR #16 | 2.825604x | 2.438770x | 2.163796x | 2.003154x |
  | `PKHTB`, ASR #24 | 1.221914x | 1.861854x | 1.912342x | 1.756898x |
  | `PKHTB`, ASR #32 | 1.196462x | 1.861727x | 1.905815x | 1.776214x |

- Permanent tests execute both ARM and Thumb encodings at bottom shifts 0, 1, 7, 15, 16, 17,
  and 31 and top shifts 1, 7, 15, 16, 17, 24, 31, and 32. They cover distinct registers,
  destination equal to either source, all three registers equal, positive and negative sources,
  unrelated-register preservation, and unchanged NZCV/Q/GE flags. The complete native ARM64 build
  passed with JDK 17 in 2 minutes 2 seconds, and Thor passed all 3,646 assertions in 28 focused
  `[core][arm][dynarmic]` cases. Source/test commit `f016be8b3` was pushed directly to
  `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 3 minutes 17 seconds. Its ARM64-only APK is 28,997,592 bytes, reports
  `f016be8b3-vanilla-thor`, and has SHA-256
  `0DF0F881D9F2E06F45FC487A65430FFFA64471878B303ACB8D4F74EBE97D2252`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 50%, 3.861 V, and 21.0 C, so
  this is not battery-discharge watt evidence. Both temporary device binaries were removed.
- Cleanup removed 2,502,513,652 logical bytes: the 448,422,216-byte native test ELF, stripped test
  copy, benchmark/encoding scratch, four rendered manual pages, copied JNI dependencies, and
  reproducible Gradle/JNI/R8/native-symbol staging. It retained the 28,997,592-byte APK plus
  476-byte metadata and the 2,789,489,413-byte active ARM64 CMake/Ninja cache. C: recovered
  2,059,714,560 physical bytes and reported 81,224,990,720 bytes free immediately afterward. No
  PDF or rendered manual artifact was committed.
- This is optimization 106 in the overlapping Thor work tally. The 1.20x-3.01x measurements apply
  only while executing these exact halfword-pack forms. They cannot be added to the other 105
  items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result. Those
  still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B run.

## ARM64 A32 Scalar Extend-And-Add (2026-08-18)

- A32 ARM/Thumb-2 `SXTAB`, `SXTAH`, `UXTAB`, and `UXTAH` are part of the 3DS ARM11 guest ISA. With
  rotation zero, Dynarmic previously built a narrow, sign/zero extension, and generic add. It now
  retains `SignedExtendAndAdd32` or `UnsignedExtendAndAdd32` as first-class IR, and ARM64 emits one
  extended-register `ADD` using `SXTB`, `SXTH`, `UXTB`, or `UXTH`. x64 and RISC-V polyfill the new
  IR back into the established exact DAG. Nonzero guest rotations deliberately keep their previous
  IR path.
- The complete arithmetic/extend/shift pages in the Cortex-A510, A710, A715, and X3 software
  optimization guides were rendered and visually checked before measurement. A510 lists extended
  `ADD`/`SUB` at latency 1 and throughput 3, with latency 2 when the dependency is on `Rm`. A710,
  A715, and X3 list latency 2 and throughput 2 on the M pipeline. This supported measuring each
  accessible Thor core class instead of extrapolating from one core.
- `llvm-objdump` verified the intended sequence change for every width and signedness: rotation-zero
  forms changed from `SXTB`/`SXTH`/`UXTB`/`UXTH` plus `ADD` to one extended-register `ADD`.
  Nonzero controls changed from `ROR` plus extension plus add to `ROR` plus extended add. The
  standalone harness used four independent chains for 4,000,000 iterations, or 16,000,000 affected
  operations per sample, over nine alternating-order rounds. Every warmup and timed checksum
  matched and remained nonzero.

  | Guest-equivalent form | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `SXTAB`, ROR #0 | 1.330541x | 1.165637x | 1.129608x | not measurable |
  | `SXTAH`, ROR #0 | 1.332116x | 1.171669x | 1.135539x | not measurable |
  | `UXTAB`, ROR #0 | 1.329924x | 1.171331x | 1.126554x | not measurable |
  | `UXTAH`, ROR #0 | 1.340523x | 1.204479x | 1.126133x | not measurable |

- A510 rotation-zero medians changed from 0.504124 to 0.378887 ns/op for `SXTAB`, 0.502028 to
  0.376865 for `SXTAH`, 0.503691 to 0.378737 for `UXTAB`, and 0.504987 to 0.376709 for `UXTAH`.
  The nonzero controls measured 1.000410x, 1.000475x, 0.994994x, and 0.998904x respectively on
  A510. Although those controls improved by 1.017667x-1.052110x on A715 and
  1.469712x-1.474514x on A710, the A510 results were neutral and `UXTAB` ROR #24 repeated a 0.50%
  regression. The optimization is therefore limited to rotation zero.
- X3 is intentionally not reported as a physical result. Android exposed CPU 7 as online, but
  `/sys/devices/system/cpu/cpu7/core_ctl/active_cpus` remained zero, single-bit CPU 6/7 affinity
  masks returned `EINVAL`, and a short seven-load helper did not unpark it. The X3 guide informed
  the implementation review only; it cannot substitute for a benchmark.
- Permanent coverage executes 40 ARM/Thumb encodings over six input patterns. It covers all four
  operations, rotation-zero optimized forms, nonzero fallback controls, destination/addend/value
  aliases, high registers, signed and unsigned edge values, every unrelated GPR, and unchanged
  NZCV/Q/GE flags. The final native ARM64 build passed with JDK 17 in 1 minute 32 seconds, and Thor
  passed all 7,486 assertions in 29 focused `[core][arm][dynarmic]` cases. Source/test commit
  `1741a60c2` was pushed directly to `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 3 minutes 4 seconds. Its ARM64-only APK is 28,999,604 bytes, reports
  `1741a60c2-vanilla-thor`, and has SHA-256
  `21F28D5C29BB3E26A5FD7B0FA4EE2CAA000272668080D1BC7EF39E6994C3DC56`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 55%, 3.880 V, and 21.0 C, so
  this is not battery-discharge watt evidence. All temporary device helpers were removed.
- Cleanup removed 2,469,674,954 logical bytes: the 448,479,264-byte native test ELF plus
  reproducible Gradle/JNI/R8/native-symbol/mapping staging. It retained the 28,999,604-byte APK,
  476-byte metadata, and 2,797,132,682-byte active ARM64 CMake/Ninja cache. C: recovered
  2,058,350,592 physical bytes and reported 81,383,247,872 bytes free immediately afterward.
  Benchmark source/binaries, encoding scratch, stripped tests, helper scripts, and rendered manual
  pages had already been removed; no PDF or rendered manual artifact was committed.
- This is optimization 107 in the overlapping Thor work tally. The 1.13x-1.34x measurements apply
  only while executing these exact rotation-zero extend-and-add forms. They cannot be added to the
  other 106 items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal
  result. Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/
  fan/brightness/duration A/B run.

## Rejected ARM64 A32 MLA/MLS MADD/MSUB Fusion (2026-08-18)

- A tempting A32 Dynarmic change was to replace the current split `MUL` plus `ADD`/`SUB` lowering
  for `MLA`/`MLS` with native AArch64 `MADD`/`MSUB`. The complete integer multiply tables in the
  Cortex-A510, A710, A715, and X3 software optimization guides were reviewed first. A510 documents
  W-form multiply-add/subtract latency 3, throughput 1, and typical accumulator forwarding every
  two cycles; the three larger cores document latency 2 with accumulator forwarding 1 and
  throughput 1. The tables made dependency structure a required measurement dimension rather than
  a reason to assume the fused instruction was universally better.
- `llvm-objdump` verified exact split and fused sequences. A standalone harness measured both four
  independent chains and a sequential accumulator chain for 16,000,000 affected operations per
  sample over nine alternating-order rounds. Every checksum matched and remained nonzero. Ratios
  below are fused divided by the retained split path; values below 1.0 are regressions.

  | Form and dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `MLA`, independent | 1.241811x | 0.690163x | 0.625852x | 0.554212x |
  | `MLA`, dependent | 0.613736x | 0.739660x | 0.997213x | 1.001076x |
  | `MLS`, independent | 1.235381x | 0.668825x | 0.624810x | 0.554066x |
  | `MLS`, dependent | 0.595157x | 0.726579x | 1.001818x | 1.000144x |

- The fused lowering was rejected and the split `MUL` plus `ADD`/`SUB` path remains unchanged.
  This experiment does not increment the optimization tally. Its source, binaries, encodings, and
  device helper were removed after measurement; no manual PDF or rendered page was copied into the
  repository.

## ARM64 A32 Packed Halfword Saturation (2026-08-18)

- A32 ARM/Thumb-2 `SSAT16` and `USAT16` are ARM11 guest instructions. Dynarmic previously extracted
  and sign-extended both halfwords, invoked the scalar saturation operation twice, repacked them,
  derived two overflow results, and updated sticky `CPSR.Q` twice. The frontend now emits
  `PackedSignedSaturation16` or `PackedUnsignedSaturation16` plus one overflow pseudo-result and one
  `A32OrQFlag` call.
- ARM64 sign-extracts both lanes, shares the min/max constants, clamps with scalar `CMP`/`CSEL`,
  packs with `BFI`, and compares the packed result against the input once. Signed saturation to 16
  bits aliases the input and reports no overflow; unsigned saturation to zero bits returns zero and
  performs the required comparison. AdvSIMD `SQSHL`/`SQSHLU` were deliberately not used because
  their host `FPSR.QC` side effect could incorrectly alter guest VFP `FPSCR.QC`; this guest
  instruction updates only ARM11 `CPSR.Q`. x64 and RISC-V polyfill the new IR back into the exact
  established two-lane scalar DAG.
- `llvm-objdump` verified the intended old and new sequences. The standalone harness measured
  representative signed and unsigned 8-bit saturation, with the sticky-Q load/OR/store included.
  It used four independent operations and four sequential dependent operations per loop,
  4,000,000 affected operations per sample, and nine alternating-order rounds. Every checksum
  matched and remained nonzero.

  | Guest-equivalent path | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `SSAT16`, independent | 1.314113x | 1.999041x | 2.024448x | 2.031681x |
  | `SSAT16`, dependent | 1.207003x | 1.999299x | 1.966196x | 1.505940x |
  | `USAT16`, independent | 1.092654x | 2.000018x | 2.007952x | 2.031970x |
  | `USAT16`, dependent | 1.124863x | 2.000474x | 1.928901x | 1.495144x |

- Permanent coverage generates all 128 operation/immediate/encoding/alias combinations: ARM and
  Thumb, signed immediates 1-16, unsigned immediates 0-15, and aliased or distinct source and
  destination registers. Ten mixed-lane inputs and both initial Q states verify exact output,
  every unrelated GPR, unchanged NZCV/GE and FPSCR, and sticky CPSR.Q. The final native ARM64 build
  passed with JDK 17 in 1 minute 56 seconds. Thor passed the full 51,007-assertion, 30-case focused
  suite; the new test passed all 43,521 assertions when pinned separately to CPU 0/A510,
  CPU 3/A715, CPU 5/A710, and CPU 7/X3. Source/test commit `aac808826` was pushed directly to
  `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` cold
  build passed with JDK 17 in 13 minutes 9 seconds. Its ARM64-only APK is 28,999,820 bytes, reports
  `aac808826-vanilla-thor`, and has SHA-256
  `8BE9CA081B05BA8589AF2EE5C080563D01EB19B1C4C9CDE2C014C7A4C7439A41`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 55%, 3.907 V, and 22.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,493,164,811 logical bytes: the 447,536,016-byte native test ELF, benchmark and
  encoding scratch, rendered manual pages, and reproducible Gradle/JNI/R8/native-symbol/mapping
  staging. It retained the 28,999,820-byte APK, 476-byte metadata, and 2,784,972,401-byte active
  ARM64 CMake/Ninja cache. C: recovered 2,051,076,096 physical bytes and reported 80,769,478,656
  bytes free immediately afterward. Both temporary device helpers were removed; no PDF, manual
  page, benchmark binary, or scratch note was committed.
- This is optimization 108 in the overlapping Thor work tally. The 1.09x-2.03x measurements apply
  only while executing these exact packed-saturation forms. They cannot be added to the other 107
  items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result. Those
  still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B run.

## ARM64 A32 Packed Byte Sign Extension (2026-08-18)

- A32 ARM/Thumb-2 `SXTB16` is part of the 3DS ARM11 guest ISA. Dynarmic previously rotated the
  source when requested, selected bytes 0 and 2 with `0x00FF00FF`, selected their sign bits with
  `0x00800080`, multiplied the sign bits by `0x1FE`, and ORed the pieces together. On ARM64 that
  exact IR became `AND`, `AND`, constant materialization, `MUL`, and `ORR` after any rotate.
- The frontend now emits first-class `PackedSignExtendByteToHalf` IR for rotations 0, 8, 16, and
  24. ARM64 uses `SBFX` to extract and sign-extend the selected upper byte into a scratch register,
  `SXTB` for the selected low byte, and `BFI` to insert the upper halfword. Disassembly review caught
  the required alias order before implementation: `SBFX` must read the upper byte before `SXTB`
  writes a final-use register that may alias the source. x64 and RISC-V polyfill the operation back
  into the established portable mask/multiply DAG.
- Local Cortex-A510, A710, A715, and X3 optimization-guide tables were reviewed before selecting
  the sequence. They document the relevant `SBFM`/`SBFX`, `SXTB`, and `BFM`/`BFI` latency and
  throughput characteristics, but the mixed results predicted by those tables were treated only as
  a reason to benchmark every Thor core class. No manual PDF or rendered page entered the repo.
- `llvm-objdump` verified the exact five-instruction old body and three-instruction new body. A
  standalone ARM64 harness measured rotation-zero and rotation-eight forms with four independent
  chains and a sequential dependent chain. Each sample executed 16,000,000 affected operations
  (32,000,000 in the final A510 rerun), nine rounds alternated old/new order, and every checksum
  matched and remained nonzero. Lower nanoseconds per operation are better; ratios are old/new.

  | Guest-equivalent path | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | ROR 0, independent | 1.332951x | 1.856836x | 1.291697x | 1.615659x |
  | ROR 0, dependent | 1.248735x | 2.042547x | 1.331349x | 1.336929x |
  | ROR 8, independent | 1.000965x | 1.541483x | 1.235306x | 1.301351x |
  | ROR 8, dependent | 1.198771x | 1.638419x | 1.249064x | 1.240429x |

- Permanent coverage generates all 16 encoding/rotation/alias combinations: ARM and Thumb,
  rotations 0/8/16/24, and aliased or distinct source and destination. Ten mixed inputs cover both
  sign edges and ignored-byte garbage while verifying exact output, every unrelated GPR, unchanged
  NZCV/Q/GE, and unchanged FPSCR. The new test passed all 2,721 assertions when pinned separately
  to CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3. The final focused suite passed 53,728
  assertions in 31 cases. The final native verification build passed with JDK 17 in 1 minute 5
  seconds. Source/test commit `e58d8e1c3` was pushed directly to `origin/master` over command-line
  Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 2 minutes 23 seconds. Its ARM64-only APK is 29,001,332 bytes, reports
  `e58d8e1c3-vanilla-thor`, and has SHA-256
  `E89B15D0B7AFE42D2B44FE9F44B9904BD9CDBA68C68550E26036BA87BBD0AF11`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 57%, 3.877 V, and 22.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,493,001,646 logical host bytes: the 447,569,976-byte native test ELF,
  standalone benchmark and stripped test copy, rendered manual pages, and reproducible Gradle/JNI/
  R8/native-symbol/mapping staging. It retained the 29,001,332-byte APK, 476-byte metadata, and
  2,785,534,773-byte active ARM64 CMake/Ninja cache. C: recovered 2,053,423,104 physical bytes and
  reported 80,681,345,024 bytes free immediately afterward. The 86,360-byte benchmark and
  26,090,904-byte stripped test helper were also removed from the Thor; no PDF, manual page,
  benchmark binary, or scratch note was committed.
- This is optimization 109 in the overlapping Thor work tally. The 1.00x-2.04x measurements apply
  only while executing these exact packed sign-extension forms. They cannot be added to the other
  108 items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result.
  Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness/duration A/B run.

## ARM64 A32 Rotated Packed Sign-Extend-and-Add (2026-08-18)

- A32 ARM/Thumb-2 `SXTAB16` sign-extends bytes 0 and 2 of an optionally rotated source, adds them
  independently to the two destination halfwords, and wraps each lane modulo 16 bits. Dynarmic's
  old frontend rebuilt the same two-mask, sign-mask-times-`0x1FE`, and OR DAG used by the former
  `SXTB16` lowering, then crossed into SIMD for `PackedAddU16`. With a nonzero guest rotation the
  complete ARM64 body was ten host instructions: one `ROR`, five sign-extension instructions, two
  GPR-to-SIMD `FMOV`s, one halfword `ADD`, and one SIMD-to-GPR `FMOV`.
- Three disassembly-checked bodies were measured. The accepted composition routes rotations
  8/16/24 through `PackedSignExtendByteToHalf`, replacing the five-instruction sign-extension body
  with `SBFX`, `SXTB`, and `BFI`; the required `ROR` plus shared packed-add transfers remain, so the
  full nonzero path falls from ten instructions to eight. The unmodified x64 and RISC-V polyfill
  expands the first-class operation back into the same portable DAG.
- A more aggressive five-instruction rotation-zero AdvSIMD body used GPR-to-SIMD `FMOV`, byte
  `UZP1`, another `FMOV`, signed widening `SADDW`, and a final `FMOV` (six instructions with a
  rotation). It was rejected: the doubled X3 run measured 0.894154x for independent rotation zero
  and 0.978046x for independent ROR8, regressions of 10.6% and 2.2%. Applying the scalar
  composition to rotation zero was also rejected after its doubled X3 independent result repeated
  at 0.979993x, a 2.0% regression. Rotation zero therefore retains the old lowering.
- The Cortex-A510/A710 AArch32 tables document native `SXTAB16` latency/throughput differences,
  while all four AArch64 tables document materially different `SBFM`/`BFM` costs. That manual
  evidence correctly warned against accepting instruction count alone. No manual PDF or rendered
  page was copied into the repository.
- `llvm-objdump` verified the exact old, composed, and rejected fused bodies. The standalone
  benchmark used four independent source-alias chains and one source-alias chain repeated four
  times sequentially per loop. Each sample executed 16,000,000 affected operations; the final X3
  confirmation used 32,000,000. Nine rounds rotated the order of all three candidates, and every
  checksum matched and remained nonzero. The accepted ROR8 old/composed median ratios were:

  | ROR8 dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | Four independent chains | 1.065097x | 1.159406x | 1.120201x | 1.067154x |
  | Sequential chain | 1.050745x | 1.159727x | 1.089628x | 1.076240x |

- Permanent coverage generates all 40 encoding/rotation/alias combinations: ARM and Thumb,
  rotations 0/8/16/24, all-distinct operands, destination/addend alias, destination/source alias,
  addend/source alias, and all operands aliased. Ten addend/source pairs cover positive and negative
  byte edges, ignored-byte garbage, carry and borrow wrap, and mixed lanes while verifying exact
  output, every unrelated GPR, unchanged NZCV/Q/GE, and unchanged FPSCR. The new test passed all
  6,801 assertions when pinned separately to CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3.
  The final focused suite passed 60,529 assertions in 32 cases. The retained ARM64 Ninja graph built
  both the native tests and `libcitra-android.so`; source/test commit `624534787` was pushed directly
  to `origin/master` over command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` build
  passed with JDK 17 in 3 minutes 23 seconds after the pinned Khronos validation-layer ZIP was
  restored to its expected build-temp path. Its ARM64-only APK is 29,002,140 bytes, reports
  `624534787-vanilla-thor`, and has SHA-256
  `B440EE1C11C3883D7558953442DDAB3371CE0F6AF35AC44888BEFD09DBEA3494`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 57%, 3.911 V, and 23.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,493,587,500 logical host bytes: the 447,603,224-byte native test ELF,
  standalone benchmark/source and stripped test copy, rendered manual pages, pinned validation
  ZIP, and reproducible Gradle/JNI/R8/native-symbol/mapping staging. It retained the 29,002,140-byte
  APK, 476-byte metadata, and 2,785,715,434-byte active ARM64 CMake/Ninja cache. C: recovered
  2,053,017,600 physical bytes and reported 79,950,155,776 bytes free immediately afterward. The
  629,032-byte benchmark and 26,095,448-byte stripped test helper were also removed from the Thor;
  no PDF, manual page, benchmark binary, or scratch note was committed.
- This is optimization 110 in the overlapping Thor work tally. The 1.05x-1.16x measurements apply
  only while executing these exact nonzero-rotation packed sign-extend-and-add forms. They cannot
  be added to the other 109 items or treated as a whole-game FPS, sustained battery-watt,
  frametime, or thermal result. Those still require a matched title/scene/cache/renderer/driver/
  resolution/layout/mode/fan/brightness/duration A/B run.

## ARM64 A32 Native Bit Reversal (2026-08-18)

- A32 ARM/Thumb-2 `RBIT` previously expanded in the frontend to a portable mask/shift/OR network:
  two AND-and-shift pairs plus OR, followed by four AND-and-shift pairs plus three ORs. The ARM64
  backend consequently emitted 17 host instructions for a guest operation with a native scalar
  instruction.
- Dynarmic now retains the operation as first-class `ReverseBits32` IR. ARM64 emits one native
  `RBIT`; the unmodified x64 and RISC-V semantics are preserved by polyfilling the IR operation
  back to the exact old network. The A32 A64-guest frontend was intentionally left unchanged.
- The locally reviewed Cortex manuals list AArch64 `RBIT` at latency/throughput 2/3 on A510 page
  22, 1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on X3 page 18. This made the semantic IR
  route a strong candidate, but the Thor benchmark remained the acceptance test. No manual PDF or
  rendered page was copied into the repository.
- Standalone disassembly verified the exact old 17-instruction loop and new single-`RBIT` loop.
  The benchmark ran four independent operations or one sequential dependent chain repeated four
  times per loop. Each sample executed 16,000,000 affected operations; 11 samples alternated
  old/new order, and medians are reported below.

  | Dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | Four independent chains | 17.584485x | 12.871236x | 11.306165x | 15.101148x |
  | Sequential chain | 7.024895x | 9.056788x | 9.644375x | 9.113942x |

- Permanent ARM and Thumb-2 coverage checks nine values, distinct operands, source/destination
  aliases, every unrelated GPR, NZCV/Q/GE, and FPSCR. The new test passed 612 assertions when
  pinned separately to CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3. The full focused
  `[core][arm][dynarmic]` suite passed 61,141 assertions in 33 cases. Source/test commit
  `ff4994c54` was pushed directly to `origin/master` using command-line Git SSH.
- The exact post-source-commit `:app:assembleVanillaRelWithDebInfoLite` build with
  `--no-configuration-cache` passed with JDK 17. The retained ARM64-only APK is 28,998,524
  bytes, reports `ff4994c54-vanilla-thor`, and has SHA-256
  `D3D965EC21CC0D3FF5E94B4D1B8842AF00E2102FC84036E8F59FA38F5CBF3CAF`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 57%, 3.847 V, and 23.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,492,895,929 logical host bytes of temporary benchmark/test and reproducible
  Gradle/JNI/R8/native-symbol/mapping staging while retaining the APK, its 476-byte metadata, and
  the active ARM64 CMake/Ninja cache. C: recovered 2,051,411,968 physical bytes and reported
  79,644,041,216 bytes free immediately afterward. Temporary device helpers were also removed; no
  PDF, manual page, benchmark binary, or scratch note was committed.
- This is optimization 111 in the overlapping Thor work tally. The 7.02x-17.58x measurements apply
  only while executing this exact bit-reversal path. They cannot be added to the other 110 items
  or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result. Those
  still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/brightness/
  duration A/B run.

## ARM64 A32 Native Halfword Byte Reversal (2026-08-18)

- A32 ARM and Thumb-2 `REV16` previously expanded into five recurring ARM64 instructions: `LSR`,
  mask, `LSL`, mask, and `ORR`. Thumb-16 used an even longer frontend graph that separately
  extracted, byte-reversed, zero-extended, shifted, and recombined both halfwords.
- Dynarmic now retains all three guest encodings as first-class `ByteReverseHalfwords32` IR. ARM64
  emits one native `REV16`; x64 and RISC-V preserve exact behavior by polyfilling the operation
  back to the established shift/mask/OR network.
- Visual inspection of the local Cortex manuals found AArch64 `REV16` latency/throughput 1/3 on
  A510 page 22, 1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on X3 page 18. The pages were
  rendered only for review and immediately deleted; no manual PDF or rendered page was copied into
  the repository.
- `llvm-objdump` verified the exact five-instruction old loop and single-`REV16` new loop. The
  benchmark ran four independent operations or one sequential dependency chain repeated four
  times per loop. Each sample executed 16,000,000 affected operations; 11 samples alternated
  old/new order, and every final checksum matched at nonzero `0x000643d4`.

  | Dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | Four independent chains | 4.184574x | 3.789630x | 3.605787x | 4.435562x |
  | Sequential chain | 5.122643x | 3.096017x | 3.293049x | 3.268359x |

- Permanent coverage checks ARM, Thumb-16, and Thumb-2 encodings, nine values, distinct operands,
  source/destination aliases, every unrelated GPR, NZCV/Q/GE, and FPSCR. The new test passed all
  918 assertions when pinned separately to CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3. The
  full focused `[core][arm][dynarmic]` suite passed 62,059 assertions in 34 cases. Source/test
  commit `a3c723d8b` was pushed directly to `origin/master` using command-line Git SSH.
- The exact post-commit `:app:assembleVanillaRelWithDebInfoLite` build with
  `--no-configuration-cache` passed with JDK 17 in 2 minutes 51 seconds. The retained ARM64-only APK
  is 28,998,496 bytes, reports `a3c723d8b-vanilla-thor`, and has SHA-256
  `73BEFC0F27839AE7E411A5B840A6E587013C4406C403E8DB4BBF6A6BDF972EF4`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 42%, 3.685 V, and 27.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,492,142,451 logical host bytes of temporary benchmark/test and reproducible
  Gradle/JNI/R8/native-symbol/mapping staging while retaining the 28,998,496-byte APK, its 476-byte
  metadata, and the 2,793,288,818-byte active ARM64 CMake/Ninja cache. C: recovered 2,052,165,632
  physical bytes and reported 79,188,664,320 bytes free immediately afterward. The 7,816-byte
  benchmark and 26,103,304-byte stripped test helper were also removed from the Thor; no PDF,
  manual page, benchmark binary, or scratch note was committed.
- This is optimization 112 in the overlapping Thor work tally. The 3.10x-5.12x measurements apply
  only while executing this exact halfword-byte-reversal path. They cannot be added to the other
  111 items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result.
  Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness/duration A/B run.

## ARM64 A32 Native Signed Halfword Byte Reversal (2026-08-18)

- A32 ARM, Thumb-16, and Thumb-2 `REVSH` previously emitted three recurring ARM64 instructions:
  `UXTH`, `REV16`, and `SXTH`. The high half of the input must remain irrelevant, and bit 15 of the
  byte-reversed low half must still sign-extend through the destination word.
- Dynarmic now retains all three guest encodings as first-class `ByteReverseSignedHalf32` IR.
  ARM64 emits `REV; ASR #16`; x64 and RISC-V preserve exact behavior by polyfilling the operation
  back to `LeastSignificantHalf`, `ByteReverseHalf`, and `SignExtendHalfToWord`.
- Visual inspection of the local Cortex manuals found AArch64 `REV`/`REV16` latency/throughput 1/3
  on A510 page 22, 1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on X3 page 18. The `SBFM`
  group containing `SXTH` is 2/3 on A510 and 1/4 on the other three cores; the A510 guide notes
  immediate `ASR` as a latency-1 alias. That evidence required benchmarking both two-instruction
  candidates rather than choosing by instruction count. Rendered review pages were immediately
  deleted, and no manual PDF or page was copied into the repository.
- `llvm-objdump` verified the exact three-instruction old sequence and both two-instruction
  candidates. The benchmark ran four independent operations or one sequential dependency chain
  repeated four times per loop. Each sample executed 16,000,000 affected operations; 15 samples
  rotated old/`REV; ASR`/`REV16; SXTH` order. Checksums matched at nonzero `0xfffff63b` for the
  independent pattern and `0xffffd3a7` for the dependency pattern.

  | Dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | Four independent chains | 2.621212x | 1.570994x | 1.550576x | 1.605491x |
  | Sequential chain | 2.631136x | 1.499708x | 1.499609x | 1.499462x |

- `REV; ASR #16` won acceptance. `REV16; SXTH` was close on the larger cores and about 1.1% faster
  for A510 independent work, but it took 1.644622375 ns/op on the A510 dependency chain versus
  1.079433625 ns/op for `REV; ASR`. A temporary actual-JIT code dump produced raw words
  `5ac00a74 13107e94`, decoded as `rev w20, w19; asr w20, w20, #16`. The diagnostic hook and
  helper were removed; the final clean stripped test executable was byte-identical to the fully
  tested clean binary (SHA-256 `39CDFB99607D88BEF5E72DEA4600CA5770BFBB12354CDCB92CF85C438DF9FC38`).
- Permanent coverage checks ARM, Thumb-16, and Thumb-2 encodings, nine dirty-upper/sign-boundary
  values, distinct operands, source/destination aliases, every unrelated GPR, NZCV/Q/GE, and
  FPSCR. The new test passed all 918 assertions when pinned separately to CPU 0/A510, CPU 3/A715,
  CPU 5/A710, and CPU 7/X3. The full focused `[core][arm][dynarmic]` suite passed 62,977 assertions
  in 35 cases on CPU 3/A715. The clean JDK 17 ARM64 native build passed 2,200 Ninja steps in 14
  minutes 42 seconds. Source/test commit `cd95d873f` was pushed directly to `origin/master` using
  command-line Git SSH.
- Exact source-commit packaging with `:app:assembleVanillaRelWithDebInfoLite`, ordinary Gradle build
  caching, and `--no-configuration-cache` passed in 3 minutes 49 seconds. The retained APK is
  28,999,048 bytes, reports `cd95d873f-vanilla-thor`, and has SHA-256
  `EC2E530DC6E1AFEA2E5349C1934588E59AD02E3891182C1CD820331E62C1D7A3`. Wi-Fi ADB installed it over
  `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was empty,
  and no app UI or game was launched. Thor was USB-powered at 33%, 3.734 V, and 25.0 C, so this is
  not battery-discharge watt evidence.
- Cleanup removed the 448,682,880-byte native test ELF and reproducible Gradle/JNI/R8/native-symbol/
  mapping staging while retaining the APK, its 476-byte metadata, and the 2,788,591,339-byte active
  ARM64 CMake/Ninja cache. C: recovered 2,029,408,256 physical bytes and reported 78,889,033,728
  bytes free immediately afterward. Four temporary device helpers totaling 78,331,256 bytes were
  removed from `/data/local/tmp`; no PDF, manual page, benchmark binary, or scratch note was
  committed.
- This is optimization 113 in the overlapping Thor work tally. The 1.50x-2.63x measurements apply
  only while executing this exact signed-halfword byte-reversal path. They cannot be added to the
  other 112 items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal
  result. Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/
  fan/brightness/duration A/B run.

## ARM64 A32 Native Bitfield Extraction (2026-08-18)

- A32 ARM and Thumb-2 `UBFX` previously expanded to recurring `LSR; AND`; `SBFX` expanded to
  `LSL; ASR`. Dynarmic now retains them as first-class `UnsignedBitFieldExtract32` and
  `SignedBitFieldExtract32` IR. ARM64 emits one native `UBFX` or `SBFX`, and the legal full-width
  `lsb=0,width=32` identity aliases the source without emitting an instruction. x64 and RISC-V
  polyfill the new operations back to the exact established shift/mask graphs.
- Visual inspection of the local Cortex guides found the AArch64 basic `SBFM`/`UBFM` bitfield group
  at latency/throughput 2/3 on A510 page 22, 1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on
  X3 page 18. The A510 footnote lists the simple immediate `LSL`/`LSR`/`ASR` aliases at latency 1,
  predicting a throughput win but a possible dependency tie against the old two-operation graph.
  Rendered review pages were deleted immediately; no manual PDF or rendered page was copied into
  the repository.
- A temporary actual-JIT trace captured raw words `53083e74`, `53054674`, `13083e74`, and
  `13054674`. Host `llvm-objdump` decoded them as `ubfx w20,w19,#8,#8`,
  `ubfx w20,w19,#5,#13`, `sbfx w20,w19,#8,#8`, and `sbfx w20,w19,#5,#13`. The trace hook was
  removed, the final stripped binary contained no trace marker, and its focused test passed all
  8,161 assertions.
- The standalone benchmark was disassembly-checked for the exact old and new instruction bodies.
  It ran four independent chains or one sequential dependency chain, 32,000,000 affected
  operations per sample, 15 samples, and alternating old/new order. Every old/new checksum matched
  and remained nonzero.

  | Guest operation and dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `UBFX`, four independent chains | 1.5054x | 2.0185x | 2.0231x | 2.0579x |
  | `SBFX`, four independent chains | 2.1327x | 2.0176x | 2.0218x | 2.0581x |
  | `UBFX`, sequential chain | 1.0342x | 1.9992x | 1.9997x | 2.0001x |
  | `SBFX`, sequential chain | 1.0209x | 1.9999x | 2.0006x | 1.9994x |

- Permanent coverage checks signed and unsigned ARM/Thumb-2 forms; fields `{0,1}`, `{0,32}`,
  `{31,1}`, `{8,8}`, `{5,13}`, and `{16,16}`; ten boundary/dirty inputs; distinct and
  source/destination-alias operands; every unrelated GPR; NZCV/Q/GE; and FPSCR. The 8,161-assertion
  case passed separately on CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3. The full focused
  `[core][arm][dynarmic]` suite passed 71,138 assertions in 36 cases on CPU 3/A715. A clean native
  ARM64 build passed 2,200 Ninja steps in 13 minutes 18 seconds; the final trace-free incremental
  rebuild passed four steps in 1 minute 24 seconds. Source/test commit `f4bc8cae9` was pushed
  directly to `origin/master` using command-line Git SSH.
- Exact source-commit packaging with `:app:assembleVanillaRelWithDebInfoLite`, JDK 21, ordinary
  Gradle caching, and `--no-configuration-cache` passed in 3 minutes 54 seconds. The retained APK
  is 29,000,672 bytes, reports `f4bc8cae9-vanilla-thor`, and has SHA-256
  `FDD4B07D78AE38C5E7E68CFF544529453253FABC354F6543E39D519AC6C9376C`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported `stopped=true`, the process-ID check was
  empty, and no app UI or game was launched. Thor was USB-powered at 23%, 3.702 V, and 26.0 C, so
  this is not battery-discharge watt evidence.
- Cleanup removed 2,549,202,191 logical host bytes of the native test ELF, benchmark/test helpers,
  and reproducible Gradle/JNI/R8/native-symbol/mapping staging while retaining the APK, its
  476-byte metadata, and the 2,794,765,804-byte active ARM64 CMake/Ninja cache. C: recovered
  2,104,160,256 physical bytes and reported 78,131,527,680 bytes free immediately afterward. The
  four exact temporary device helpers were also removed; no PDF, manual page, benchmark binary, or
  scratch note was committed.
- This is optimization 114 in the overlapping Thor work tally. The 1.02x-2.13x measurements apply
  only while executing these exact bitfield-extract paths. They cannot be added to the other 113
  items or treated as a whole-game FPS, sustained battery-watt, frametime, or thermal result.
  Those still require a matched title/scene/cache/renderer/driver/resolution/layout/mode/fan/
  brightness/duration A/B run.

## ARM64 A32 Native Bitfield Insertion (2026-08-18)

- A32 ARM and Thumb-2 `BFI` previously expanded into four recurring ARM64 instructions: clear the
  destination field with `AND`, shift the source with `LSL`, mask the inserted field with another
  `AND`, and combine with `ORR`. Dynarmic now retains the operation as `BitFieldInsert32` or the
  single-input `BitFieldInsertSelf32` form. ARM64 emits one native `BFI`; x64 and RISC-V polyfill
  the operations back to the exact established graph. A full-width `lsb=0,width=32` replacement
  aliases the source without code, and a self insertion at `lsb=0` is also an identity. `BFC`
  deliberately remains unchanged because its ARM64 logical-immediate clear is already one
  instruction.
- The self opcode is a code-generation requirement, not just an IR naming distinction. The
  distinct lowering consumes a read/write destination and a separate source. The self lowering
  consumes one read/write value and emits `BFI` with the same physical register twice, preventing
  the allocator from materializing a hidden copy before the instruction.
- Visual inspection of the complete local Cortex manual pages found AArch64 `BFM` at
  latency/throughput 2/3 on A510 page 22, 2/2 on A710 page 27, 1/4 on A715 page 20, and 2/2 on X3
  page 18. That predicts large issue-throughput savings everywhere, a true distinct dependency win
  on A715, and possible distinct dependency ties on A510/A710/X3 because the old destination
  `AND` to `ORR` critical path can overlap the source shift/mask work. The rendered pages were
  deleted after review; no PDF or rendered manual page entered the repository.
- A temporary emitter-span trace captured raw words `331b3293` and `331b3273`. Host
  `llvm-objdump` decoded the complete spans as exactly `bfi w19,w20,#5,#13` and
  `bfi w19,w19,#5,#13`, proving both distinct and self forms are one instruction with no hidden
  copy. The diagnostic was removed. The final stripped binary contained no `BFI115` marker and
  was byte-identical to the earlier clean binary: 26,122,328 bytes with SHA-256
  `7244AB37E03937C440C0D75070A74DFE21A36E189AFD737DF3C088DE1B559309`.
- The standalone benchmark's old and new bodies were disassembly-checked. Each invocation used 15
  samples, alternated old/new order, and executed 32,000,000 affected operations per sample.
  Four complete all-core invocations were run; every old/new checksum matched and was nonzero.
  The table reports the median of the four per-invocation medians so one X3 DVFS outlier cannot
  inflate the accepted result.

  | Operand/dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 5 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | Distinct, independent chains | 2.8367x | 2.5211x | 2.0470x | 2.0028x |
  | Self alias, independent chains | 2.8142x | 3.8745x | 2.6386x | 2.1808x |
  | Distinct, sequential chain | 0.9979x | 2.0011x | 1.0000x | 1.0002x |
  | Self alias, sequential chain | 1.5152x | 3.1851x | 1.5004x | 1.5037x |

- Permanent coverage checks ARM and Thumb-2 encodings; fields `{0,1}`, `{0,32}`, `{31,1}`,
  `{8,8}`, `{5,13}`, and `{16,16}`; ten boundary/dirty input pairs; distinct and destination/
  source-alias operands; every unrelated GPR; NZCV/Q/GE; and FPSCR. The 4,081-assertion case
  passed separately on CPU 0/A510, CPU 3/A715, CPU 5/A710, and CPU 7/X3. The final clean focused
  case again passed 4,081 assertions, and the complete `[core][arm][dynarmic]` suite passed 75,219
  assertions in 37 cases on CPU 3/A715. Source/test commit `f13065b0f` was pushed directly to
  `origin/master` using command-line Git SSH.
- Exact source-commit packaging with JDK 17, `:app:assembleVanillaRelWithDebInfoLite`, ordinary
  Gradle caching, and `--no-configuration-cache` passed in 3 minutes 43 seconds. The retained
  ARM64-only APK is 29,001,628 bytes, reports `f13065b0f-vanilla-thor`, and has SHA-256
  `9C84256BFF6FBC7CBAA91C504944CC8C07B9D75A33BB9884B6DC887F6764E6AB`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; Android reported no process after a final force-stop, and no
  app UI or game was launched. Thor reported AC power at 27%, 4.031 V, and 30.0 C, so the timing
  results are not battery-discharge watt evidence.
- Cleanup removed 2,575,854,404 logical host bytes of the native test ELF, benchmark/test/manual-
  render helpers, and reproducible Gradle/JNI/R8/native-symbol/mapping staging. C: recovered
  2,130,010,112 physical bytes and reported 77,687,324,672 bytes free. The retained active ARM64
  CMake/Ninja cache is 2,795,713,152 bytes; retained build output is only the 29,001,628-byte APK
  and its 476-byte metadata. Five exact device helpers totaling 104,500,104 bytes were removed
  from `/data/local/tmp`; no PDF, rendered manual page, benchmark binary, or scratch note was
  committed.
- This is optimization 115 in the overlapping Thor work tally. The 0.9979x-3.8745x measurements
  apply only while executing these exact bitfield-insert patterns; the 0.9979x A510 distinct-chain
  median is an effectively neutral 0.21% difference consistent with the manual-predicted tie. The
  values cannot be added to the other 114 items or treated as whole-game FPS, sustained battery-
  watt, frametime, or thermal results. Those still require a matched title/scene/cache/renderer/
  driver/resolution/layout/mode/fan/brightness/duration A/B run. A32 `MOVT` to native ARM64 `MOVK`
  is the next scalar JIT candidate, but it needs the same alias, disassembly, correctness, and
  all-core measurement gates before implementation.

## ARM64 A32 Native Move Top Half (2026-08-18)

- A32 ARM and Thumb-2 `MOVT` previously built a generic low-half `AND` plus shifted-immediate `OR`
  graph. Dynarmic now retains nonzero forms as `MoveTopHalf32`. ARM64 reads and writes the same
  allocation and emits the exact architectural match, `MOVK Wd,#imm,LSL#16`; x64 and RISC-V
  polyfill back to the established graph. The central emitter retains immediate zero as
  `AND Wd,Wd,#0xffff`, because identity removal already made that old path one instruction.
- The complete local Cortex optimization-guide pages were used, not instruction-count intuition.
  The move-wide family containing `MOVN`/`MOVZ`/`MOVK` is latency/throughput 1/3 on A510 page 22,
  1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on X3 page 18. The Arm architecture semantics
  also match exactly: MOVK retains every destination bit outside the selected 16-bit halfword and
  does not update flags. No PDF or rendered manual page entered Git.
- A temporary emitter trace captured raw JIT word `72a24693`; host `llvm-objdump` decoded it as
  exactly `movk w19,#0x1234,lsl #16`, proving a one-instruction span with no hidden register copy.
  The diagnostic was removed. The final stripped test binary contains no `THOR_MOVT116` marker,
  is 26,128,600 bytes, and has SHA-256
  `179AA28540897777CAA0EC5D3C4D332300FF3843944CD824E7B7B512F81EAD0B`.
- The standalone old/new bodies were disassembly-checked. The representative `0x1234` old body is
  `AND; MOVZ; ORR`, an OR-encodable `0xffff` old body is `AND; ORR`, and the identity-reduced zero
  body is one `AND`; each candidate is one MOVK. Each invocation used 15 samples, alternated
  old/new order, and executed 32,000,000 affected guest operations per sample. Three complete
  invocations ran on each Thor core class; the table reports the median of those three per-run
  medians.

  | Immediate/dependency pattern | A510 CPU 0 | A715 CPU 3 | A710 CPU 6 | X3 CPU 7 |
  | --- | ---: | ---: | ---: | ---: |
  | `0x1234`, independent chains | 2.6231x | 2.8808x | 2.9015x | 2.7145x |
  | `0x1234`, sequential chain | 2.0075x | 2.0001x | 1.9990x | 1.9993x |
  | `0xffff`, independent chains | 2.0936x | 1.9489x | 1.9424x | 1.8513x |
  | `0xffff`, sequential chain | 2.0037x | 2.0000x | 1.9996x | 2.0001x |

- The initial `MOVT #0` candidate was not accepted blindly. Its independent A510 result repeatedly
  regressed by 7.1%-9.0%, while A715/A710 were effectively tied and X3 was DVFS-sensitive. The
  final zero guard emits the identical old one-AND path, so it cannot take that regression while
  every nonzero immediate keeps native MOVK.
- Permanent coverage checks ARM and Thumb-2 encodings; destination registers 0/4/8/12; zero, one,
  boundary, alternating, and dirty immediates; ten boundary/dirty register inputs; every unrelated
  GPR; NZCV/Q/GE; and FPSCR. The final 12,241-assertion case passed on CPU 0/A510, CPU 3/A715,
  CPU 6/A710, and CPU 7/X3. The complete `[core][arm][dynarmic]` suite passed 87,460 assertions in
  38 cases on CPU 3/A715. The diagnostic-free Android ARM64 native build passed in 1 minute 33
  seconds. Source/test commit `31968b954` was pushed directly to `origin/master` with command-line
  Git SSH.
- Exact source-commit packaging with JDK 17, ordinary Gradle caching,
  `:app:assembleVanillaRelWithDebInfoLite`, and `--no-configuration-cache` passed in 3 minutes 10
  seconds. The retained ARM64-only APK is 29,003,004 bytes, reports
  `31968b954-vanilla-thor`, and has SHA-256
  `3948DEE4659E8C91DF1E077604E0493DF1C259C3077B2EBC38066438CABFFAF4`. Wi-Fi ADB installed it
  over `org.azahar_emu.azahar.debug`; a final force-stop left no process, and no app UI or game was
  launched. Thor reported AC power at 59%, 4.226 V, and 35.0 C, so the timing results are not
  battery-discharge watt evidence.
- Cleanup removed 2,549,786,788 logical host bytes of stripped/unstripped tests, trace/benchmark
  helpers, and reproducible Gradle/JNI/R8/native-symbol/mapping staging. C: recovered
  2,108,264,448 physical bytes and reported 75,939,856,384 bytes free. The retained active ARM64
  CMake/Ninja cache is 2,790,551,470 bytes; retained build output is only the 29,003,004-byte APK
  and its 476-byte metadata. Four exact device helpers totaling 79,132,088 bytes were removed from
  `/data/local/tmp`; no PDF, rendered manual page, benchmark binary, or scratch note was committed.
- This is optimization 116 in the overlapping Thor work tally. The accepted 1.85x-2.90x
  independent and about 2.00x dependency results apply only while executing nonzero MOVT paths.
  They cannot be added to the other 115 items or treated as whole-game FPS, sustained battery-
  watt, frametime, or thermal results. Those still require a matched title/scene/cache/renderer/
  driver/resolution/layout/mode/fan/brightness/duration A/B run.

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

6. Crypto workload profiling

   The false-negative CRC32/PMULL configure probes are repaired and the optional units retain
   runtime feature gates. Profile actual 3DS CRC, GCM, and GF(2) workloads before treating those
   latent hardware paths as a gameplay optimization; AES/SHA content paths were already hardware
   accelerated and crypto setup is not currently a sustained-game-FPS premise.

7. Future preprocessed-texture cache evidence

   Texture-filter results already live in the owning rasterizer surface's scaled GPU image until a
   guest write invalidates or uploads the region, so a separate disk cache would add hashing, I/O,
   synchronization, and storage without evidence of a power win. The final screen Anime4K filter
   is different and normally runs once per presented frame because its input changes each frame.

   The per-game manager now reports and deletes the real persistent Vulkan/OpenGL shader caches.
   Add a preprocessed/decoded texture-cache category only after a prototype demonstrates a warm-run
   time or energy win greater than hashing, storage I/O, synchronization, invalidation, and extra
   storage/VRAM costs on Thor. Keep texture dumps and downloaded packs visibly separate; a future
   pack-uninstall action must be labeled as deletion of user content, not cache cleanup.

## Benchmark Checklist

- Test with the release-style Thor APK: `:app:assembleVanillaRelWithDebInfoLite`.
- Use the same Thor Control Center performance mode, fan mode, brightness, and driver before comparing.
- Capture FPS, frametime stability, speed percentage, battery temperature, and whether audio crackles.
- Run one cold-cache pass and one warm-cache pass.
- Record the title ID, region, ROM revision, cheat preset, renderer, internal resolution, secondary display layout, and GPU driver.

## ARM64 A32 Narrow-Store Extension Elision (2026-08-18)

- Ordinary A32 byte and halfword stores previously materialized
  `LeastSignificantByte`/`LeastSignificantHalf` as ARM64 `UXTB`/`UXTH`, then immediately used
  `STRB`/`STRH`, whose architectural write width discarded the upper bits again. The ARM64 emitter
  now aliases the raw word only when the narrow value has exactly one use and that use is matching
  `A32WriteMemory8`/`A32WriteMemory16`. Shared values, other U8/U16 consumers, exclusive stores,
  mismatched widths, and endian-reversal paths retain canonical narrowing.
- The local Cortex software-optimization manuals identify `UXTB`/`UXTH` as aliases in the baseline
  `UBFM` group on X3 page 18, A715 page 20, A710 pages 27-28, and A510 pages 22-23. `STRB` and
  `STRH` consume the low byte/halfword by definition. This proves that the extension is redundant
  for the gated shape and that removing it saves an integer instruction; it does not prove a
  store-pipeline throughput or battery-power gain. No PDF or rendered manual page entered Git.
- Temporary emitter traces captured raw JIT words `38334b34` and `78334b34`. Capstone decoded the
  complete wrapper spans as exactly `strb w20, [x25, w19, uxtw]` and
  `strh w20, [x25, w19, uxtw]`, with no hidden extension or register copy. The diagnostic was
  removed. The final trace-free stripped test binary is 26,136,392 bytes, contains no
  `THOR_NARROW117` marker, and has SHA-256
  `295DDE99220E6B7BD7651A87EB348370AAFDB70F6F65E185890210A7F341F9FE`.
- A disassembly-checked standalone helper compared four independent old `UXTB/UXTH; STRB/STRH`
  chains against four direct stores. Each of 15 alternating-order samples executed 32,000,000
  affected stores and required matching byte/halfword checksums of 510/131070. The values below
  are old time divided by new time; A510 uses five invocation medians, A715 and A710 use three,
  and X3 reports the one valid invocation before Android `core_ctl` intermittently rejected later
  CPU 6/7 affinity masks with `EINVAL`:

  | Thor core | Direct `STRB` | Direct `STRH` |
  | --- | ---: | ---: |
  | A510 CPU 0 | 0.999368x | 1.002850x |
  | A715 CPU 3 | 0.999791x | 0.999637x |
  | A710 CPU 5 | 1.000054x | 1.000341x |
  | X3 CPU 7, one accepted invocation | 1.000024x | 1.000072x |

- The store-saturated loop is therefore throughput-neutral within noise even though the generated
  path falls from two host instructions to one. The accepted benefit is lower generated-code size
  and less fetch/decode/integer-issue work when these guest stores execute; a watt reduction is a
  plausible hypothesis, not a measured result.
- Permanent ARM and Thumb-16 coverage exercises `STRB`/`STRH`, distinct data/base operands and
  data-equals-base aliases, eight dirty/boundary inputs, callback and fastmem paths, all unrelated
  GPRs, NZCV/Q/GE, and FPSCR. The focused case passed 2,592 assertions on A510 CPU 0, A715 CPU 3,
  A710 CPU 6, and X3 CPU 7. The clean CPU-3 ARM64 Dynarmic suite passed 90,052 assertions in 39
  cases. The source change is commit `8bb915e32` and is pushed to `origin/master`.
- The exact post-commit JDK 17 `:app:assembleVanillaRelWithDebInfoLite`
  `--no-configuration-cache` build passed. The 29,003,652-byte APK has SHA-256
  `6241014BD33858C1A3BB37FC017C28968167398B02DC4E598B2E7B870D6AC58F`, package
  `org.azahar_emu.azahar.debug`, and version `8bb915e32-vanilla-thor`. It was installed over Wi-Fi
  ADB at `192.168.1.33:5555`, then verified `stopped=true` with no PID; the app/game was not
  launched.
- Cleanup removed 2,575,311,320 logical bytes: the 104,561,839-byte local scratch tree, the
  448,896,104-byte unstripped native test executable, and all reproducible Gradle staging. The
  reusable ARM64 CMake/Ninja cache is 2,796,842,775 bytes; retained Gradle output is only the
  29,003,652-byte APK and its 476-byte metadata. C: free space increased by 1,909,010,432 physical
  bytes from the pre-clean audit. All five exact `/data/local/tmp` helpers were removed.
- This is optimization 117 in the overlapping Thor work tally. It is not additive with the other
  116 entries and does not establish whole-game FPS, frametime, thermal, or wattage gains. Those
  claims still require a controlled matched title/scene/cache/renderer/driver/resolution/layout/
  performance-mode/fan/brightness/duration A/B run, which was intentionally not performed because
  the current instruction is not to launch the app.

## ARM64 A32 Signed Narrow-Load Fusion (2026-08-18)

- A32 `LDRSB`/`LDRSH` previously reached the ARM64 backend as an unsigned `A32ReadMemory8`/
  `A32ReadMemory16` followed by `SignExtendByteToWord`/`SignExtendHalfToWord`. Direct fastmem and
  page-table hits therefore emitted `LDRB; SXTB` or `LDRH; SXTH`. Dynarmic now emits one native
  `LDRSB`/`LDRSH` only when the extension is the load's sole immediately following consumer. The
  extension aliases the load result without code. Shared, non-adjacent, mismatched, ordered/
  acquire, exclusive, endian-reversed, A64, and unrelated shapes retain the established lowering;
  callback and fastmem/page-table fault fallbacks still sign-extend their narrow return explicitly.
- The complete external Cortex optimization-guide tables were inspected directly. Basic register-
  offset `LDRB`/`LDRSB` and `LDRH`/`LDRSH` share latency/throughput 4/3 on X3 page 19, 4/3 on A715
  page 21, 4/3 on A710 page 29, and 2/2 on A510 page 24. The same-cost signed load removes a real
  `SBFM` alias and dependency without enabling an optional ISA extension. No PDF or rendered manual
  page entered Git.
- A temporary emitter-boundary trace captured raw JIT words `38f34b34`, `78f34b34`, `38f54b33`,
  and `78f54b33`. Independent Capstone decoding identified exactly
  `ldrsb w20,[x25,w19,uxtw]`, `ldrsh w20,[x25,w19,uxtw]`,
  `ldrsb w19,[x25,w21,uxtw]`, and `ldrsh w19,[x25,w21,uxtw]`. The diagnostic and the unavailable
  Android disassembly experiment were removed before the final build; no trace marker remains.
- The standalone helper was disassembly-checked so each split body contained eight
  `LDRB/LDRH; SXTB/SXTH` pairs and each fused body contained eight `LDRSB/LDRSH` instructions,
  with the same eight adds, loop control, and nonzero checksums (`ffc2f700` byte, `7ff8f700`
  halfword). Each of eight alternating-order samples executed 4,000,000 iterations, or 32,000,000
  affected loads. The table reports median nanoseconds per affected load and old/fused speedup.

  | Thor core and path | Split ns/load | Fused ns/load | Old/fused |
  | --- | ---: | ---: | ---: |
  | A510 CPU 0, byte | 0.686038 | 0.556078 | 1.2337x |
  | A510 CPU 0, halfword | 1.203090 | 0.556183 | 2.1631x |
  | A715 CPU 3, byte | 0.370259 | 0.364127 | 1.0168x |
  | A715 CPU 3, halfword | 0.370347 | 0.363895 | 1.0177x |
  | A710 CPU 6, byte | 0.357950 | 0.357806 | 1.0004x |
  | A710 CPU 6, halfword | 0.357780 | 0.357963 | 0.9995x |

- The A510 loop therefore used 18.9% less median affected-path time for bytes and 53.8% less for
  halfwords; A715 used about 1.7% less, while A710 was neutral within 0.1%. Android intermittently
  rejected the CPU 5 and CPU 7 affinity masks with `EINVAL` despite listing them online, so no X3
  number is invented. The manuals still establish no extra signed-load cost on X3. These are
  load/accumulate-loop results, not an instruction-frequency-weighted emulator estimate.
- Permanent ARM and Thumb coverage checks byte/halfword loads, distinct and destination-equals-
  base forms, ten signed boundaries, callback and fastmem paths, every unrelated GPR, NZCV/Q/GE,
  and FPSCR. The focused case passed 3,040 assertions on CPU 3/A715. The full suite initially
  exposed and prevented two integration mistakes in unrelated producer handling and fallback
  register-allocation lifetime; after correction, the clean trace-free `[core][arm][dynarmic]`
  suite passed 93,092 assertions in 40 cases. Source/test commit `3f76c7440` was pushed directly to
  `origin/master` with command-line Git SSH.
- Exact post-commit packaging with JDK 17,
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache`, and ordinary Gradle caching
  passed in 3 minutes 41 seconds. The ARM64-only APK is 29,006,964 bytes, has SHA-256
  `44AF95AEB8A26DB45FE48F5C3464191A54DABEB97DD80522FF475C257CE972B8`, and reports package
  `org.azahar_emu.azahar.debug` version `3f76c7440-vanilla-thor`. Wi-Fi ADB installed it, then a
  force-stop verified `stopped=true` with no PID; neither the app nor a game was launched. Thor
  reported AC power at 80%, 4.273 V, and 25.0 C, so this is not battery-discharge watt evidence.
- Cleanup removed 2,575,457,579 logical host bytes: the 104,589,660-byte scratch set, the
  448,949,152-byte unstripped test ELF, and reproducible Gradle staging. C: recovered
  2,133,901,312 physical bytes and reported 56,508,461,056 bytes free. The retained active ARM64
  CMake/Ninja cache is 2,797,119,953 bytes; retained build output is only the 29,006,964-byte APK
  and 476-byte metadata. Five exact device helpers totaling 104,582,416 bytes were removed from
  `/data/local/tmp`; no PDF, benchmark, test binary, rendered manual page, or scratch note was
  committed.
- This is optimization 118 in the overlapping Thor work tally. The 0.9995x-2.1631x exact-loop
  results apply only while executing these signed narrow-load shapes and cannot be added to the
  other 117 items. Removing one generated instruction reduces code-cache, fetch/decode, and
  integer-issue work, so lower energy is plausible, but whole-game FPS, frametime, thermal, and
  wattage claims still require a controlled matched title/scene/cache/renderer/driver/resolution/
  layout/performance-mode/fan/brightness/duration A/B run.

## ARM64 Chained Narrow-to-Long Sign Extension (2026-08-18)

- Dynarmic can form `SignExtendByteToWord -> SignExtendWordToLong` or
  `SignExtendHalfToWord -> SignExtendWordToLong`. ARM64 previously emitted two dependent baseline
  bitfield aliases: `SXTB/SXTH Wd,Wn`, then `SXTW Xd,Wd`. AArch64 defines direct
  `SXTB/SXTH Xd,Wn` forms with the same result: sign-extending bits 7 or 15 directly to 64 bits is
  identical to first sign-extending them to 32 bits and then sign-extending bit 31 to 64 bits.
- The accepted emitter gate requires a non-immediate narrow source, exactly one use, and an
  immediately following `SignExtendWordToLong` consumer using the narrow extension as argument
  zero. The word extension aliases its input without code, and the long extension emits direct
  `SXTB X` or `SXTH X`. Immediate, shared, non-adjacent, mismatched, word-only, and unrelated forms
  retain the original lowering. Producer and consumer predicates are derived from the same helper
  so a fallback cannot consume an unextended value.
- ARM/Thumb-2 `SMLAWB`/`SMLAWT` currently produce the halfword chain while preserving their exact
  multiply, shift, modular add, overflow extraction, and sticky-Q graph. This optimization removes
  only the redundant intermediate sign extension; it does not apply the previously rejected
  fused/reassociated SMLAW multiply-accumulate candidate that regressed A715 and X3.
- Complete Cortex software-optimization tables were inspected directly. Baseline `SBFM` aliases
  are latency/throughput 1/6 on X3 page 18, 1/4 on A715 page 20, and 1/4 on A710 page 27. A510 page
  22 lists the basic family at 2/3, with latency 1 for the `SXTB` alias. The manuals therefore
  predict a dependency and issue reduction from deleting the second operation without requiring
  an optional extension or a core-specific build. No PDF or rendered manual page entered Git.
- A temporary emitter-boundary trace captured raw JIT words `93403e75`, `93403eb3`, and
  `93403eb6`. LLVM decoded them as `sxth x21,w19`, `sxth x19,w21`, and `sxth x22,w21` respectively.
  There was no preceding word-form `SXTH` or trailing `SXTW`. The diagnostic and temporary opcode
  object were removed before the final build.
- The standalone helper was disassembly-checked so its split independent bodies contained eight
  `SXTB/SXTH W` plus eight `SXTW X` instructions and its direct bodies contained eight
  `SXTB/SXTH X` instructions. Dependent bodies repeated the same shapes on one register. Each of
  eight alternating-order samples executed 4,000,000 iterations, or 32,000,000 affected
  operations, with matching nonzero split/direct checksums. The table reports old/direct median
  speedup.

  | Thor core | Byte independent | Half independent | Byte dependent | Half dependent |
  | --- | ---: | ---: | ---: | ---: |
  | A510 CPU 0 | 4.343224x | 4.259255x | 3.053356x | 1.986687x |
  | A715 CPU 3 | 1.815167x | 1.839695x | 1.999006x | 1.998513x |
  | A710 CPU 5 | 1.891211x | 1.891639x | 2.001035x | 1.999644x |
  | X3 CPU 7 | 2.001748x | 2.002068x | 2.000773x | 1.999879x |

- Android initially rejected CPU 7 affinity with `EINVAL` despite reporting CPUs 0-7 online and
  permitted. Brief load on the accessible big-core cluster let `core_ctl` accept the X3 run; all
  helpers were stopped afterward. Thor reported AC power, 80%, 4.271 V, and 25.0 C. These are
  instruction-sequence timings under wall power, not battery-discharge watt measurements.
- Permanent ARM and Thumb coverage exercises SMLAW bottom/top forms, distinct operands,
  destination aliases with each source role and the all-alias form, positive/negative overflow,
  initial sticky Q, unchanged NZCV/GE, every unrelated GPR, and unchanged FPSCR. The focused case
  passed 4,080 assertions. The clean final `[core][arm][dynarmic]` suite passed 97,172 assertions
  in 41 cases on Thor. Source/test commit `a8611bf76` was pushed directly to `origin/master` with
  command-line Git SSH.
- Exact JDK 17 packaging with
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed in 3 minutes 32 seconds.
  The ARM64-only APK is 29,006,484 bytes,
  has SHA-256 `ECE6AA41909B2571C6161A3513B6AEAD8794C9BE561516371E8A2B1D6468D4FA`, and reports package
  `org.azahar_emu.azahar.debug` version `a8611bf76-vanilla-thor`. It was installed over Wi-Fi ADB,
  immediately force-stopped, and verified `stopped=true` with no PID; neither the app nor a game
  was launched.
- Cleanup removed 2,497,115,187 logical host bytes: the 26,167,732-byte scratch tree, the
  448,982,400-byte unstripped native test executable, and reproducible Gradle staging. C: recovered
  2,070,192,128 physical bytes and reports 56,486,883,328 bytes free. The retained active ARM64
  CMake/Ninja cache is 2,797,255,579 bytes; retained Gradle output is only the 29,006,484-byte APK
  and 476-byte metadata. Both exact device helpers were removed; no PDF, benchmark, test binary,
  rendered manual page, or scratch note was committed.
- This is optimization 119 in the overlapping Thor work tally. The 1.815167x-4.343224x figures
  apply only to the exact removed instruction chain and cannot be added to the other 118 entries.
  Lower code-cache, fetch/decode, dependency, and integer-issue work makes lower energy plausible,
  but whole-game FPS, frametime, temperature, thermal slope, and watts still require a controlled
  matched title/scene/cache/renderer/driver/resolution/layout/performance-mode/fan/brightness/
  duration A/B run.

## ARM64 Small Shifted-ADD Folding and VABA Rejection (2026-08-18)

- The first candidate was the obvious AArch64 replacement for same-width A32/A64 `VABA`:
  `SABD/UABD` followed by `ADD` can be expressed as one native accumulating `SABA/UABA`. The
  complete Cortex tables warned that this was not uniformly cheaper. X3 page 25 lists
  `SABD/UABD` latency/throughput 2/4 and `SABA/UABA` 4(1)/2; A715 pages 27-28 and A710 page 42
  list 2/2 versus 4(1)/1; A510 page 35 lists `SABD/UABD` latency 3 with split `2,1` throughput but
  `SABA/UABA` latency 6 with `1/2,1/4` throughput. No PDF or rendered page entered Git.
- A disassembly-checked helper compared eight independent and eight true accumulator-chain
  `SABD/UABD; ADD` operations with matching `SABA/UABA` operations. Each of nine alternating-order
  samples executed 4,000,000 iterations, or 32,000,000 affected operations, with matching
  checksums. Old/fused medians on A510 were:

  | Form | Independent | Accumulator dependency |
  | --- | ---: | ---: |
  | `SABA.8` | 2.005866x | 0.670348x |
  | `SABA.16` | 1.868680x | 0.670527x |
  | `SABA.32` | 1.972331x | 0.688968x |
  | `UABA.8` | 2.051850x | 0.671513x |
  | `UABA.16` | 1.971682x | 0.661446x |
  | `UABA.32` | 1.999445x | 0.659546x |

- The same six forms were 1.0168x-1.0337x independent and 1.9991x-2.0005x dependent on A715,
  0.9994x-1.0012x independent and 1.6656x-1.6671x dependent on A710, and
  1.9237x-2.0002x independent and 1.7325x-1.7339x dependent on X3. The native accumulator looked
  excellent on most big-core patterns but made the A510 accumulator chain 31.1%-34.0% slower.
  Global fusion was therefore rejected and no VABA source change was retained.
- The accepted candidate instead targets ordinary no-flags A32 adds whose second operand is a
  sole-use, immediately adjacent `LogicalShiftLeft32` by an immediate 1 through 4. ARM64 aliases
  the shift result to its input without emitting code and uses one
  `ADD Wd,Wbase,Windex,LSL #shift`. Flags/carry, shared/non-adjacent shifts, immediate sources,
  variable/zero shifts, shifts 5 through 31, subtraction, and unrelated consumers retain the old
  lowering. The producer and consumer use the same eligibility helper so a fallback cannot read an
  unshifted alias.
- The exact helper compared eight independent, eight base-dependent, and eight index-dependent
  old `LSL; ADD` chains with one-instruction shifted ADD chains. The main run used 4,000,000
  iterations and nine alternating-order samples per shape. A longer A510 confirmation used
  8,000,000 iterations, or 64,000,000 affected operations, and 15 samples. Old/fused A510 results
  for the retained gate were:

  | Shift | Independent | Base dependency | Index dependency |
  | ---: | ---: | ---: | ---: |
  | 1 | 1.286088x | 1.001042x | 1.007412x |
  | 2 | 1.475840x | 0.981775x | 0.997746x |
  | 3 | 1.335876x | 0.989126x | 0.987284x |
  | 4 | 1.492759x | 1.012459x | 1.006867x |

- For shifts 1 through 4, A715 independent/base/index results were 1.8124x-1.8351x,
  1.008x-1.011x, and 1.9985x-2.0005x; A710 was 1.890x-1.893x, about 1.000x, and
  1.999x-2.000x; X3 was 1.8477x-2.0012x, about 1.000x, and about 2.000x. The bounded gate trades
  large independent/front-end and big-core shifted-index wins for A510 dependency results close to
  one. It does not claim every synthetic dependency shape improves.
- Wider shifts were explicitly rejected. For shifts 16/31 the base-dependent form fell to about
  0.505x on A715, 0.500x on A710, and 0.500x on X3 even though independent forms sometimes won.
  The permanent gate therefore stops at four instead of assuming that fewer instructions always
  means better heterogeneous-core scheduling.
- Temporary emitter instrumentation captured 128 fused JIT emissions: 32 each for shifts 1, 2,
  3, and 4. The unique words were `0b130674`, `0b130a74`, `0b130e74`, and `0b131274`; their opcode
  and immediate fields validate as single 32-bit shifted-register ADD instructions with immediate
  1, 2, 3, and 4. The trace was removed, the emitter and production library were rebuilt, and both
  the 449,025,552-byte unstripped and 26,153,672-byte stripped clean test binaries contained no
  trace marker.
- Permanent ARM and Thumb-2 coverage exercises shifts 1/2/3/4 plus unfused 5/16/31, distinct
  operands, destination/base/index aliases, base-equals-index and all-alias forms, dirty/boundary
  inputs, 32-bit wrap, every unrelated GPR, NZCV/Q/GE, and FPSCR. The clean focused case passed
  9,520 assertions, and the final Thor `[core][arm][dynarmic]` suite passed 106,692 assertions in
  42 cases. Source/test commit `e984ad250` was pushed directly to `origin/master` with command-line
  Git SSH.
- Exact post-commit JDK 17 packaging with
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed in 3 minutes 7 seconds.
  The ARM64-only APK is 29,007,336 bytes, has SHA-256
  `039E69AF41858A704545E4ABB3BCB4610C0F99C038B9F8DDBC95A25757FD0B6A`, and reports package
  `org.azahar_emu.azahar.debug` version `e984ad250-vanilla-thor`. Wi-Fi ADB installed it, then a
  force-stop verified `stopped=true` with no PID; neither app nor game was launched. Thor reported
  AC power, 80%, 4.271 V, and 25.0 C, so this is not battery-discharge watt evidence.
- Cleanup removed 2,550,451,023 logical host bytes: 79,360,075 bytes of scratch, the
  449,025,552-byte unstripped test ELF, and reproducible Gradle/JNI/R8/native-symbol/mapping
  staging. C: recovered 2,106,241,024 physical bytes and reports 56,474,251,264 bytes free. The
  retained active ARM64 CMake/Ninja cache is 2,797,552,350 bytes; retained build output is only the
  29,007,336-byte APK and 476-byte metadata. Three present device helpers totaling 52,669,048
  bytes were removed from `/data/local/tmp`; the already-absent VABA helper was also included in
  the exact cleanup command. Existing unrelated screenshots were left untouched. No PDF,
  benchmark, test binary, rendered manual page, or scratch note was committed.
- This is optimization/candidate entry 120 in the overlapping Thor ledger: one bounded shifted-ADD
  optimization shipped and one unsafe global VABA fusion was permanently rejected. The exact-loop
  ratios cannot be added to the other 119 entries or treated as whole-game FPS, sustained watts,
  frametime, or thermal improvement. Those still require a controlled matched title/scene/cache/
  renderer/driver/resolution/layout/performance-mode/fan/brightness/duration A/B run, which was
  intentionally not performed because the app/game was not to be launched.

## ARM64 Right-Shifted ADD Folding (2026-08-18)

- Command-line Git fetched authoritative `upstream/master` at
  `db15d78feb97ed19b6fc0354481e74694d339594`; the fork was 221 commits ahead and zero behind before
  this work, so no upstream merge was needed. Work remained on `master` and used the configured SSH
  remotes.
- Dynarmic previously materialized a sole A32 `LogicalShiftRight32` or
  `ArithmeticShiftRight32` before an immediately following flag-free `Add32`, even though AArch64
  can encode the shift directly in `ADD`. The retained gate requires one use, immediate adjacency,
  a non-immediate source, an immediate 1..31, no shift carry pseudo, no ADD flag/overflow pseudo,
  and carry-in false. Shared, non-adjacent, immediate-source, variable, zero/32, flag/carry,
  subtraction, and unrelated forms retain the old lowering. The separately measured LSL gate stays
  at 1..4.
- A disassembly-checked helper compared eight independent, eight base-dependent, and eight
  index-dependent old `LSR/ASR; ADD` bodies with one shifted-register `ADD`. Representative shifts
  1/2/3/4/8/16/31 used 4,000,000 iterations, or 32,000,000 affected operations per body, and nine
  alternating-order samples. Every old/fused checksum matched. Old-over-fused median ranges were:

  | Thor core | Independent | Base dependency | Index dependency |
  | --- | ---: | ---: | ---: |
  | A510 CPU 0 | 1.381850x-1.771904x | 1.437729x-1.872461x | 1.379672x-1.826679x |
  | A715 CPU 3 | 1.072932x-1.077689x | 1.087407x-1.093244x | 1.094345x-1.100728x |
  | A710 CPU 6 | 1.061272x-1.064909x | 1.059394x-1.064169x | 1.166458x-1.170341x |
  | X3 CPU 7 | 0.999391x-1.002055x | 1.015065x-1.124654x | 1.057515x-1.337368x |

  Thor reported AC power, 80% charge, 4.271 V, and 25.0 C at the start. These are wall-powered
  instruction-kernel timings, not battery-discharge watt measurements.

- The first X3 confirmation was discarded because the temporary core-wake workers were still
  eligible to run on CPU 7. The corrected protocol killed and reaped those workers before timing.
  Its first LSR#1 row still caught frequency settling, so a preconditioned 20,000,000-iteration,
  21-sample confirmation replaced it: 0.999391x independent, 1.024746x base-dependent, and
  1.337368x index-dependent. The table conservatively excludes one benefit-inflating LSR#2
  frequency-settling outlier. No contaminated result controls the gate.
- Temporary actual-emitter tracing plus `llvm-objdump` proved the generated words. LSR shifts
  1/2/3/4/5/16/31 decoded from `0b530674`, `0b530a74`, `0b530e74`, `0b531274`, `0b531674`,
  `0b534274`, and `0b537e74`; ASR used `0b930674`, `0b930a74`, `0b930e74`, `0b931274`,
  `0b931674`, `0b934274`, and `0b937e74`. Each is one
  `add w20,w19,w19,lsr/asr #shift`. Negative gates decoded as standalone LSL for shifts 5/16/31,
  `mov w20,wzr` for LSR32, and standalone `asr w20,w19,#31` for ASR32. The trace hook was removed;
  the 449,035,480-byte clean unstripped and 26,154,632-byte stripped test binaries contained zero
  trace markers and the clean stripped SHA-256 returned to
  `579B9E94800A20B8FA32FB5CC465104A672981BE242537CFF28F3B852EBDB8E7`.
- Permanent ARM and Thumb-2 coverage now exercises LSL/LSR/ASR; encoded shifts
  0/1/2/3/4/5/16/31; distinct operands; destination/base/index, base/index, and all-way aliases;
  signed ASR boundaries; modular wrap; every unrelated GPR; NZCV/Q/GE; and FPSCR. The clean focused
  case passed 32,640 assertions separately on A510 CPU 0, A715 CPU 3, A710 CPU 6, and X3 CPU 7.
  The clean full `[core][arm][dynarmic]` suite passed 129,812 assertions in 42 cases on A715.
  Source/test commit `752115dc9` was pushed directly to `origin/master` over command-line Git SSH.
- Exact JDK 17 packaging from source commit `752115dc9` with
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed in 3 minutes 1 second.
  The ARM64-only APK is 29,008,576 bytes, has SHA-256
  `69937CA0CF18154A214FB3525ED5556169625A0493D310D0DB38F216219460AE`, and reports package
  `org.azahar_emu.azahar.debug` version `752115dc9-vanilla-thor`. Wi-Fi ADB installed it, then a
  force-stop verified `stopped=true` with no PID. Neither app nor game was launched.
- Exact cleanup removed 2,497,807,188 logical host bytes: 26,712,598 bytes of bounded scratch, the
  449,035,480-byte unstripped test ELF, and 2,022,059,110 bytes of reproducible Gradle/JNI/R8/
  native-symbol/mapping staging. C: recovered 2,027,413,504 physical bytes and reports
  56,242,896,896 bytes free. The retained active ARM64 CMake/Ninja cache is 2,797,658,936 bytes;
  retained build output is only the 29,008,576-byte APK and 476-byte metadata. Both exact device
  helpers totaling 26,697,560 bytes were removed from `/data/local/tmp`. No PDF, benchmark, test
  binary, rendered manual page, or scratch note was committed.
- This is optimization/candidate entry 121 in the overlapping Thor ledger. It removes one host
  instruction only when matching guest shifts execute; its kernel ratios cannot be added to the
  other 120 entries or converted into emulator-wide FPS or watts. Lower code-cache, fetch/decode,
  dependency, and integer-issue work makes lower energy plausible, but whole-game frametime,
  temperature, thermal slope, and battery watts still require a controlled matched title/scene/
  cache/renderer/driver/resolution/layout/performance-mode/fan/brightness/duration A/B run.

## ARM64 Shifted SUB Folding (2026-08-19)

- Command-line Git fetched authoritative `upstream/master` at
  `db15d78feb97ed19b6fc0354481e74694d339594`; the fork was 223 commits ahead and zero behind before
  this work, so no merge was needed. Work remained on `master`, and both source and documentation
  commits used the configured command-line Git SSH remote. RPCS3's current
  [`Avoid redundant XFloat normalization in SELB`](https://github.com/RPCS3/rpcs3/commit/82164a5),
  [`Avoid redundant copy when writing to mip level or Z layer`](https://github.com/RPCS3/rpcs3/commit/ad059d0),
  and earlier [`Shorten FI dependency chain`](https://github.com/RPCS3/rpcs3/commit/27f0e87)
  changes reinforced the transferable pattern of removing redundant materialization only behind
  an exact semantic predicate; no RPCS3 code was copied into this 3DS emulator.
- Dynarmic previously emitted a standalone immediate LSL/LSR/ASR and then `SUB` for ordinary
  no-flags A32 subtraction. ARM64 now aliases a sole immediately adjacent shift producer to its
  non-immediate input and emits one shifted-register `SUB` when the immediate is 1..31. The shared
  gate proves normal `Sub32` carry-in true, no associated arithmetic or shift pseudo-operation,
  one producer use, immediate adjacency, and a non-immediate shift source. Shared, non-adjacent,
  immediate-source, variable, zero/32, flag/carry, borrow/reverse-subtract, and unrelated cases
  retain the established split lowering. ADD keeps its independently measured LSL 1..4 gate.
- A disassembly-checked helper compared four independent, eight base-dependent, and eight
  shifted-index-dependent `LSL/LSR/ASR; SUB` bodies with one shifted-register `SUB`. Shifts
  1/2/3/4/5/8/16/31 used 1,000,000 iterations and nine alternating-order samples in the all-core
  run, with matching checksums throughout. Old-over-fused median ranges were:

  | Thor core | Independent | Base dependency | Index dependency |
  | --- | ---: | ---: | ---: |
  | A510 CPU 0 | 1.9168x-2.1679x | 1.4962x-1.7710x | 1.4865x-1.7664x |
  | A715 CPU 3 | 1.0837x-1.4598x | 1.1102x-1.7645x | 1.1414x-1.7961x |
  | A710 CPU 6 | 1.1343x-1.7994x | 1.0582x-1.6535x | 1.1678x-1.8219x |
  | X3 CPU 7, LSL #1..#4 | 1.934x-2.000x | 2.196x-2.238x | 2.178x-2.222x |
  | X3 CPU 7, other forms | approximately neutral | 1.016x-1.125x | 1.067x-1.117x |

  Thor began the run on AC power at 80%, 4.271 V, and 25.0 C. These are wall-powered
  instruction-kernel timings, not battery-discharge watt measurements.
- The first long X3 confirmation was discarded because its temporary core-wake workers had not
  yet been reaped. Corrected 20,000,000-iteration, 21-sample independent runs killed and reaped the
  helpers before timing: LSL #1..#4 measured 1.9566x-1.9814x, while wider LSL and every LSR/ASR
  form were approximately neutral at 0.9996x-1.0024x. A doubled-work 40,000,000-iteration,
  21-sample ASR check measured 0.9997x-1.0024x, rejecting one isolated ASR #2 loss as
  non-repeatable. No contaminated result controls the shipped gate.
- Temporary actual-emitter tracing captured final Dynarmic words for LSL, LSR, and ASR shifts
  1/2/3/4/5/16/31. `llvm-objdump` decoded the `4b13....`, `4b53....`, and `4b93....` families as
  exactly one `sub w20,w19,w19,lsl/lsr/asr #shift`. The trace hook was removed before the final
  build. The clean unstripped test ELF was 449,051,840 bytes; its temporary stripped copy was
  26,156,296 bytes with SHA-256
  `D11DF48D8C9621CF8F26288AFF5338A57254F9569740AB38767F896A3C6D7482`.
- Permanent ARM and Thumb-2 coverage exercises both ADD and SUB, LSL/LSR/ASR, encoded shifts
  0/1/2/3/4/5/16/31, distinct operands, destination/base/index and all-way aliases, dirty and
  signed-boundary inputs, modular 32-bit wrap, every unrelated GPR, NZCV/Q/GE, and FPSCR. The clean
  focused case passed 65,280 assertions separately on A510 CPU 0, A715 CPU 3, A710 CPU 6, and X3
  CPU 7 during the verification sequence; the final trace-free build passed it again on A715. The
  final full `[core][arm][dynarmic]` suite passed 162,452 assertions in 42 cases on A715. Source and
  test commit `88b4da62d` was pushed directly to `origin/master` over command-line Git SSH.
- Exact post-commit JDK 17 packaging with
  `:app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` passed in 2 minutes 11 seconds.
  The ARM64-only APK is 29,008,048 bytes, has SHA-256
  `E1B145275CA80454EB2DAB5A9A9C405BF640498D70E2059430DEF89F4AF6B246`, and reports package
  `org.azahar_emu.azahar.debug` version `88b4da62d-vanilla-thor`. Wi-Fi ADB installed it, then a
  force-stop verified `stopped=true` with no PID. Neither app nor game was launched.
- Exact cleanup removed 2,497,380,018 logical host bytes: 26,222,307 bytes of bounded scratch, the
  449,051,840-byte unstripped test ELF, and reproducible Gradle/JNI/R8/native-symbol/mapping
  staging. C: recovered 2,055,483,392 physical bytes and reports 56,370,929,664 bytes free. The
  retained active ARM64 CMake/Ninja cache is 2,791,814,967 bytes; retained build output is only the
  29,008,048-byte APK and 476-byte metadata. Both exact device helpers totaling 26,210,120 bytes
  were removed from `/data/local/tmp`. No PDF, benchmark, test binary, rendered manual page, or
  scratch note was committed.
- This is optimization/candidate entry 122 in the overlapping Thor ledger. It removes one host
  instruction only when matching guest shifted-subtract paths execute; its exact-loop ratios
  cannot be added to the other 121 entries or converted into whole-emulator FPS or watts. Lower
  code-cache, fetch/decode, dependency, and integer-issue work makes lower energy plausible, but
  whole-game frametime, thermal slope, and battery watts still require a controlled matched
  title/scene/cache/renderer/driver/resolution/layout/performance-mode/fan/brightness/duration A/B.
