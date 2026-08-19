# Agent Notes

- Always work directly on the repository's existing default branch (`master` here, or `main` in repositories that use it). Do not create, switch to, or leave work on any other branch.
- Commit small, coherent, verified slices directly on `master` and push them to `origin/master` frequently. Never include unrelated user files or generated build output just to make a checkpoint.
- Use command-line Git over the repository's SSH remotes for status, fetch, commit, and push operations. Do not use GitHub workflow guides, PR automation, web publishing flows, or the GitHub CLI unless the user explicitly asks for them.
- Keep fork-specific source, patches, tests, and documentation in this repository. Do not create a separate repository or fork for a customized dependency; vendor that dependency here when a normal submodule commit would otherwise require another remote.
- `externals/soundtouch` is intentionally vendored from former submodule commit `9ef8458d8561d9471dd20e9619e3be4cfe564796` so its Thor AArch64 overlap path stays in this repository. Do not restore it to a gitlink; retain the LGPL license and omit unused prebuilt example binaries.
- `externals/cryptopp` is intentionally vendored from former submodule commit
  `8d92d788421483a43e09acf1cd4a2861cb2b8cab` so ARM feature-probe repairs stay in this repository.
  Do not restore it to a gitlink. Crypto++ `try_compile` probes include installed-style
  `<cryptopp/...>` headers and therefore must receive the vendored `include/` directory. Keep
  CRC32 and PMULL in specialized translation units with runtime `HasCRC32()` / `HasPMULL()` gates;
  never enable optional crypto ISA extensions globally. AES and SHA already use their existing
  hardware paths, so do not attribute their performance to the CRC32/PMULL probe repair.
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
- Label Thor CPU-affinity measurements from the device MIDRs, not assumed Linux numbering: CPUs
  0-2 are Cortex-A510 (`0xd46`), CPUs 3-4 are Cortex-A715 (`0xd4d`), CPUs 5-6 are Cortex-A710
  (`0xd47`), and CPU 7 is Cortex-X3 (`0xd4e`).
- The primary engineering goal is higher sustained Azahar performance at lower battery power on AYN Thor. Treat average FPS, frametime distribution, battery power, temperature, thermal slope, visual correctness, and stability as joint acceptance criteria; a short FPS-only improvement is not a win.
- Deeply audit x86- and x64-originated code before assuming the ARM64 port is efficient. Check compile-time architecture branches, scalar fallbacks, host feature detection, atomics/spin loops, cache maintenance, SIMD width and lane semantics, Dynarmic A64 codegen, shader/PICA translation, Vulkan synchronization, memory copies/conversions, and thread scheduling. Compare with current RPCS3 and sibling ARM emulator lessons, but port only techniques that match 3DS guest semantics and Azahar's host architecture.
- Prefer runtime-gated AArch64/NEON hardware acceleration and fewer memory passes, barriers, wakeups, and format conversions. Do not enable global Cortex-X3/SVE flags, assume x86 memory ordering, replace PICA floating-point operations with non-equivalent host instructions, or add background worker threads without measured Thor evidence.
- Every ARM64 optimization must have an explicit correctness argument, a native `arm64-v8a` build, and a repeatable Thor A/B plan. Do not claim lower watts or higher sustained speed until the same title, scene, caches, renderer, resolution, driver, performance mode, fan mode, brightness, and display layout have been compared on device.
- Dynarmic A32 keeps guest NZCV in reserved callee-saved `W23`. `A32SetCpsrNZCV` must load its IR
  argument directly into `X23` through `ReadIntoFixedRegister()` so a flags value becomes one
  `MRS X23, NZCV`, not `MRS Xtemp, NZCV` plus `MOV W23, Wtemp`. Fixed-register reads may target
  only registers excluded from the active allocator order; preserve use accounting, flag spilling,
  callback/state synchronization, and the linked-block arithmetic-NZCV regression test.
- Dynarmic ARM64 read/write operands may inherit the read value's physical register only for a
  non-immediate value of the same host-register class with exactly one remaining IR use, exactly
  one active lock, and no prior realization of the output. `ReplaceLastUseWith()` transfers the
  location metadata to the output, and the `RAReg` lifetime must unlock that new value without
  clearing its reused location. Preserve the allocate-and-copy fallback for every other case and
  retain the A32 VTBX read/write regression on a real ARM64 host.
- Preserve the move-free ARM64 Dynarmic lowerings built on that final-use contract:
  `Pack2x32To1x64` must reuse the low operand and insert the high word with `BFI`,
  `LeastSignificantWord` must remain a zero-code low-word alias through `DefineAsExisting()`, and
  `PackedSelect` must reuse the final-use GE mask as `BSL`'s destination. Keep the real A32 `UMLAL`
  packed-word regression and the A32 `SEL` regression over all 16 GE masks; shared values must
  continue through the allocator's conservative copy fallback.
- Preserve ARM64 signed-narrow fusion only when `LeastSignificantByte`/`LeastSignificantHalf` has
  exactly one use and the immediately following IR instruction is the matching word/long signed
  extension. In that case the narrow value aliases its source and `SXTB`/`SXTH` performs both jobs.
  Keep `UXTB`/`UXTH` for zero extensions, shared/non-adjacent values, exclusive stores, ordinary
  stores outside the separately documented exact-width gate, and every unrecognized consumer; only
  the separately documented sole-consumer shift-count and ordinary-store fusions may bypass it.
  Retain the real A32 `SXTB`, `SXTH`, `SMULBB`, and dirty-high-byte `LSL` regression so an over-broad
  alias cannot silently corrupt shift semantics.
- ARM64 Dynarmic may omit the second `AND #0xff` in no-carry A32 `LogicalShiftLeft32`,
  `LogicalShiftRight32`, and `ArithmeticShiftRight32` only when the shift argument resolves through
  identities to a materialized `LeastSignificantByte`. A byte with exactly one eventual consumer
  may instead alias its raw source only when that consumer takes it as argument 1 of 32-bit LSL,
  LSR, or ROR. For no-carry LSL/LSR, preserve `TST #0xe0` plus EQ selection: AArch64 consumes only
  bits 4:0, while bits 7:5 distinguish the A32 0..31 range from 32..255. ROR may use the raw source
  directly because both architectures rotate by the low five bits; its carry path must retain the
  low-byte zero test. Do not extend this alias to ASR: its raw-count clamp regressed on A715, so ASR
  retains `UXTB` and the established canonical path. Preserve generic U8 masks and the complete
  carry lowerings. Keep real guest coverage for dirty-upper-bit amounts 0, 1, 31, 32, 33, and 255
  across no-flags and carry-producing LSL/LSR/ASR/ROR.
- A32 scalar NEON long multiply must broadcast its selected 16-bit or 32-bit source lane with
  `VectorBroadcastElement()` before `VectorMultiplySignedWiden()` or
  `VectorMultiplyUnsignedWiden()`. Do not restore the x86-shaped
  `VectorGetElement()` plus `VectorBroadcast()` pair: on ARM64 it lowers to an element-to-GPR
  `UMOV` followed by a GPR-to-SIMD `DUP`, while the direct form is one element `DUP`. Retain real
  guest `VMULL.S16`, `VMLAL.U16`, `VMLSL.S32`, and `VMULL.U32` coverage with distinct lane indices,
  signed extremes, accumulator wrapping, and complete 64-bit results.
- A32 D-register `VZIP.8`/`VZIP.16` must keep the two halves of `VectorInterleaveLower()` in SIMD:
  write the lower half with `SetVector()` and rotate the upper half down by 64 bits for its
  `SetVector()`. Do not restore `VectorGetElement(64)` plus `SetExtendedRegister()`: the ARM64
  backend turns each half into `UMOV` to a GPR followed by `FMOV` back to a D register. Preserve
  low/high D-register encoding coverage, both legal element sizes, and the existing Q-form path.
- A32/A64 `VABDL` and `VABAL` must express the widening absolute difference with
  `VectorSignedAbsoluteDifferenceWiden()` or `VectorUnsignedAbsoluteDifferenceWiden()` before any
  accumulation. The ARM64 backend must lower those IR operations directly to `SABDL`/`UABDL` on
  the selected 64-bit source half. Do not restore the A32 `VectorGetElement(64)` ->
  `ZeroExtendToQuad()` -> `VectorZeroExtend()` chain or the A64 pair of pre-extensions; they turn a
  native one-instruction operation into cross-register-bank transfers and separate widening.
  Keep the x64 polyfill and signed/unsigned 8/16/32-bit guest coverage, including signed extremes
  and widened-lane accumulator wraparound.
- A32/A64 `VADDL`/`VADDW` and `VSUBL`/`VSUBW` must preserve their widening or wide operation in
  `VectorSignedAddSubWiden()`/`VectorUnsignedAddSubWiden()` or
  `VectorSignedAddSubWide()`/`VectorUnsignedAddSubWide()`. The ARM64 backend must lower these
  directly to `SADDL`/`UADDL`/`SSUBL`/`USUBL` or `SADDW`/`UADDW`/`SSUBW`/`USUBW` on the selected
  64-bit source half. Do not restore frontend `VectorSignExtend()`/`VectorZeroExtend()` plus generic
  `VectorAdd()`/`VectorSub()` sequences: those expand native long forms from one host instruction
  to three and wide forms to two. Keep the x64 polyfill and A32 signed/unsigned long/wide tests,
  including signed extremes and modular destination-lane wraparound.
