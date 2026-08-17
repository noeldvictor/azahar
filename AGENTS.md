# Agent Notes

- Always work directly on the repository's existing default branch (`master` here, or `main` in repositories that use it). Do not create, switch to, or leave work on any other branch.
- Commit small, coherent, verified slices directly on `master` and push them to `origin/master` frequently. Never include unrelated user files or generated build output just to make a checkpoint.
- Use command-line Git over the repository's SSH remotes for status, fetch, commit, and push operations. Do not use GitHub workflow guides, PR automation, web publishing flows, or the GitHub CLI unless the user explicitly asks for them.
- Keep fork-specific source, patches, tests, and documentation in this repository. Do not create a separate repository or fork for a customized dependency; vendor that dependency here when a normal submodule commit would otherwise require another remote.
- `externals/soundtouch` is intentionally vendored from former submodule commit `9ef8458d8561d9471dd20e9619e3be4cfe564796` so its Thor AArch64 overlap path stays in this repository. Do not restore it to a gitlink; retain the LGPL license and omit unused prebuilt example binaries.
- SoundTouch integer samples require an exact 32-bit `LONG_SAMPLETYPE`; never change it back to C++
  `long`, which is 64-bit under Android's AArch64 LP64 ABI and scalarizes the FIR. The AArch64
  stereo FIR must reuse the canonical coefficient vector for both channels while `LD2`
  deinterleaves samples, preserving the 64 taps, signed accumulation, arithmetic divide-by-16384,
  saturation, generic non-AArch64 coefficient-table path, and exact output. Final linked code should
  retain paired coefficient loads, two sample `LD2`, independent `SMLAL`/`SMLAL2` accumulators, and
  `ADDV` reductions per sixteen taps rather than duplicated coefficient `LD2` or scalar `SMADDL`.
- SoundTouch's integer WSOLA correlation state (`corr`, rolling `lnorm`, and `maxnorm`) is also
  intentionally 32-bit. Do not restore C++ `long`/`unsigned long` on Android LP64. Preserve the
  distinction between the initial paired normalizer shift and the accumulator path's per-sample
  shifted subtraction/addition, including their possible rounding-unit difference. Android
  AArch64 Clang should retain `interleave_count(1)`: final linked code must stay spill-free and use
  two `LD2`, four `SMULL`/`SMLAL`, two shifts, two vector adds, and one loop branch per eight stereo
  frames before the `ADDV` reduction. Re-run the 16/256/1024-frame differential coverage after
  changing the correlation math or compiler hints.
- Azahar's `TimeStretcher` is a pure-tempo SoundTouch client: pitch and rate remain exact unity, so
  it must enable `SETTING_BYPASS_RATE_TRANSPOSER_AT_UNITY` before any samples enter the pipeline.
  Keep the setting default-off for generic SoundTouch clients, reject explicit mid-stream changes,
  preserve it across `clear()`/`flush()`, and automatically disable it on the first non-unity
  effective rate so dynamic rate/pitch crossover behavior cannot silently change. The bypass must
  report TDStretch-only latency and tail-call `TDStretch::putSamples` without the AA FIR,
  interpolator, or RateTransposer FIFO path. Retain byte-exact 0.72/0.93/1.08 tempo coverage,
  awkward chunk boundaries, flush/clear checks, and the non-unity auto-disable assertion.
