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

6. Crypto feature-dispatch audit

   The Android build detects ARMv8 and NEON headers and already enables the AES-oriented Crypto++
   path, but its configure result leaves CRC32 and PMULL disabled. Profile actual 3DS crypto and
   checksum workloads, verify compiler/runtime feature gating, and add focused vectors before
   enabling anything else; crypto setup is not currently a sustained-game-FPS premise.

## Benchmark Checklist

- Test with the release-style Thor APK: `:app:assembleVanillaRelWithDebInfoLite`.
- Use the same Thor Control Center performance mode, fan mode, brightness, and driver before comparing.
- Capture FPS, frametime stability, speed percentage, battery temperature, and whether audio crackles.
- Run one cold-cache pass and one warm-cache pass.
- Record the title ID, region, ROM revision, cheat preset, renderer, internal resolution, secondary display layout, and GPU driver.