- A32/A64 vector and by-element `VMLAL`/`VMLSL` or `SMLAL`/`UMLAL`/`SMLSL`/`UMLSL` must retain
  `VectorSignedMultiplyAccumulateWiden()`/`VectorUnsignedMultiplyAccumulateWiden()` through IR.
  The ARM64 backend must consume the accumulator with `ReadWriteQ()` and emit the matching native
  long multiply-accumulate/subtract. Do not split this back into widening multiply plus generic
  add/sub: that doubles recurring host instructions and measured 5.017x slower on Cortex-A510.
  Eight-independent-chain timing was otherwise tied within 0.6% on A710/A715/X3, so describe this
  as an exact-path instruction/efficiency win rather than an emulator-wide speedup. Preserve the
  x64 polyfill, direct SIMD lane broadcast before the fused operation, signed/unsigned 8/16/32-bit
  semantics, modular accumulator wraparound, and the A32 full-vector plus scalar-lane tests.
- A32 `SHASX`/`SHSAX`/`UHASX`/`UHSAX` mixed halving operations must keep the ARM64 backend's
  `REV32` plus native `SHADD`/`SHSUB` or `UHADD`/`UHSUB` and element-to-element lane insert. Do not
  restore the widening/sign-mask/shift/narrow sequence: that expands each recurring guest operation
  from four host instructions to nine and measured 2.316x-2.506x slower on the tested Thor
  A510/A715/A710 cores. Preserve signed floor rounding, unsigned underflow, both ASX/SAX lane
  arrangements, and the permanent A32 edge-case test. This is a hot-path result, not a whole-game
  FPS or watt claim.
- A32 `SASX`/`SSAX`/`UASX`/`USAX` mixed wrapping operations must keep the ARM64 backend's `REV32`,
  narrow `ADD`/`SUB`, and element-to-element low-lane insert. When GE is live, preserve signed GE
  through `SHADD`/`SHSUB` plus `CMGE`, unsigned addition carry through `CMHI`, and unsigned
  subtraction no-borrow through `UHSUB` plus `CMGE`; when GE is dead, retain the four-instruction
  result-only path. Do not restore the extension/extract/sign-mask/narrow sequence: it uses 10
  signed or 11 unsigned instructions instead of eight and measured 1.024x-1.334x slower across
  tested Thor A510/A715/A710/X3 cores. Do not substitute the rejected seven-instruction widening
  candidate: its final `XTN` lengthened the dependency chain and regressed tested A715/A710 cores
  by 5.4%-10.9%. Preserve both ASX/SAX layouts, signed non-negative GE, unsigned carry/no-borrow,
  NZCV/Q, and the permanent multi-edge A32 test. Keep this result path-local until a matched game
  and power A/B exists.
- A32 `QASX`/`QSAX`/`UQASX`/`UQSAX` must remain packed through
  `PackedSaturatedAddSubU16/S16` or `PackedSaturatedSubAddU16/S16`. The ARM64 backend must spill
  lazy host FPSR state before using `SQADD`/`SQSUB` or `UQADD`/`UQSUB`, exchange the second source
  with `REV32`, and insert only the alternate low lane. Do not restore scalar halfword extraction,
  extension, two generic saturation clamps, and repacking: that expands the recurring path from
  four host instructions to 21 and measured 1.11x-2.14x slower on tested A510/A715/A710 cores.
  Preserve signed/unsigned saturation, ASX/SAX lane placement, unchanged guest NZCV/Q/GE flags,
  the x64 SSE4.1/SSE2 lowering, and the permanent A32 edge-case test. Treat this as a path-local
  result until a matched game/power A/B exists.
- For local Android builds, use JDK 17 and the Android SDK from `src/android`.
- The Android APK target for this repo is the AYN Thor, so keep `abiFilter` set to `arm64-v8a` only. Do not build x86_64 unless the user explicitly asks for it.
- When building an APK to send to the AYN Thor, use `.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite` and install `app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk`. This is release-optimized, debug-signed, uses the `-thor` version suffix, and keeps the `.debug` package so it installs over the Thor test app without the debug/JNI-debug performance hit.
- Use `:app:assembleVanillaDebug` only when an actual debuggable APK is needed.
- Before pushing Android changes, verify at least `:app:compileVanillaDebugKotlin`; prefer a full `:app:assembleVanillaRelWithDebInfoLite` when native code, packaging, or Thor installs are involved.
- Vulkan Anime4K is a real three-stage filter: copy the unscaled source to an independent image, generate the RG16F X gradient, generate the R16F Y/luma gradient, then refine into the scaled surface. Never bind the destination surface as one of its sampled inputs. Preserve explicit transfer, color-attachment, and fragment-read dependencies and compare a fixed frame against the OpenGL path on Adreno after changing this code.
- Texture-filter output already persists in the owning rasterizer surface's scaled GPU image until
  guest writes invalidate or re-upload that region; do not add a disk cache for these transient GPU
  surfaces without measured Thor evidence that hashing, storage I/O, synchronization, and VRAM
  duplication are a net power win. Screen-filter Anime4K is a separate final-presentation pass and
  normally runs for each presented frame. Any Android per-game cache manager must key persistent
  data by title ID, report sizes, separate Vulkan/OpenGL shader and future preprocessed-texture
  caches from texture dumps, and never label or delete the user's custom/downloaded texture pack as
  disposable cache. The current long-press **Manage Cached Data** dialog accounts for the exact
  per-title files removed by the existing Vulkan/OpenGL shader-cache actions, performs filesystem
  size queries off the UI thread, and requires explicit confirmation before either deletion. Keep
  size accounting and deletion coverage aligned when cache paths change.
- Vulkan guest-texture and final-presentation samplers deliberately keep anisotropy disabled with
  `maxAnisotropy = 1.0f`. PICA exposes nearest/linear and mip filtering but no anisotropy control,
  and the OpenGL path does not add it. Preserve exact guest filter semantics and deterministic
  nearest presentation; do not restore device-maximum anisotropy without an explicit user setting,
  visual validation, and a matched Thor performance/power A/B. The Vulkan device feature may stay
  enabled for future controlled uses.
- Large AArch64 `Common::FindMinMax()` index scans deliberately switch at 128 bytes to four
  independent minimum and four independent maximum accumulators over each 64-byte batch. Preserve
  exact unsigned `u8`/`u16` extrema, the one-vector and scalar tails, empty-input sentinels, and the
  unchanged non-AArch64 paths. Do not lower the crossover to one batch: its extra vector setup and
  tree reduction nearly consume the first 64-byte saving. Final ThinLTO should retain two Q-form
  `LDP`, eight `UMIN`/`UMAX`, and five address/control instructions per repeated band with no
  spills. Keep prefix-reference coverage across 127/128/129 bytes and equivalent halfword counts.
- The indexed PICA CPU-fallback vertex cache is a fully associative 64-entry circular-replacement
  cache. On AArch64, scan each complete sixteen-ID band with two Q loads, two halfword compares,
  `XTN`/`XTN2` or equivalent `UZP1` narrowing, one lane-select/`ORN` equivalent, and one `UMINV`,
  then retain the scalar tail and non-AArch64 path.
  Preserve first-match behavior if duplicate IDs occur, the `[0, vertex_cache_count)` valid-prefix
  invariant before the cache fills, hit behavior that does not advance replacement state, and the
  existing circular replacement order after 64 misses. Keep exhaustive count/value differential
  coverage and inspect final ThinLTO before treating the source shape as a performance result.
- The AArch64 PICA command-list fast path may consume four pairs only after vector preflight proves every header has an in-range ordinary register ID, zero extra-data length, and no special handler. Preserve ordered scalar writes for duplicate/nonconsecutive IDs, the compact partial/special fallback, exact byte masks, command-delay counts, and dirty-bit behavior.
- The AArch64 PICA `EX2` helper keeps its eight exact float words in one aligned two-Q-register
  block. Preserve their lane mapping, keep `EX2` in the `needs_one` analysis set, and retain the
  polynomial's multiplication/addition order, NaN behavior, and input clamps when changing its
  paired-load lowering. Keep range reduction as scalar `FCVTNS`, lane-to-GPR `MOV`, then scalar
  `SCVTF`. `FRINTN` plus `FCVTZS` and direct GPR-destination `FCVTNS` were correct in focused tests
  but repeatably regressed A710 by about 2.9% and 20%, respectively.
- The AArch64 PICA `LG2` positive-input helper similarly keeps its five exact coefficient words in
  one aligned two-Q-register block. Its unbiased exponent is signed: convert the 32-bit GPR directly
  with scalar `SCVTF`, never unsigned `UCVTF` or a GPR-to-vector move first. Preserve its
  `SRC2`/`VSCRATCH2` lane map, Horner order, and special-value result vectors. Classify the scalar
  input with `FCMP input,#0.0`, then `B.VS` for NaN before `B.LE` for signed zero, negative finite
  values, and negative infinity. Do not restore the old SIMD-mask/GPR sequence or add a separate
  self-compare: the three-instruction classifier was faster on every Thor core class, while the
  two-compare alternative regressed A715 and X3. Keep NaN, both signed zeros, both infinities,
  negative inputs, and powers of two across negative and positive exponents covered on ARM64.
- Keep AArch64 PICA `RCP` on exact scalar `FDIV`: a hardware estimate plus one or two Newton steps
  measured slower on every Thor core class. `RSQ` deliberately uses one scalar `FRSQRTE`, squares
  that estimate, applies `FRSQRTS` with the original input, then performs the final `FMUL`. Do not
  replace the squared-estimate operand with a precomputed `input * estimate`; that changes the
  architecture's infinity-times-zero special handling. Preserve zero, infinity, negative, and NaN
  behavior, keep one refinement only (two were slower than exact everywhere), and retain dense
  positive-normal exponent coverage on real ARM64.