- Ask the user before making a materially different product, source-policy, or UX choice when the repository and existing requirements do not settle it. Keep moving with safe, reversible assumptions when the choice does not materially change the result.
- The active GitHub fork is `git@github.com:noeldvictor/azahar-thor-experiment.git`; keep fork-facing docs branded as Azahar Thor Experiment, not upstream Azahar.
- Public-facing docs should clearly disclose that this is a personal, AI-assisted/vibe-coded, no-support experiment with no stability guarantee.
- Android work lives under `src/android`; keep cheat-build branding and UI changes scoped there when possible.
- Performance work targets AYN Thor Base/Pro/Max: Snapdragon 8 Gen 2, Adreno 740, active cooling, LPDDR5X, and UFS 3.1 storage according to AYN's current product page. The mirrored Thor manual claims UFS 4.0, so do not use storage generation as an optimization premise without verifying the physical device. Do not tune defaults around Thor Lite / Snapdragon 865 unless the user explicitly asks.
- The primary engineering goal is higher sustained Azahar performance at lower battery power on AYN Thor. Treat average FPS, frametime distribution, battery power, temperature, thermal slope, visual correctness, and stability as joint acceptance criteria; a short FPS-only improvement is not a win.
- Deeply audit x86- and x64-originated code before assuming the ARM64 port is efficient. Check compile-time architecture branches, scalar fallbacks, host feature detection, atomics/spin loops, cache maintenance, SIMD width and lane semantics, Dynarmic A64 codegen, shader/PICA translation, Vulkan synchronization, memory copies/conversions, and thread scheduling. Compare with current RPCS3 and sibling ARM emulator lessons, but port only techniques that match 3DS guest semantics and Azahar's host architecture.
- Prefer runtime-gated AArch64/NEON hardware acceleration and fewer memory passes, barriers, wakeups, and format conversions. Do not enable global Cortex-X3/SVE flags, assume x86 memory ordering, replace PICA floating-point operations with non-equivalent host instructions, or add background worker threads without measured Thor evidence.
- Every ARM64 optimization must have an explicit correctness argument, a native `arm64-v8a` build, and a repeatable Thor A/B plan. Do not claim lower watts or higher sustained speed until the same title, scene, caches, renderer, resolution, driver, performance mode, fan mode, brightness, and display layout have been compared on device.
- For local Android builds, use JDK 17 and the Android SDK from `src/android`.
- The Android APK target for this repo is the AYN Thor, so keep `abiFilter` set to `arm64-v8a` only. Do not build x86_64 unless the user explicitly asks for it.
- When building an APK to send to the AYN Thor, use `.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite` and install `app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk`. This is release-optimized, debug-signed, uses the `-thor` version suffix, and keeps the `.debug` package so it installs over the Thor test app without the debug/JNI-debug performance hit.
- Use `:app:assembleVanillaDebug` only when an actual debuggable APK is needed.
- Before pushing Android changes, verify at least `:app:compileVanillaDebugKotlin`; prefer a full `:app:assembleVanillaRelWithDebInfoLite` when native code, packaging, or Thor installs are involved.
- Vulkan Anime4K is a real three-stage filter: copy the unscaled source to an independent image, generate the RG16F X gradient, generate the R16F Y/luma gradient, then refine into the scaled surface. Never bind the destination surface as one of its sampled inputs. Preserve explicit transfer, color-attachment, and fragment-read dependencies and compare a fixed frame against the OpenGL path on Adreno after changing this code.
- The AArch64 PICA command-list fast path may consume four pairs only after vector preflight proves every header has an in-range ordinary register ID, zero extra-data length, and no special handler. Preserve ordered scalar writes for duplicate/nonconsecutive IDs, the compact partial/special fallback, exact byte masks, command-delay counts, and dirty-bit behavior.
- The AArch64 PICA `EX2` helper keeps its eight exact float words in one aligned two-Q-register block. Preserve their lane mapping, keep `EX2` in the `needs_one` analysis set, and retain the polynomial's multiplication/addition order, NaN behavior, and input clamps when changing its paired-load lowering.
- The AArch64 PICA `LG2` positive-input helper similarly keeps its five exact coefficient words in one aligned two-Q-register block. Preserve its `SRC2`/`VSCRATCH2` lane map, Horner order, and separate unchanged NaN/zero/negative special-value vectors and branches.
- The AArch64 PICA source-swizzle planner must preserve exact four-lane selector composition. Its
  26 primitive `EXT`/`REV64`/`ZIP`/`UZP`/`TRN`/`DUP`/lane-move operations cover exactly one
  identity, 26 one-operation, and 122 two-operation selectors; the remaining 107 selectors retain
  the literal `LDR` plus `TBL` fallback. Keep the compile-time all-256 mapping proof and the
  permanent `All Source Swizzles` generated-shader test. Do not claim this affects draws that
  successfully use hardware vertex shaders; it targets immediate, geometry, and software-fallback
  shader invocations.
- The AArch64 PICA program/swizzle range updater scans eight words per first-stage NEON block and combines both comparison masks before its unchanged `UMAXV`. Preserve the all-equal `UINT32_MAX` sentinel, exact highest-changed-lane result (including low lane zero and high lane four), paired stores only after a detected change, the four-word tail, scalar remainder, dirty flags, and biggest-range accounting.
- The AArch64 ETC1/ETC1A4 block decoder maps selector and negation bit `4 * x + y` into two
  row-major eight-pixel AdvSIMD bands. Preserve horizontal `x / 2` versus flipped `y / 2`
  subblocks, table selection, signed modifiers, exact `[0,255]` saturation, column-major ETC1A4
  alpha nibble order, RGBA byte order, arbitrary signed output stride, and the unchanged scalar
  non-AArch64 path. Final ThinLTO should retain vector `USHL`, `SQXTUN`, `ZIP`, four Q stores, and
  one `TBL`/`SLI` alpha expansion for ETC1A4 rather than regressing to a 16-pixel scalar loop.