- AArch64 PICA `DP3` must retain sanitized four-lane multiplication but reduce only X/Y/Z. Form the
  X+Y pair in a scratch scalar while broadcasting Z independently, then perform one scalar `FADD`
  and final lane broadcast. Preserve x64's `(X + Y) + Z` grouping, ignore W even when it is NaN,
  and do not reassociate or fuse the operations. Do not restore the W-zero insertion followed by
  two dependent pairwise reductions; the shorter dependency graph measured 16.7-26.0% faster on
  Thor core classes. Keep interpreter/JIT W-NaN and broadcast-result coverage.
- AArch64 PICA `DP4`, `DPH`, and `DPHI` must reduce their sanitized four-lane product with two
  same-source Q-form `FADDP` instructions. The first produces `[X+Y, Z+W, X+Y, Z+W]`; the second
  computes the same ordered `(X+Y)+(Z+W)` result in every lane. Do not restore the scalar second
  `FADDP` plus `DUP`: it adds one recurring host instruction and measured 1.37x-1.57x slower in
  exact independent/dependent Thor kernels. Preserve DPH/DPHI's forced source-one W value, x64's
  arithmetic grouping, sanitized zero/infinity multiplication, swizzles, destination masks, and
  full-lane output coverage. Treat the measured gain as path-local until a matched game/power A/B.
- AArch64 PICA `MOVA` consumes only X/Y. Keep its truncating conversion on D-form `.2S` `FCVTZS`,
  then choose extraction by the destination mask. X-only and Y-only must use one signed element
  transfer (`SMOV Xd, Vn.S[lane]`), which combines the SIMD-to-GPR move and sign extension. XY must
  retain one packed low-64-bit transfer followed by `SXTW`/`ASR`: two `SMOV`s measured 26.1-97.5%
  slower on the Thor's A710/A715/X3 cores. Do not widen the conversion back to Q-form or write
  disabled address/loop registers. Preserve negative truncation, partial masks, ignored exceptional
  Z/W inputs, and initial-state behavior; keep explicit X-only, Y-only, and XY interpreter/JIT
  coverage. D-form conversion measured essentially twice the Q-form throughput on every core class,
  while partial-mask `SMOV` removed one more instruction and won or tied on all four classes.
- The AArch64 PICA `CMP` helper combines X/Y only when both lanes use the same operation. Preserve
  the ordered `FCMEQ`/`FCMGT`/`FCMGE` masks, inverted-equality implementation of `NotEqual` so NaN
  remains unordered/true, sign-bit extraction for lanes zero and one, and the unchanged scalar path
  for mixed operators. Keep all six operators covered against the interpreter on real ARM64.
- AArch64 PICA conditional flow relies on `COND0`/`COND1` remaining canonical zero/one values from
  byte loads, bit extraction, or `CSET`. Keep `Compile_EvaluateCondition()` to one flag-setting
  instruction and return the condition code that means guest-true: OR uses `CMN/NE`, `TST/EQ`, or
  `CMP/GE/LE`; AND uses `TST/NE`, `CMN/EQ`, or `CMP/GT/LT`; JustX/JustY use `CMP/EQ`. IFC and CALLC
  branch on the inverse, while BREAKC and JMPC branch on the returned condition. Do not restore
  scratch-register boolean inversion/materialization or assume every result is EQ/NE. Preserve all
  sixteen truth-table combinations and permanent IFC/CALLC/JMPC/BREAKC interpreter/JIT coverage.
- The AArch64 PICA source-swizzle planner must preserve exact four-lane selector composition. Its
  26 primitive `EXT`/`REV64`/`ZIP`/`UZP`/`TRN`/`DUP`/lane-move operations cover exactly one
  identity, 26 one-operation, and 122 two-operation selectors; the remaining 107 selectors retain
  the literal `LDR` plus `TBL` fallback. Keep the compile-time all-256 mapping proof and the
  permanent `All Source Swizzles` generated-shader test. Do not claim this affects draws that
  successfully use hardware vertex shaders; it targets immediate, geometry, and software-fallback
  shader invocations.
- AArch64 PICA `EX2`/`LG2` calls made while a guest `CALL` return is live in `X30` must preserve that
  guest link in reserved `X16` around the local math-helper `BL`, then restore `X30` and return
  architecturally through `X30`. Keep the helper target passed by reference: Oaknut attaches an
  unresolved branch writeback to that exact `Label` object before the helper is bound. Ordinary
  math calls outside guest subroutines stay as one direct `BL`; keep the established root and guest
  stack layout unchanged. The local math helpers must not grow an ABI/external call while `X16` is
  live unless the guest link is explicitly preserved. Do not return through `X16`/`X17` or compact
  the guest root frame: exact A510 measurements rejected both designs. Retain nested-CALL plus
  `EX2`/`LG2` coverage, all-core shader runs, and exact-path alternating-order measurements.
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
- Raster fill downloads deliberately materialize repeating two-, three-, and four-byte patterns
  with a phase-preserving prefix, one seed, and exponentially growing non-overlapping `memcpy`
  calls; all-equal patterns use one `memset`. Do not restore the per-pattern tiny-copy loop or its
  backup/restore writes. Preserve arbitrary start/end offsets, bytes outside the requested range,
  and the source pattern. `CanFill()` must keep its at-most-16-byte compatibility probe on the
  stack rather than allocating. Retain exhaustive phase/length and large-range canary coverage,
  and verify final ThinLTO leaves each renderer's `DownloadFillSurface()` with one `FillMemory()`
  call rather than an inlined tiny-copy loop.
- Vulkan `CommandChunk::Empty()` must derive emptiness from its linked-list head: successful first record sets `first`, and `ExecuteAll()` destroys every command before clearing it. Do not add a separate stale counter. Preserve the scheduler's queue-before-execution lock order and shared-condition-variable `notify_all` behavior; they prevent worker/waiter races.
- Routine Vulkan timeline progress polling is deliberately limited to every fourth submitted tick, matching the command-buffer pool depth. Preserve immediate `Refresh()` calls for explicit waits and exhausted resource pools, monotonic cached completion, and conservative garbage-collection behavior; stale-low progress may delay reuse/deletion but must never permit unfinished GPU resources to be reused or destroyed.
- `ResourcePool::CommitResource()` must pass the current completion snapshot explicitly into every
  search. Search both the hinted tail and wrapped prefix with cached monotonic `KnownGpuTick()`
  before calling `Refresh()`; cached progress may be stale-low, but any object it marks complete is
  already safe. Only a complete cached miss may query the driver. After `Refresh()`, both ranges
  must use the newly loaded completion value; never capture the pre-refresh value by copy in the
  search closure. Preserve first-free circular order within each snapshot and grow only after both
  refreshed ranges fail. A false miss allocates four more Vulkan command buffers or another
  64-descriptor-set batch, so retain forward, wrapped, cached-zero-refresh, refresh-count,
  allocation-count, and full `[video_core]` coverage.
- Vulkan `current_tick` and `gpu_tick` are numerical sequence/completion caches, not memory-publication primitives. Keep their loads, increment, and monotonic `AdvanceGpuTick()` compare/exchange relaxed unless new side data is explicitly published through a tick; Vulkan submission/completion and the existing queue/fence mutexes provide the required ordering. Query the timeline-semaphore driver counter once per `Refresh()` and fold it into the cache with atomic max so a CAS retry never repeats the driver call or regresses known completion.
- A Vulkan frame that skips host presentation must still submit pending emulation commands with
  `Scheduler::Flush()`, but it must not call `Finish()` or otherwise wait for GPU completion.
  Command-buffer/descriptor reuse, stream-buffer wrap, and deferred destruction already gate on
  completed timeline ticks. Keep synchronous `Finish()` at explicit CPU readbacks, render-frame
  recreation, presentation-window destruction, and renderer teardown where the host actually needs
  completed work or is about to destroy its backing resources.
- Native threaded Vulkan presentation must use `Scheduler::FlushWithCallback()` to keep the render
  submission and present-queue notification in one typed worker command and one routine dispatch.
  The worker must submit and release `submit_mutex`, enqueue the frame while holding `queue_mutex`,
  release that predicate mutex, and only then notify the presentation thread. Do not split this
  back into `Flush()` plus a separately recorded/dispatched callback. Keep the original worker
  drain for LibRetro cache ticks and the synchronous presentation fallback. Deferred
  rasterizer-cache destruction is safe only when the runtime completion tick is strictly newer
  than the sentenced resource tick; equality must retain the resource because that tick can still
  be queued or in flight. Any value read by a worker callback while the producer may begin the next
  frame, such as the presentation clear color, must be captured by value.
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
  full four-channel path. The accumulating front path must not load or write rear destinations;
  the first-definition front path must clear both rear planes once so the complete bus is defined.
  Accumulating full steady/ramped loops must remain 52/74 instructions per eight samples, with
  front loops at 32/46. Direct full loops should remain 38/60, and direct front loops 26/40 with
  no destination load or vector add. Keep each nested `std::array` pointer within its own array
  object instead of relying on cross-subarray pointer arithmetic. Recheck final ThinLTO and the
  front/rear destination canaries after edits.
- `GenerateCurrentFrame()` deliberately leaves the complete three-bus set pending instead of
  clearing all 7,680 bytes up front. Until one source is audible, use `MixIntoFirst()` to direct-
  write every bus that source routes; then clear its adjacent silent-bus runs and return every later
  source to the original `MixInto()` accumulation path. Do not carry per-bus initialization checks
  through later sources: their recurring control work can exceed the one-time direct-write saving.
  The all-silent case must remain one contiguous 7,680-byte clear. Preserve exact signed-zero/NaN
  predicates and advance each gain ramp exactly once. Keep first steady/ramped full/front,
  multi-bus, all-silent, disabled-state, existing-destination, and canary coverage. Final ThinLTO
  must keep the 1,244-byte spill-free accumulator and direct loops without destination loads/adds.
- The final HLE mixer skips a 160-sample downmix only when that bus's frame-wide mixer volume
  compares equal to exact signed zero; every nonzero or NaN volume retains the arithmetic path.
  The first audible bus, including an auxiliary bus after leading signed-zero buses, must define
  `current_frame` directly from its already-clamped contribution. Later audible buses retain the
  original per-bus clamp followed by saturating accumulation, and an all-silent frame must clear
  the complete output even after an audible prior frame. Preserve aux exchange semantics, saved
  intermediate buffers, Surround's Stereo behavior, and exact multiply/FMA/conversion order.
  Keep `MixCurrentFrame()` `CITRA_NO_INLINE`: final ThinLTO must keep the full mixer out of
  `Mixers::Tick()`, which is currently 136 bytes after native aux-return routing. The common
  first-bus AArch64 Stereo/Mono loops
  should remain 36/35 instructions per eight samples with Q-form `ST2` and no output `LD2` or
  `SQADD`; later accumulated paths deliberately retain their output `LD2` and two `SQADD` at 38/36
  instructions. Keep multiple-bus saturation, signed-zero, first-audible-aux, and silent-after-
  audible Mono/Stereo regression coverage.
- Final HLE mixing must consume main and disabled auxiliary buses directly from the current
  `Tick()` input. On native little-endian targets, enabled ARM11 auxiliary returns must also mix
  through four independent channel pointers into the shared return buffer; only the generic
  non-native-endian fallback stages and converts them in `state.intermediate_mix_buffer`.
  `AuxSend()` still writes each enabled source bus to shared memory. Retain all historical
  mixer-state archive slots for save compatibility: their native-endian contents are transiently
  irrelevant because the next tick bypasses them. Final AArch64 ThinLTO should leave `Tick()` at
  136 bytes, `AuxReturn()` as a 4-byte return, `AuxSend()` at 108 bytes, and only zero, one, or two
  2,560-byte `memcpy` calls for enabled sends. The four source pointers must load before, not
  inside, each NEON sample loop. Keep all-disabled, both-enabled, and mixed enabled/disabled
  routing and untouched-disabled-shared-output coverage.
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
- HLE PCM8/PCM16 decoding deliberately fills its `StereoBuffer16` deque through one counted,
  sequential output iterator. Preserve PCM8's exact unsigned-byte-to-high-byte mapping, native
  little-endian PCM16 loads, mono duplication, stereo ordering, zero length, and the scalar data
  representation. Do not restore per-sample `deque::operator[]`: final AArch64 ThinLTO should
  advance the destination pointer directly and check only the 4 KiB deque-block boundary, without
  reconstructing the destination from the deque start/map on every sample. Keep the 1023/1024/1025
  and multi-block regression cases.
- HLE partial embedded PCM16 updates must call the separate suffix decoder with the latched physical
  address and `current_sample_number`; do not restore full-buffer decode followed by deque prefix
  erase/move. The suffix decoder must re-read every retained frame from guest memory, not merely
  append newly extended data, so updates to unconsumed samples remain visible. Keep ordinary
  `DecodePCM16()` unchanged, preserve mono/stereo byte layout, zero/equal/end positions, and reset
  `current_sample_number` to zero before a declared-length shrink exactly as the established path
  did. PCM8 and ADPCM partial updates remain separately unimplemented and must not be enabled by
  analogy without title evidence and exact state/feedback coverage.
- AArch64 HLE linear interpolation deliberately evaluates the independent stereo lanes with one
  AdvSIMD `SQDMULH`. Preserve the DSP's signed-16 saturated delta, the unsigned 24-bit phase, the
  exact Q24-to-Q31 `phase << 7` mapping, truncation rather than rounding, and the scalar
  non-AArch64 path. Do not replace it with `SQRDMULH`, float interpolation, or time-lane
  vectorization. Recheck final ThinLTO whenever this math or its deque traversal changes.
- Exact `1.0f` HLE Linear resampling may route through the None copy loop only while `fposition` is
  Q24-aligned. Preserve both gates: a fractional starting phase still requires interpolation, and
  every non-unity rate must retain the original phase progression. The routed path must leave
  output fill, deque consumption, history, and final `fposition` byte-for-byte identical to Linear;
  final AArch64 ThinLTO should tail-branch to None before `SQDMULH` rather than duplicating a second
  copy loop inside Linear.
- HLE resampler traversal treats history as a virtual prefix: `V(0) = xn2`, `V(1) = xn1`, and
  `V(j) = input[j - 2]` for `j >= 2`. Keep the input index monotonic, cache the adjacent sample
  window, consume exactly that many real deque samples, and preserve `xn2`, `xn1`, `fposition`, and
  partial-output behavior across calls. Do not reinsert history into the deque or accept a helper
  call in the valid per-output ARM64 loop; final ThinLTO should reuse the cached window when the
  index is unchanged and issue one sequential sample load when it advances by one.
- `Source::GenerateFrame()` must not pre-clear a nonempty source's complete 640-byte output frame:
  every resampler mode overwrites the produced prefix. Preserve a full clear before the empty-entry
  dequeue/disable early returns, clear only `[frame_position, end)` after an active underrun, and do
  that tail clear before sample accounting and filtering. Keep the exact silence, enable/buffer
  state, resampler history, filter history, and saved-frame behavior. Final AArch64 ThinLTO should
  have no 640-byte `memset` on the steady full-frame path while retaining the empty and partial
  clears.
- AArch64 Y2R conversion deliberately processes eight pixels per AdvSIMD band for all five input
  formats. Preserve planar 4:2:2/4:2:0 horizontal chroma duplication, interleaved YUYV ordering,
  signed 32-bit widening products, both arithmetic-shift stages, offsets, saturation, numeric
  `0xRRGGBB00` output, 8x8 tile placement, and the scalar non-AArch64 path. Final ThinLTO must retain
  `SMULL`/`SMLAL`/`SMLSL`, `SQXTUN`/`UQXTN`, and register ZIP packing in each format path; do not
  replace the packed output with D-form byte `ST4`. Keep all-format, width/height, coefficient-edge,
  and untouched-row canary coverage. The test-only conversion entry point must remain hidden so it
  is garbage-collected from production shared libraries.
- AArch64 Y2R output packing deliberately processes sixteen intermediate `0xRRGGBB00` words per
  band for RGBA8, RGB8, RGB5A1, and RGB565. Preserve exact little-endian output byte order, alpha
  replacement, high-bit truncation, CDMA transfer-unit/gap progression, the scalar tail, and the
  unchanged non-AArch64 path. RGB8 must keep its three adjacent-input `TBL2` maps in the outlined
  helper so their constants load once per CDMA unit; final ThinLTO should retain a 12-instruction
  repeated loop with paired/ordinary Q stores. RGBA8 must OR alpha into the known-zero low byte of
  each valid intermediate word and use ordinary Q stores; RGB5A1/RGB565 must retain one Q-form
  `LD4`, byte masks, `SHLL`/`SHLL2`, and paired Q stores per sixteen pixels. Do not reintroduce
  `ST3`, `ST4`, per-pixel packing, or vectorized scalar-tail alias checks. Keep 15/16/17 and
  31/32/37 boundaries, channel/alpha edges, and output canaries; the test-only packing entry point
  must stay hidden and absent from the production shared library.
- `Rotation::None` plus linear Y2R output must write completed tile rows directly into the final
  strip. `linear_lut` is the identity, so do not restore the redundant tile-to-`tmp_tile` scatter
  followed by a second output copy. Preserve the unchanged rotated and Block8x8 paths, partial
  heights, arbitrary valid line strides, tile order, and untouched padding. Final AArch64 ThinLTO
  should keep the outlined 68-byte writer with one post-indexed Q-form `LDP`, one post-indexed
  Q-form `STP`, decrement, and branch per eight-pixel band. Retain zero/multiple-tile,
  1/2/7/8-row, padded-stride, and guard-canary coverage; its test hook must remain absent from the
  production shared library.
- Zero-gap 8-bit Y2R input deliberately borrows the contiguous guest CDMA stream until that strip's
  conversion has consumed it. Preserve exact `address += amount` and `image_size -= amount` state,
  zero-length behavior, independent direct/compact decisions for each plane, gapped-transfer
  compaction, and every 16-bit format's low-byte extraction. Do not restore an unconditional input
  staging copy. Final AArch64 ThinLTO should keep the outlined helper at 136 bytes with a
  seven-instruction direct route (`LDRH`, `CBZ`, `LDP`, `ADD`, `SUB`, `STP`, `RET`) and no copied
  data. Retain zero-gap untouched-staging tests plus gapped byte-reference and guard-canary coverage;
  the test wrapper must remain absent from the production library.