- AArch64 converted RGB5A1, RGB565, and RGBA4 texture copies deliberately process sixteen pixels
  per linear loop or two Morton rows per tile loop. Preserve exact 5/6/4/1-bit replication on
  decode, high-bit truncation on encode, bottom-up Morton row placement, padded row strides, and
  the scalar non-AArch64 path. Full-tile decode should retain `LD2`, vector shifts/masks,
  vector narrowing, `ZIP`, and ordinary paired Q stores; do not replace its output with `ST4`.
  Encode must share byte-level channel masks and packing across both eight-pixel halves. Linear
  encode uses one Q-form `LD4` per sixteen pixels, `SHLL`/`SHLL2`, and paired Q stores; do not
  split it back into two D-form `LD4` operations. Morton encode retains two D-form `LD4` loads
  because its rows are non-contiguous, but combines their components before shared preparation and
  retains `ST2` for the Morton rows. Keep the exhaustive 65,536-value round-trip and odd
  linear-length/canary coverage, and recheck final ThinLTO instead of assuming the intrinsics
  survived.
- AArch64 IA8, RG8, I8, A8, and IA4 Morton expansion must combine each two-row band with `ZIP`
  and ordinary paired Q stores; do not reintroduce D-form byte `ST4`. Native RGB8 and D24
  two-row Morton copies may retain structured `LD3` for deinterleaving, but packed output must use
  the exact two-`TBL2` row shuffle plus ordinary Q/D stores rather than D-form byte `ST3`. Preserve
  component order, bottom-up rows, padded stride, both swizzle directions, the scalar non-AArch64
  path, and the compile-time 24-byte shuffle proof. Final ThinLTO must be checked because
  Cortex-A510 documents these D-form byte stores at only `1/25` (`ST4`) and `1/17` (`ST3`).
- Converted linear RGB8 copies process sixteen pixels per AArch64 vector body. Decode must preserve
  packed BGR to RGBA order and opaque alpha while using one exact 48-byte `LD3` plus register ZIPs,
  not four-register `TBL`. Encode must preserve RGBA to packed BGR order with three overlapping
  adjacent-input `TBL2` operations whose compile-time indices stay below 32; do not widen them back
  to `TBL3`/`TBL4`. Retain exact buffer-bound alignment, the scalar tail, the non-AArch64 path, the
  37-pixel vector/tail/canary test, and final ThinLTO inspection.
- Converted D24 Morton tiles must process sixteen depths per AArch64 two-row band while preserving
  little-endian 24-bit assembly, bottom-up rows, padded strides, exact `UCVTF`/`FDIV` decode, exact
  `FMUL`/`FCVTZU` encode truncation, and the scalar non-AArch64 path. Keep D-form `LD3`, one-table
  Morton shuffles, `ZIP`/`UZP`/narrowing, and ordinary packed stores; do not introduce the
  Cortex-A510-hostile four-table `TBL`, reciprocal approximations, per-pixel scalar work, or hot-loop
  spills. Retain edge/pattern depth coverage and byte-exact canaries, and recheck final ThinLTO.
- Vulkan D24S8 staging unpack deliberately handles sixteen packed S8D24 pixels per AArch64 band.
  Load the complete 64-byte band before overwriting its in-place depth plane, preserve the trailing
  contiguous stencil plane, exact integer D24 shift, exact D32 `UCVTF`/`FDIV`, scalar tail, zero
  length, and five-bytes-per-pixel contract. Final ThinLTO should retain ordinary paired Q loads,
  four `USHR`, three `UZP1`, paired Q depth stores, and one Q stencil store; do not replace this
  with `LD4`, a table constant, approximate reciprocal math, or per-pixel scalar work. Keep the
  15/16/17 and 31/32/33 boundaries, depth edges, both modes, and canary coverage.