- Zero-gap `Rotation::None` plus linear Y2R output deliberately gathers each completed tile row,
  packs its final format, and writes guest memory in one pass. Preserve RGBA8 alpha replacement,
  RGB8 byte order and odd-tile tail, RGB5A1/RGB565 truncation, exact address/image-size progression,
  and the fact that every input strip is consumed before output begins. Rotated, Block8x8, and
  gapped output must retain the established staging routes. Final AArch64 ThinLTO should keep the
  RGBA8/RGB8/RGB5A1/RGB565 helpers at 188/304/208/192 bytes and their repeated bodies at 10
  instructions per 8 pixels, 11 per 16, 15 per 8, and 13 per 8 respectively. RGB8 must retain three
  adjacent-input `TBL2` operations for paired tiles plus its exact one-tile Q/D tail; 16-bit formats
  may retain one D-form byte `LD4` because horizontally adjacent tile rows are non-contiguous, but
  all formats must use ordinary guest stores and no `ST3`/`ST4`. Keep zero/odd/even tile counts,
  0/1/2/7/8-row, alpha-edge, transfer-unit, state, and guard-canary coverage; the hidden test wrapper
  must remain absent from the production library.
- The complete direct Y2R route must not allocate the dead CDMA strip buffer. Bypass it only when
  output is zero-gap `Rotation::None` plus linear and every active input is either zero-gap
  YUV422/YUV420 8-bit planar or zero-gap interleaved YUYV. Ignore inactive-plane gaps, but retain
  staging for any active gap, both 16-bit formats, rotation, Block8x8, or output gap. A null staging
  pointer may reach `PrepareInputData8()` only on its zero-gap borrowed-pointer branch. Keep the
  fallback allocation uninitialized: do not use value-initializing `make_unique<T[]>()` or add a
  `memset`. Final AArch64 ThinLTO should keep `PerformConversion()` at `0x2c18`, branch around one
  `new[]`, leave the tile allocation intact, and use `CBZ` to skip only the matching strip-buffer
  `delete[]`; staging partition addresses remain outside the strip loop. Retain all-format active-
  gap/output-condition predicate tests plus null-staging transfer/state coverage, and keep the
  hidden test hook absent from the production library.
- Android Eco Turbo defaults on. Above 100% speed it uses a wall-clock token budget to cap host presentation/composition at 60 FPS without changing guest timing or the selected turbo limit. Do not replace this with a divisor derived from the requested speed: a scene that cannot reach that speed would be undersampled. Preserve screenshot and video-dump preparation, reset the budget at normal speed, and keep the UI clear that disabling Eco Turbo is smoother but uses more GPU work on the 120 Hz panel.
- OpenGL and Vulkan presentation deliberately resolve the top-screen right eye only when an active
  main, secondary, screenshot, or frame-dump layout can sample it. Mono-left and bottom-only
  layouts must skip the per-frame right surface lookup/upload; stereo modes and explicit mono-right
  must retain it. If `RightEyeDisabler` actually blocked the just-finished eye, consume that fact
  once only when preparing a render target, and alias the current left presentation image plus
  coordinates into the right descriptor slot. A throttled/non-presented VBlank must leave the fact
  pending. Do not infer a skipped eye from the setting alone: per-title detection can disable the hack.
  Keep the fallback right texture allocated/configured for later mode changes, include additional-
  top layouts in the predicate, and retain focused layout coverage plus final AArch64 branch/codegen
  inspection.
- ARM and Thumb-2 `SMLALD`/`SMLALDX`/`SMLSLD`/`SMLSLDX` deliberately use the generic signed
  multiply-add/subtract-long IR operations. Keep ARM64 on four signed-halfword extracts followed by
  two `SMADDL`/`SMSUBL` operations, including exchange and accumulator aliasing. Do not replace this
  with an AdvSIMD `SMULL`/horizontal-reduction route: Thor measurements showed the GPR path was
  faster on every accessible cluster. Retain ARM and Thumb signed-edge, 64-bit wrap, unchanged-flag,
  and source/destination alias tests.
- ARM and Thumb-2 plain `SMLAL` plus `SMLALBB`/`SMLALBT`/`SMLALTB`/`SMLALTT` deliberately use
  the generic signed multiply-add-long IR operation. Keep plain ARM64 on one `SMADDL`; keep only the
  two required signed-halfword extracts before `SMADDL` for the halfword forms. Preserve modulo-
  64-bit accumulation, ARM `S`-bit N/Z updates, unchanged C/V/Q/GE state, Thumb behavior, and every
  source/destination accumulator alias. Retain the permanent plain/halfword signed-edge, wrap,
  flag, and alias coverage.
- ARM and Thumb-2 `UMULL`/`UMLAL` deliberately use `UnsignedMultiplyLong` in generic Dynarmic IR.
  Keep ARM64 on native `UMULL Xd, Wn, Wm`, followed by the required packed-accumulator `ADD` for
  `UMLAL`. Do not globally fuse `UMLAL` to `UMADDL`: Thor measurements improved A510 but regressed
  A715, A710, and X3. Leave `UMAAL` on its existing generic multiply/add lowering; native `UMULL`
  and reassociated/fused candidates regressed X3 or other big cores. Preserve ARM `S`-bit N/Z,
  unchanged C/V/Q/GE, modulo-64-bit arithmetic, Thumb behavior, unsigned extremes, and every
  source/destination alias in permanent tests.
- ARM and Thumb-2 `SMULL` deliberately use `SignedMultiplyLong` in generic Dynarmic IR. Keep ARM64
  on one native `SMULL Xd, Wn, Wm`; do not restore two `SXTW` operations followed by X-form `MUL`.
  The native path measured 1.600x-3.500x faster across the Thor's X3, A715, A710, and A510 core
  classes. Preserve exact signed 32x32-to-64 arithmetic, ARM `S`-bit N/Z updates, unchanged
  C/V/Q/GE, Thumb behavior, signed extremes/zero, and every source/destination alias in permanent
  tests.
- ARM and Thumb-2 `SMMUL{R}`/`SMMLA{R}`/`SMMLS{R}` must retain the signed long operations in
  generic Dynarmic IR. Keep `SMMUL` on `SignedMultiplyLong`; form the accumulator as a zero-extended
  word shifted left 32 bits and use `SignedMultiplyAddLong` or `SignedMultiplySubtractLong` for
  `SMMLA`/`SMMLS`. ARM64 must emit `SMULL`, or `LSL` plus `SMADDL`/`SMSUBL`. Do not restore the two
  `SXTW` operations, X-form `MUL`, generic add/subtract, or zero-plus-`BFI` accumulator pack: exact
  Thor sequences measured 1.586x-2.000x for `SMMUL` and 1.573x-2.130x for the fused forms across
  measured A510/A715/A710 cores. Preserve modulo-64-bit add/subtract, the unrounded high word,
  rounding from intermediate bit 31 with 32-bit wrap, unchanged NZCV/Q/GE, Thumb behavior, signed
  extremes, and source/destination aliases in permanent tests. Keep the claim path-local until a
  matched game/power A/B exists.
- ARM and Thumb-2 `SMULWB`/`SMULWT` deliberately keep the signed halfword as `U32` and use
  `SignedMultiplyLong(U32, U32)` before the 16-bit logical shift. ARM64 must emit
  `SXTH + SMULL + LSR` for the bottom form or `ASR + SMULL + LSR` for the top form; do not restore
  separate word-to-long extensions around X-form `MUL`. Exact Thor sequences measured
  1.458x-2.237x across A510/A715/A710/X3. Do not apply the same lowering to `SMLAWB`/`SMLAWT`
  without new all-core evidence: the full sticky-Q candidate improved A510 but repeated medians
  regressed A715 slightly and X3 by up to 1.01%, so those accumulate forms intentionally retain
  their established lowering. Preserve signed 32x16 multiplication, exact bits 16-47, unchanged
  NZCV/Q/GE for `SMULW`, top/bottom selection, Thumb behavior, signed extremes, and source/
  destination aliases in permanent tests. Keep the speed claim path-local until a matched game/
  power A/B exists.
- ARM64 Dynarmic may collapse `SignExtendByteToWord` or `SignExtendHalfToWord` followed by
  `SignExtendWordToLong` only when the narrow extension has a non-immediate source, exactly one
  use, and the long extension is its immediately following argument-zero consumer. The word
  extension must alias its input and the long extension must emit one direct `SXTB Xd,Wn` or
  `SXTH Xd,Wn`. Keep shared, immediate, non-adjacent, mismatched, ordinary word-only, and unrelated
  chains on the established `SXTB`/`SXTH` plus `SXTW` lowering; keep the producer and consumer
  predicates symmetrical. This removes one instruction from the current ARM/Thumb-2
  `SMLAWB`/`SMLAWT` halfword path but does not authorize the separately rejected fused multiply/
  accumulate rewrite. Preserve bottom/top forms, destination aliases with each source role,
  signed overflow, sticky CPSR.Q, NZCV/GE, unrelated GPRs, and FPSCR in permanent tests. Exact
  independent/dependent byte/halfword sequences measured 1.82x-4.34x across Thor A510/A715/A710/
  X3; keep the claim path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic may fuse `VectorSignExtend8/16/32` or `VectorZeroExtend8/16/32` with the
  immediately following matching `VectorLogicalShiftLeft16/32/64` only when the extension has one
  use and the shift consumes it as argument zero. An immediate smaller than the original narrow
  element width emits native `SSHLL`/`USHLL`; an immediate exactly equal to that width is accepted
  only for zero extension and emits native `SHLL`. The extension then aliases its narrow source.
  Shared, non-adjacent, mismatched-width, non-immediate, larger-than-width, and signed maximum-width
  forms must retain `SXTL`/`UXTL` plus `SHL`; the alias and fused-emitter predicates must remain
  symmetrical so a fallback never sees an unextended operand. Preserve signed/unsigned 8/16/32-bit
  A32 `VSHLL` coverage, architectural maximum-width `VSHLL.I8/I16/I32`, high registers, source/
  destination overlap, untouched SIMD state, and unchanged CPSR flags. Treat the measured result
  as path-local until a matched game and power A/B exists.
- ARM64 Dynarmic may fuse a sole-use, immediately adjacent `VectorLogicalShiftRight16/32/64`
  followed by matching `VectorNarrow` or `VectorUnsignedSaturatedNarrow`, or
  `VectorArithmeticShiftRight16/32/64` followed by matching
  `VectorSignedSaturatedNarrowToSigned`/`ToUnsigned`. The narrow consumer must be argument zero,
  the immediate must be 1 through half the source width, and the producer/consumer predicates must
  remain symmetrical. Emit `SHRN`, `UQSHRN`, `SQSHRN`, or `SQSHRUN` respectively; saturating forms
  must load the host FPSR so guest FPSCR.QC remains sticky. Shared, non-adjacent, mismatched,
  non-immediate, zero, or out-of-range forms retain the generic shift plus narrow path. Preserve
  all 16/32/64-bit source widths, high registers, partial/full overlap, unrelated SIMD state, CPSR,
  FPSCR state, and QC behavior in permanent tests.
- A32/A64 vector rounding shift-right narrowing must use the first-class
  `VectorRoundingNarrow`, `VectorSignedSaturatedRoundingNarrowToSigned`/`ToUnsigned`, or
  `VectorUnsignedSaturatedRoundingNarrow` IR operation. ARM64 must emit one `RSHRN`, `SQRSHRN`,
  `SQRSHRUN`, or `UQRSHRN`; saturating forms must load host FPSR so guest FPSCR.QC remains sticky.
  Do not restore the frontend's shift/broadcast/AND/equal/subtract/narrow DAG on ARM64: the exact
  fused paths measured 13.13x-14.81x on Thor A510, 2.81x-3.54x on A715, 3.23x-3.59x on A710,
  and 3.51x-3.96x on X3. x64 and RISC-V must polyfill the first-class operation back into that
  overflow-safe DAG. Preserve all four rounding instruction families, 16/32/64-bit sources,
  legal shifts, high registers, source/destination overlap, exact negative rounding, saturation,
  unrelated SIMD state, CPSR/FPSCR state, and QC behavior in permanent tests. Keep the claim
  path-local until a matched title and power A/B exists.
- A32/A64 vector `VRSHR`/`SRSHR`/`URSHR` and `VRSRA`/`SRSRA`/`URSRA` must retain the first-class
  signed/unsigned rounding shift-right or rounding shift-right-accumulate IR operations. ARM64 must
  emit one `SRSHR`/`URSHR` or `SRSRA`/`URSRA`; x64 and RISC-V must polyfill back to the established
  overflow-safe shift/broadcast/AND/equal/subtract sequence plus the optional modular add. Preserve
  8/16/32/64-bit lanes, legal immediate shifts including the element width, D/Q forms, high
  registers, source/destination overlap, exact negative rounding, modular accumulator wrap,
  unrelated SIMD state, and unchanged CPSR/FPSCR. Do not fuse plain non-rounding `VSRA` into
  `SSRA`/`USRA`: although it improved A510 and was neutral on A715/A710, the exact sequence regressed
  Thor X3 by 5.7%-22.3%. The accepted `VRSHR` path measured 9.88x-10.61x on A510, 2.50x-2.51x on
  A715, 2.71x-2.72x on A710, and 3.52x-4.77x on X3; `VRSRA` measured 5.08x-5.65x, 2.99x-3.01x,
  3.39x-3.50x, and 2.54x-3.37x respectively. Keep these claims path-local until a matched title and
  battery-power A/B exists.
- A32/A64 vector `VSLI`/`SLI` and `VSRI`/`SRI` must retain first-class shift-insert IR. ARM64 must
  emit one native `SLI` or `SRI`; x64 and RISC-V must use the exact polyfill that preserves the
  destination bits outside the insertion field. Keep each 8/16/32/64-bit lane's legal immediate
  range, D/Q forms, low/high registers, source/destination overlap, unrelated SIMD state, and
  unchanged CPSR/FPSCR under permanent tests. Do not restore ARM64's five-instruction
  shift/immediate/broadcast/bit-clear/OR expansion: the exact native path measured 6.94x-8.32x on
  Thor A510, 1.99x-2.01x on A715, 2.17x-2.21x on A710, and 2.42x-2.43x on X3. Keep these claims
  path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic `PackedAbsDiffSumU8` must retain the two-instruction `UABDL H8` plus low-`H4`
  `UADDLV` lowering used by A32 ARMv6 `USAD8`/`USADA8`. The low four widened halfwords are exactly
  the four guest byte lanes; never reduce all eight lanes or restore the old `MOVI`/`UABD`/`AND`/
  `UADDLV` mask path. Preserve ARM and Thumb encodings, maximum difference sum 1020, modular
  `USADA8` accumulator wrap, destination/source and accumulator aliases, unrelated registers, and
  unchanged NZCV/Q/GE flags in permanent tests. The exact four-to-two instruction path measured
  1.759435x on Thor A510, 2.515585x on A715, 2.505252x on A710, and 2.806593x on X3. Keep these
  claims path-local until a matched title and battery-power A/B exists.
- A32 ARMv6 `PKHBT`/`PKHTB` must retain the first-class `PackHalfwordBottom`/`PackHalfwordTop` IR.
  ARM64 must reuse the top source with `BFXIL` for bottom shift 0, reuse the bottom source with
  `BFI` for bottom shift 16, otherwise use at most `LSL` plus `BFXIL`; top shifts 1-16 use one
  `BFXIL`, while shifts 17-32 use `ASR` plus `BFXIL` with ASR #32 represented by #31. x64 and
  RISC-V must polyfill these operations back to the exact shift/two-mask/OR DAG. Preserve ARM and
  Thumb encodings, bottom shifts 0-31, top shifts 1-32, destination/source/all-source aliases,
  unrelated registers, and unchanged NZCV/Q/GE flags in permanent tests. The seven exact forms
  measured 1.196462x-3.014697x on Thor A510, 1.750912x-2.460514x on A715,
  1.344066x-2.163796x on A710, and 1.489624x-2.232581x on X3. Keep these claims path-local until a
  matched title and battery-power A/B exists.
- A32 ARM/Thumb-2 `SXTAB`/`SXTAH`/`UXTAB`/`UXTAH` with rotation zero must retain the first-class
  `SignedExtendAndAdd32`/`UnsignedExtendAndAdd32` IR. ARM64 must emit one extended-register `ADD`
  using `SXTB`, `SXTH`, `UXTB`, or `UXTH`; x64 and RISC-V must polyfill back to the established
  narrow/extend plus modular-add DAG. Preserve destination/addend/value aliases, high registers,
  unrelated registers, and unchanged NZCV/Q/GE flags in permanent ARM and Thumb tests. Do not send
  nonzero rotations through this path: the required `ROR` plus extended `ADD` was neutral on A510
  and one representative form repeated a 0.50% regression. Exact rotation-zero paths measured
  1.329924x-1.340523x on A510, 1.165637x-1.204479x on A715, and 1.126133x-1.135539x on A710.
  The X3 was parked by `core_ctl` during this run, so its optimization-guide evidence is not a
  physical Thor benchmark. Keep all speed claims path-local until a matched title and power A/B.
- A32 ARM/Thumb-2 `SSAT16`/`USAT16` must retain the first-class `PackedSignedSaturation16`/
  `PackedUnsignedSaturation16` IR and one combined overflow pseudo-result. ARM64 must share the
  two lanes' bounds, clamp with scalar `CMP`/`CSEL`, pack with `BFI`, compare the packed result to
  the input once, and call `A32OrQFlag` once. Do not substitute AdvSIMD `SQSHL`/`SQSHLU`: their
  host `FPSR.QC` side effect is not the guest ARM11 `CPSR.Q` result and can corrupt guest VFP
  `FPSCR.QC`. Signed saturation to 16 bits may alias the input with overflow false; unsigned
  saturation to zero bits must return zero and compare against the input. x64 and RISC-V must
  polyfill the first-class operation back into the exact two-lane scalar DAG. Preserve every
  signed 1-16 and unsigned 0-15 immediate, ARM and Thumb encodings, source/destination aliases,
  untouched registers/NZCV/GE, sticky initial Q, and unchanged FPSCR in permanent tests. The exact
  path measured 1.09x-1.31x on Thor A510, about 2.00x on A715, 1.93x-2.02x on A710, and
  1.50x-2.03x on X3. Keep these claims path-local until a matched title and battery-power A/B.
- A32 ARM/Thumb-2 `SXTB16` must retain the first-class `PackedSignExtendByteToHalf` IR. ARM64 must
  use `SBFX` to save the selected upper byte in a scratch register before `SXTB` writes the result,
  then use `BFI` to insert the sign-extended upper halfword. Do not reverse the first two
  operations: the final-use `ReadWriteW` allocation may alias the guest source and destination.
  x64 and RISC-V must polyfill the operation back to the established two-mask, constant, multiply,
  and OR DAG. Preserve ARM and Thumb encodings, rotations 0/8/16/24, source/destination aliases,
  untouched GPRs, unchanged NZCV/Q/GE, and unchanged FPSCR in permanent tests. The exact path
  measured 1.00x-1.33x on Thor A510, 1.54x-2.04x on A715, 1.24x-1.33x on A710, and 1.24x-1.62x on
  X3; the A510 rotation-eight independent form was effectively tied at 1.000965x. Keep these claims
  path-local until a matched title and battery-power A/B exists.