- Vulkan `CommandChunk::Empty()` must derive emptiness from its linked-list head: successful first record sets `first`, and `ExecuteAll()` destroys every command before clearing it. Do not add a separate stale counter. Preserve the scheduler's queue-before-execution lock order and shared-condition-variable `notify_all` behavior; they prevent worker/waiter races.
- Routine Vulkan timeline progress polling is deliberately limited to every fourth submitted tick, matching the command-buffer pool depth. Preserve immediate `Refresh()` calls for explicit waits and exhausted resource pools, monotonic cached completion, and conservative garbage-collection behavior; stale-low progress may delay reuse/deletion but must never permit unfinished GPU resources to be reused or destroyed.
- Vulkan `current_tick` and `gpu_tick` are numerical sequence/completion caches, not memory-publication primitives. Keep their loads, increment, and monotonic `AdvanceGpuTick()` compare/exchange relaxed unless new side data is explicitly published through a tick; Vulkan submission/completion and the existing queue/fence mutexes provide the required ordering. Query the timeline-semaphore driver counter once per `Refresh()` and fold it into the cache with atomic max so a CAS retry never repeats the driver call or regresses known completion.
- HLE audio intermediate mixes deliberately use `PlanarQuadFrame32` from `Source::MixInto()` through
  aux exchange and final downmix. Preserve channel-major live storage, contiguous whole-buffer aux
  copies on little-endian hosts, the endian-converting fallback, and the historical sample-major
  `QuadFrame32` save-state archive representation. Do not reintroduce `LD4`/`ST4` transposes without
  final ThinLTO inspection across the X3/A715/A710/A510 manuals; Cortex-A510 documents Q-form
  32-bit `ST4` throughput as `1/50`.
- AArch64 HLE source gain mixing deliberately handles eight stereo samples per band with ordinary
  paired Q loads plus `UZP`, widens signed 16-bit samples, converts to float, multiplies by the
  exact gain, truncates with `FCVTZS`, and adds to the four planar `s32` buses. Keep the ramp-active
  choice outside the sample loop and preserve `float(sample) * (1 / 159)` plus fused
  `start + (end - start) * progress`, the post-frame ramp state, and the scalar non-AArch64 path.
  Do not replace the source loads with structured `LD2`/`LD4`. Keep steady, ramped, disabled,
  signed-16 edge, existing-destination, and canary coverage; final ThinLTO should retain the
  eight-sample NEON loop without a per-sample ramp branch or per-iteration vector spill/reload
  traffic. After fusing all three buses, one entry/exit `d8`/`d9` callee-save pair is the measured
  trade for two removed calls.
- `Source::MixInto()` deliberately handles all three intermediate buses in one frame-level call.
  Preserve one caller invocation per source, the single disabled-source state transition, and
  silent-bus elision only when every ending gain is exact signed zero and either no ramp is active
  or every starting gain is exact signed zero. Any nonzero gain or NaN must take the arithmetic
  path, and nonzero-to-zero/zero-to-nonzero ramps must still mix. On AArch64, final ThinLTO should
  retain one Q `FCMEQ`/`UMINV` predicate per checked gain vector rather than four scalar compares.
  Keep three-bus, signed-zero, zero-to-zero ramp, nonzero-ramp, disabled-state, and canary coverage.
- An active AArch64 HLE source bus may use the front-stereo specialization only when both ending
  rear gains are exact signed zero and, during a ramp, both starting rear gains are also exact
  signed zero. Preserve the integer `AND`/`TST #0x7fffffff7fffffff` predicate: any nonzero bit
  pattern after removing the two sign bits, including a subnormal, infinity, or NaN, must use the
  full four-channel path. The front path must not load or write rear destinations; the full steady
  and ramped loops must remain the original 52 and 74 instructions per eight samples. Keep each
  nested `std::array` pointer within its own array object instead of relying on cross-subarray
  pointer arithmetic. Recheck final ThinLTO and the front/rear destination canaries after edits.
- The final HLE mixer skips a 160-sample downmix only when that bus's frame-wide mixer volume
  compares equal to exact signed zero. Preserve `current_frame` clearing, aux send/return copies,
  saved intermediate buffers, and every nonzero or NaN volume's arithmetic path. Final AArch64
  ThinLTO should retain one `FCMP`/`B.EQ` before the output-format dispatch in both `Mixers::Tick()`
  and the outlined `MixCurrentFrame()`. Active AArch64 stereo/mono downmix deliberately handles
  eight samples with Q-form `LD2`/`ST2`, `SQXTN2`, and `.8h` saturating adds; keep the exact scalar
  multiply/FMA order in each four-lane half. Final linked loops should remain 39/37 instructions
  per eight samples with no D-form structured memory operation, extra `UZP`/`ZIP`, or vector spill.
  Keep signed-zero Mono and Stereo regression coverage.
- AArch64 HLE source filters deliberately vectorize the independent left/right channels, never
  adjacent time samples: the simple and biquad recurrences must remain sequential. Keep filter
  coefficients and histories register-resident across each 160-sample frame, preserve the exact
  reset passthrough coefficients (`1 << 15` and `1 << 14`) plus their final history, and retain the
  scalar non-AArch64 path. Final ThinLTO should continue to show `SMULL`/`SMLAL`, arithmetic shift,
  and `SQXTN`; do not assume source intrinsics are useful without checking the linked library.