- A32 ARM/Thumb-2 `SXTAB16` may use `PackedSignExtendByteToHalf` before `PackedAddU16` only for
  rotations 8/16/24. Keep rotation zero on the established mask/mask/constant/multiply/OR DAG: the
  shorter scalar composition regressed the doubled Thor X3 independent run by 2.0%. Do not replace
  the accepted path with the tested `FMOV`/`UZP1`/`FMOV`/`SADDW`/`FMOV` fusion; despite wins on
  A510/A715/A710 and dependent X3 chains, it regressed X3 independent rotation zero by 10.6% and
  rotation eight by 2.2%. x64 and RISC-V must continue to polyfill the first-class operation back
  to the portable DAG. Preserve ARM and Thumb encodings, rotations 0/8/16/24, distinct operands,
  every two-way alias, all-way aliasing, modular halfword wrap, untouched GPRs, unchanged NZCV/Q/GE,
  and unchanged FPSCR. The accepted ROR8 path measured 1.050745x-1.065097x on A510,
  1.159406x-1.159727x on A715, 1.089628x-1.120201x on A710, and 1.067154x-1.076240x on X3. Keep
  these claims path-local until a matched title and battery-power A/B exists.
- A32 ARM/Thumb-2 `RBIT` must retain the first-class `ReverseBits32` IR operation. ARM64 must emit
  one native `RBIT`; x64 and RISC-V must polyfill it back to the exact mask/shift/OR network. Keep
  permanent ARM and Thumb coverage for distinct operands and source/destination aliases while
  proving untouched GPRs, unchanged NZCV/Q/GE, and unchanged FPSCR. The old 17-instruction ARM64
  path fell to one instruction and measured 11.306165x-17.584485x for independent chains and
  7.024895x-9.644375x for a sequential dependency chain across all four Thor CPU classes. Keep
  these claims path-local until a matched title and battery-power A/B exists.
- A32 ARM/Thumb-16/Thumb-2 `REV16` must retain the first-class `ByteReverseHalfwords32` IR
  operation. ARM64 must emit one native `REV16`; x64 and RISC-V must polyfill it back to the exact
  shift/mask/OR network. Do not restore Thumb-16's separate upper/lower-half extraction, reversal,
  extension, shift, and OR graph. Preserve all three guest encodings, distinct operands, source/
  destination aliases, untouched GPRs, unchanged NZCV/Q/GE, and unchanged FPSCR in permanent
  tests. The measured five-instruction ARM/Thumb-2 body fell to one and measured
  3.605787x-4.435562x for independent chains and 3.096017x-5.122643x for a sequential dependency
  chain across all four Thor CPU classes. Keep these claims path-local until a matched title and
  battery-power A/B exists.
- A32 ARM/Thumb-16/Thumb-2 `REVSH` must retain the first-class `ByteReverseSignedHalf32` IR
  operation. ARM64 must emit `REV; ASR #16`; x64 and RISC-V must polyfill it back to
  `LeastSignificantHalf`, `ByteReverseHalf`, and `SignExtendHalfToWord`. Do not restore the old
  ARM64 `UXTH; REV16; SXTH` sequence or substitute `REV16; SXTH`: the latter was materially slower
  on the A510 dependency chain. Preserve all three guest encodings, distinct operands, source/
  destination aliases, dirty upper-half inputs, untouched GPRs, unchanged NZCV/Q/GE, and unchanged
  FPSCR in permanent tests. The three-instruction body fell to two and measured
  1.550576x-2.621212x for independent chains and 1.499462x-2.631136x for a sequential dependency
  chain across all four Thor CPU classes. Keep these claims path-local until a matched title and
  battery-power A/B exists.
- A32 ARM/Thumb-2 `UBFX`/`SBFX` must retain the first-class `UnsignedBitFieldExtract32`/
  `SignedBitFieldExtract32` IR operations. ARM64 must emit one native `UBFX` or `SBFX` for every
  non-full-width legal field and alias the source without code for `lsb=0,width=32`; x64 and
  RISC-V must polyfill back to the exact `LSR; AND` or `LSL; ASR` graph. Preserve both guest
  encodings, boundary fields, signedness, distinct and source/destination-alias operands, untouched
  GPRs, unchanged NZCV/Q/GE, and unchanged FPSCR in permanent tests. Exact Thor measurements were
  1.5054x-2.0579x for unsigned throughput and 2.0176x-2.1327x for signed throughput. Dependency
  chains were about 2.00x on A715/A710/X3 but only 1.02x-1.03x on A510, consistent with the A510
  manual's latency table. Keep these claims path-local until a matched title and battery-power A/B
  exists.
- A32 ARM/Thumb-2 `BFI` must retain first-class `BitFieldInsert32` and
  `BitFieldInsertSelf32` IR. ARM64 must lower the distinct form with one read/write destination,
  one source read, and one native `BFI`; the self form must use one read/write operand so register
  allocation cannot insert a hidden `MOV`. Preserve the zero-code full-width replacement and
  `lsb=0` self identity. x64 and RISC-V must polyfill both operations back to the exact
  destination-mask/source-shift/source-mask/OR graph. Keep ARM and Thumb encodings, boundary
  fields, distinct and destination/source-alias operands, unrelated registers, NZCV/Q/GE, and
  FPSCR under permanent tests. Do not route `BFC` through this operation: its established ARM64
  logical-immediate clear already costs one instruction. Repeated exact Thor measurements found
  2.0028x-3.8745x throughput gains, neutral distinct dependency chains on A510/A710/X3, a 2.0011x
  distinct dependency gain on A715, and 1.5004x-3.1851x self dependency gains. Keep all claims
  path-local until a matched title and battery-power A/B exists.
- A32 ARM/Thumb-2 `MOVT` must retain the first-class `MoveTopHalf32` IR operation for nonzero
  immediates. ARM64 must use one read/write operand and emit one native `MOVK Wd,#imm,LSL#16`;
  x64 and RISC-V must polyfill it back to the exact low-half `AND` plus shifted-immediate `OR` DAG.
  Keep immediate zero on the established one-`AND #0xffff` identity-reduced path: the otherwise
  equivalent MOVK candidate repeatedly regressed independent A510 measurements by 7.1%-9.0%.
  Preserve ARM and Thumb-2 encodings, low/high destination registers, zero/boundary/dirty values,
  every unrelated GPR, NZCV/Q/GE, and FPSCR in permanent tests. Exact accepted-path measurements
  were 2.6231x/2.0075x independent/dependent on A510, 2.8808x/2.0001x on A715,
  2.9015x/1.9990x on A710, and 2.7145x/1.9993x on X3 for a representative nonzero immediate.
  Keep these claims path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic ordinary A32 byte/halfword stores may alias
  `LeastSignificantByte`/`LeastSignificantHalf` to the raw word only when that value has exactly
  one use and its consumer is matching `A32WriteMemory8`/`A32WriteMemory16`. Native `STRB`/`STRH`
  must then perform the final truncation with no preceding `UXTB`/`UXTH`. Do not extend this to
  shared or non-store U8/U16 values, exclusive writes, mismatched widths, or the endian-reversal
  path. Preserve exact low-width callback arguments and fastmem writes for dirty-upper-bit inputs,
  ARM/Thumb encodings, data/base aliases, unrelated GPRs, NZCV/Q/GE, and FPSCR in permanent tests.
  The exact store-saturated Thor benchmark was throughput-neutral, so describe this only as one
  removed host instruction and lower code-cache/front-end/integer-issue work until a matched title
  and battery-power A/B exists.
- ARM64 Dynarmic ordinary A32 signed byte/halfword loads may fold a sole immediately following
  `SignExtendByteToWord`/`SignExtendHalfToWord` consumer into native `LDRSB`/`LDRSH`. Keep the load
  and extension predicates symmetrical: shared, non-adjacent, mismatched, ordered/acquire,
  exclusive, endian-reversed, A64, and unrelated producers must retain their old lowering. Direct
  fastmem/page-table hits use the signed load; callback and fastmem/page-table fallback paths must
  still sign-extend the returned narrow value before the extension aliases it. Do not hold a
  `GetArgumentInfo()` result while falling through to the legacy extension emitter. Preserve ARM
  and Thumb encodings, destination/base aliases, callback and fastmem reads, boundary values,
  unrelated GPRs, NZCV/Q/GE, and FPSCR in permanent tests. The exact load/accumulate loop reduced
  median affected-path time by 18.9%/53.8% for byte/halfword on A510 and 1.7%/1.7% on A715, while
  A710 was neutral within 0.1%; CPU 5 and X3 affinity were parked during this run. Treat the win as
  path-local instruction/code-cache/front-end work until a matched title and battery-power A/B.
- ARM64 Dynarmic may alias a sole, immediately adjacent, non-immediate A32
  `LogicalShiftLeft32` into a following flag-free/carry-free `Add32` only for immediate shifts
  1 through 4. Emit one shifted-register `ADD Wd,Wbase,Windex,LSL #shift`. Keep flags/carry,
  shared, non-adjacent, immediate-source, variable, zero, and shifts 5 through 31 on the
  established lowering. Preserve ARM and Thumb-2 encodings, destination/base/index aliases,
  full-width wrap, unrelated GPRs, NZCV/Q/GE, and FPSCR in permanent tests. Do not widen the gate
  from instruction-count intuition: exact Thor base-dependent shifts 16/31 regressed to about
  0.50x on A715/A710/X3, while the accepted 1..4 range was independently rechecked on A510.