- HLE GC-ADPCM decoding deliberately loads one packed byte for each two recurrent samples and
  sign-extends both four-bit values without a lookup table. Preserve high-nibble-before-low-nibble
  feedback order, scale/coefficient selection, signed fixed-point arithmetic, saturation, duplicated
  stereo output, partial frames, the historical padded second sample for odd lengths, and final
  `yn1`/`yn2`. Final AArch64 ThinLTO should retain one byte load plus direct signed bitfield
  extraction per pair and no `SIGNED_NIBBLES` symbol or indexed nibble-table load. Keep the
  independent table-reference test across all nibble values, scales, histories, clipping, and
  frame boundaries.
- AArch64 HLE linear interpolation deliberately evaluates the independent stereo lanes with one
  AdvSIMD `SQDMULH`. Preserve the DSP's signed-16 saturated delta, the unsigned 24-bit phase, the
  exact Q24-to-Q31 `phase << 7` mapping, truncation rather than rounding, and the scalar
  non-AArch64 path. Do not replace it with `SQRDMULH`, float interpolation, or time-lane
  vectorization. Recheck final ThinLTO whenever this math or its deque traversal changes.
- HLE resampler traversal treats history as a virtual prefix: `V(0) = xn2`, `V(1) = xn1`, and
  `V(j) = input[j - 2]` for `j >= 2`. Keep the input index monotonic, cache the adjacent sample
  window, consume exactly that many real deque samples, and preserve `xn2`, `xn1`, `fposition`, and
  partial-output behavior across calls. Do not reinsert history into the deque or accept a helper
  call in the valid per-output ARM64 loop; final ThinLTO should reuse the cached window when the
  index is unchanged and issue one sequential sample load when it advances by one.
- Android Eco Turbo defaults on. Above 100% speed it uses a wall-clock token budget to cap host presentation/composition at 60 FPS without changing guest timing or the selected turbo limit. Do not replace this with a divisor derived from the requested speed: a scene that cannot reach that speed would be undersampled. Preserve screenshot and video-dump preparation, reset the budget at normal speed, and keep the UI clear that disabling Eco Turbo is smoother but uses more GPU work on the 120 Hz panel.
- Keep generated Android storage bounded. Check free C: space and the sizes of `src/android/app/.cxx` and `src/android/app/build` before and after large native builds. Retain only the active `arm64-v8a` release configuration cache and APKs still needed for testing; after verification, remove stale Debug, x86/x86_64, obsolete CMake configuration-hash, and Gradle intermediate trees using exact validated paths inside this repository. Do not leave tens of gigabytes of reproducible build output behind or run a broad cleanup that could touch source, manuals, saves, or unrelated user files.
- The Thor may enumerate through both USB (`c3ca0370`) and wireless ADB. Use `adb -s c3ca0370` for deterministic installs and tests when both transports are present. Strip the large native test executable into a temporary file before pushing it to `/data/local/tmp`, and remove both temporary copies immediately after the run.
- Do not commit generated Gradle, CMake, or APK output.
- Thor GPU driver UX should stay simple: keep the guided driver picker with visible per-driver download buttons, recommendation notes, recent Turnip rollback choices, manual ZIP fallback, and system-driver fallback working. The guide fetches K11MCH1 AdrenoToolsDrivers release assets at runtime and must validate `meta.json` before installing.
- E.X. Troopers (`0004000000053700`) has a custom Thor compatibility profile: Android launch caps resolution to 2x, forces JIT/HW shader/shader cache basics, disables custom texture loading, and the core hack list enables the texture-copy fallback skip for that title. Keep its recommended cheat preset at 30 FPS unless on-device testing proves 60 FPS is stable.
- Keep Android/Thor profile manifests under `src/android/app/src/main/assets/game_profiles/` in sync with any hardcoded game-specific profile logic.
- Keep first-party Markdown current when behavior changes: `README.md`, `AGENTS.md`, `AI-POLICY.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `docs/*.md`, `tools/README.md`, and Android asset READMEs. Leave vendored dependency Markdown and license files alone unless a dependency itself changes.
- Track Thor performance findings in `docs/thor-optimization-notes.md`. Thor dual-display mode intentionally pins the primary panel to the 3DS top screen and the secondary panel to the 3DS bottom screen, and the app must not recreate the old hidden virtual secondary-display render path.
- Keep Snapdragon/Adreno/ARM research used for this fork under `docs/research/` and a provenance index under `docs/hardware/`. Prefer concise project-specific summaries. Do not commit vendor, device, or console manual PDFs; keep local research copies outside the Git repository and record their public source, revision, hash, and project relevance in the provenance index.