- ARM64 Dynarmic may apply that same symmetrical sole-use/immediately-adjacent/non-immediate gate
  to A32 `LogicalShiftRight32` and `ArithmeticShiftRight32` feeding flag-free/carry-free `Add32`
  for immediates 1 through 31. Emit one `ADD Wd,Wbase,Windex,LSR/ASR #shift`. Keep carry or flag
  pseudos, shared/non-adjacent producers, immediate sources, variable/zero/32 shifts, and unrelated
  consumers on the established ADD lowering; shifted subtraction is governed by the separate rule
  below, and the ADD LSL gate must not widen beyond 1..4. Preserve ARM and Thumb-2 encodings,
  destination/base/index aliases, signed ASR behavior, modular 32-bit wrap, unrelated GPRs,
  NZCV/Q/GE, and FPSCR. Actual-JIT trace words and
  representative shifts 1/2/3/4/8/16/31 must remain the performance gate: affected-path medians
  improved on A510/A715/A710, while X3 independent work was neutral and dependency chains won.
  Keep all claims path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic may alias a sole, immediately adjacent, non-immediate A32 `LogicalShiftLeft32`
  into flag-setting `Add32` or normal-carry-in `Sub32` only for immediate shifts 1 through 4 and
  only when the arithmetic instruction's sole pseudo-operation is `GetNZCVFromOp`. Emit one
  `ADDS`/`SUBS Wd,Wbase,Windex,LSL #shift`; this covers ARM/Thumb-2 ADDS/SUBS and the same IR used
  by CMN/CMP. The shift must have one use and no carry pseudo-result. Keep shared/non-adjacent,
  immediate-source, variable, zero, carry/overflow/other pseudo users, every flag-setting LSR/ASR,
  and flag-setting LSL 5 through 31 on the established split lowering. Preserve destination/base/
  index aliases, comparison no-write behavior, full NZCV including carry and overflow boundaries,
  Q/GE, unrelated GPRs, and FPSCR in permanent ARM and Thumb tests. Do not generalize from static
  instruction count: base-dependent right/wide-shift forms measured about 0.51x-0.53x on Thor's
  A715/A710/X3, while the accepted small-LSL range stayed above the 0.995 floor on all four core
  classes. Keep gains path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic may alias a sole, immediately adjacent, non-immediate A32 LSL/LSR/ASR producer
  into an ordinary flag-free/carry-free `Sub32` only when its immediate is 1 through 31 and the
  subtraction carry-in is the normal true value. Emit one
  `SUB Wd,Wbase,Windex,LSL/LSR/ASR #shift`. The shift producer and subtraction consumer must use
  the same eligibility helper so fallback cannot observe an unshifted alias. Keep shared,
  non-adjacent, immediate-source, variable, zero/32, flag/carry, reverse-subtract/borrow, and
  unrelated forms on the established lowering. Preserve ARM and Thumb-2 encodings, every
  destination/base/index alias, signed ASR behavior, modular 32-bit wrap, unrelated GPRs,
  NZCV/Q/GE, and FPSCR. Actual-JIT words for all three shift families and representative immediate
  boundaries plus per-core Thor measurements must remain the gate. Do not apply the SUB 1..31
  result to ADD's independently measured LSL 1..4 limit, and keep the gains path-local until a
  matched title and battery-power A/B exists.
- ARM64 Dynarmic may alias a sole, immediately adjacent, non-immediate A32 LSL/LSR/ASR/ROR
  producer into operand 1 of a flag-free/carry-free `And32`, `Eor32`, or `Or32` for immediate
  shifts 1 through 31. Emit one shifted-register `AND`/`EOR`/`ORR`. The producer and consumer must
  use the same eligibility helper so a producer can alias its raw input only when its consumer
  will encode the shift. Keep flag or carry pseudos, shared/non-adjacent producers, immediate
  sources, variable shifts, zero/32/RRX forms, shifts in another operand, and unrelated consumers
  on the established lowering. Preserve ARM and Thumb-2 encodings, every destination/source alias,
  full-width logical results, unrelated GPRs, NZCV/Q/GE, and FPSCR. Require actual-JIT words for
  all three logical families and all four shift kinds plus representative boundaries and all-core
  Thor measurements. This independent logical result does not widen ADD's measured LSL 1..4 gate;
  keep all gains path-local until a matched title and battery-power A/B exists.
- ARM64 Dynarmic may alias a sole, immediately adjacent, non-immediate A32 LSL/LSR/ASR/ROR
  producer into a no-flags/no-carry `Not32` for immediate shifts 1 through 31. Emit one native
  shifted-register `MVN`. The producer and consumer must share one eligibility helper so the
  producer aliases its raw source only when `Not32` will encode the shift. Keep flag/carry pseudos,
  shared or non-adjacent producers, immediate sources, variable shifts, zero/32/RRX forms, and
  unrelated consumers on the established lowering. Preserve ARM and Thumb-2 encodings, distinct
  and source/destination-alias operands, full-width results, unrelated GPRs, NZCV/Q/GE, and FPSCR.
  Require actual-JIT words for all four shift kinds, representative boundaries, and correctness
  runs on every Thor core class. Keep gains path-local until a matched title and battery-power A/B.
- Do not globally fold a shifted operand into ARM64 `BIC` for A32 `AndNot32`. Although the unary
  shifted-input and independent shapes can improve, repeated A510 base-dependent confirmations
  measured approximately 0.9857x and 0.9926x for representative ASR/LSL forms. Retain the split
  shift plus `BIC` lowering unless a future dependency-aware predicate proves the exact safe shape
  and wins on every intended Thor core class. Instruction count and the manuals' logical timing
  rows are candidate guidance, not sufficient acceptance evidence.
- Do not globally replace A32/A64 same-width `SABD/UABD` plus `ADD` for `VABA` with native
  `SABA/UABA`. Although independent and big-core dependency patterns can win, exact accumulator-
  chain measurements regressed to 0.6595x-0.6890x on A510 for signed/unsigned 8/16/32-bit forms.
  Keep the split lowering unless a future gate proves its dependency shape and wins on every
  intended Thor core class. The manuals' slower A510 `SABA/UABA` timing is a warning, not a
  substitute for the retained all-core benchmark evidence.
- Do not globally fuse A32 `MLA`/`MLS` into ARM64 `MADD`/`MSUB`. Exact four-chain measurements
  showed attractive independent A510 results but regressed the dependent A510 path and both
  measured patterns on A715; independent A710 and X3 patterns also regressed badly. Retain the
  split `MUL` plus `ADD`/`SUB` lowering unless a future title-gated, dependency-aware proof wins
  on every intended Thor core class.
- Keep generated Android storage bounded. Check free C: space and the sizes of `src/android/app/.cxx` and `src/android/app/build` before and after large native builds. Retain only the active `arm64-v8a` release configuration cache and APKs still needed for testing; after verification, remove stale Debug, x86/x86_64, obsolete CMake configuration-hash, and Gradle intermediate trees using exact validated paths inside this repository. Do not leave tens of gigabytes of reproducible build output behind or run a broad cleanup that could touch source, manuals, saves, or unrelated user files.
- Do not pass Gradle `--configuration-cache` for Android packaging. `app/build.gradle.kts` runs
  command-line Git during configuration, and Gradle 8.13 rejects that while storing the cache even
  after native and APK tasks succeed. Use `--no-configuration-cache`; the ordinary Gradle build
  cache and active native CMake/Ninja cache remain useful.
- The Thor may enumerate through both USB (`c3ca0370`) and wireless ADB. The user currently prefers
  wireless ADB at `192.168.1.33:5555`; use that transport for installs and tests unless they ask to
  switch back, and always pass `-s` so the same physical device is not addressed twice. Record AC,
  USB, or battery power state with performance evidence: wall-powered measurements are useful for
  sustained thermals but are not battery-discharge watt measurements. Strip a large native test
  executable into a temporary file before pushing it to `/data/local/tmp`, and remove both
  temporary copies immediately after the run.
- Do not commit generated Gradle, CMake, or APK output.
- Thor GPU driver UX should stay simple: keep the guided driver picker with visible per-driver download buttons, recommendation notes, recent Turnip rollback choices, manual ZIP fallback, and system-driver fallback working. The guide fetches K11MCH1 AdrenoToolsDrivers release assets at runtime and must validate `meta.json` before installing.
- E.X. Troopers (`0004000000053700`) has a custom Thor compatibility profile: Android launch caps resolution to 2x, forces JIT/HW shader/shader cache basics, disables custom texture loading, and the core hack list enables the texture-copy fallback skip for that title. Keep its recommended cheat preset at 30 FPS unless on-device testing proves 60 FPS is stable.
- Keep Android/Thor profile manifests under `src/android/app/src/main/assets/game_profiles/` in sync with any hardcoded game-specific profile logic.
- Keep first-party Markdown current when behavior changes: `README.md`, `AGENTS.md`, `AI-POLICY.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `docs/*.md`, `tools/README.md`, and Android asset READMEs. Leave vendored dependency Markdown and license files alone unless a dependency itself changes.
- Track Thor performance findings in `docs/thor-optimization-notes.md`. Thor dual-display mode intentionally pins the primary panel to the 3DS top screen and the secondary panel to the 3DS bottom screen, and the app must not recreate the old hidden virtual secondary-display render path.
- Keep Snapdragon/Adreno/ARM research used for this fork under `docs/research/` and a provenance index under `docs/hardware/`. Prefer concise project-specific summaries. Do not commit vendor, device, or console manual PDFs; keep local research copies outside the Git repository and record their public source, revision, hash, and project relevance in the provenance index.
