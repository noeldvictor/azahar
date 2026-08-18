# Azahar Thor Experiment

<p align="center">
  <img src="docs/media/branding/azahar-thor-experiment-banner.png" alt="Azahar Thor Experiment banner">
</p>

<p align="center">
  <img src="docs/media/branding/no-support-fork-it.svg" alt="No support. Fork it and do stuff yourself.">
</p>

This is a personal Android fork of [Azahar](https://github.com/azahar-emu/azahar) for the AYN Thor. It is tuned around my own handheld setup, bundled cheat workflow, and local testing. It is not upstream Azahar, not an official release channel, and not trying to be a general-purpose support project.

> [!WARNING]
> This fork is vibe coded with AI assistance. That is intentional and disclosed. If AI-assisted code, docs, or generated art bother you, this repo is not for you. Use upstream Azahar or another fork.

> [!CAUTION]
> Personal-use experiment. No guarantee of stability, compatibility, correctness, performance, support, or future updates. No games, keys, BIOS, firmware, or copyrighted game content are included. Use your own legally dumped content.

## What This Is

- Android-focused Azahar fork for AYN Thor Base/Pro/Max testing.
- Branded as `Azahar (Cheat Advanced)` on Android.
- Built around release-optimized Thor APKs, not desktop packages.
- Includes a source-controlled bundled cheat set and game profile notes for my own setup.
- Uses screenshots from the Thor workflow to document the target experience.

## Target Hardware

Optimization work assumes AYN Thor Base/Pro/Max hardware: Snapdragon 8 Gen 2, Adreno 740, active cooling, and LPDDR5X. AYN's product page and mirrored manual disagree about the UFS generation, so storage tuning does not assume either one until the physical device is verified. Thor Lite is a different Snapdragon 865 / Adreno 650 target and should not drive defaults unless explicitly called out.

See [Thor optimization notes](docs/thor-optimization-notes.md) for current performance hooks and candidate code paths.

## What This Is Not

- Not upstream Azahar.
- Not a supported emulator distribution.
- Not a compatibility reporting project.
- Not a place to request ROMs, keys, firmware, game files, or piracy help.
- Not a promise that any cheat, game profile, or performance tweak will work for your copy of a game.

## Support And Issues

Do not open issues expecting support for this experiment. Fork it and do stuff yourself. If something breaks, patch it and own the result. If the AI/vibe-coded nature of the fork is a problem, look elsewhere.

The upstream Azahar project has its own rules, standards, and support expectations. Do not send Thor-experiment problems to upstream.

## Where This Fork Diverges

This fork has moved away from stock Azahar in visible ways:

- Android app label changed to `Azahar (Cheat Advanced)`.
- Android launcher icon and README branding changed for this experiment.
- Bundled Android cheats live under `src/android/app/src/main/assets/cheats/`.
- Android game list marks titles with available bundled cheats more clearly.
- Turbo speed toast spam is suppressed.
- Android Eco Turbo defaults on and caps host presentation/composition to 60 FPS above 100% speed
  while emulation continues at the selected turbo limit. It can be disabled under General for
  smoother fast-forward on the Thor's 120 Hz panel.
- Android Graphics has a separate Screen Filter selector. Its opt-in Anime4K v4 Mobile mode applies
  a single-pass, screen-space DoG filter while each 3DS screen is scaled into the layout; the older
  Texture Filter choices still operate on game textures and remain separate.
- Texture-filter results are retained in each rasterizer surface's scaled GPU image and recomputed
  only when the guest invalidates or uploads that texture region. Screen-filter Anime4K remains a
  per-present pass because it filters the finished, changing 3DS screens rather than reusable game
  textures. Long-press a game and choose **Manage Cached Data** to see that title's separate Vulkan
  and OpenGL shader-cache sizes or delete either cache after an explicit confirmation. Downloaded
  custom-texture packs remain separate user content and are never presented as disposable cache.
- Missing/stale ROM entries stop before launch instead of continuing into emulation.
- Game equality was fixed to compare real fields instead of treating hash collisions as equality.
- Thor builds are Android `arm64-v8a` only unless deliberately changed.
- Crypto++ is vendored in-tree and its ARM64 configure probes can find the vendored public headers,
  restoring runtime-gated CRC32 and PMULL implementations. Only specialized objects receive the
  optional ISA flags; the rest of the Android binary remains baseline AArch64. Existing AES/SHA
  acceleration is unchanged by this repair.
- The emulated 3DS Y2R video/camera block converts eight pixels at a time on AArch64 with exact
  widening AdvSIMD fixed-point math. All five planar and interleaved YUV formats retain the scalar
  result and tile layout; non-AArch64 builds retain the scalar implementation. Isolated release
  codegen removes 77.4%-82.6% of the repeated instructions, but this is a path-local result rather
  than a whole-game FPS or battery claim.
- Y2R's outgoing RGBA8, RGB8, RGB5A1, and RGB565 conversion also packs sixteen pixels per AArch64
  band. Final ThinLTO uses ordinary contiguous stores instead of the auto-vectorizer's `ST3`/`ST4`;
  repeated packing work falls by 35.5%-88.9% depending on the format. Exact alpha, channel order,
  16-bit truncation, CDMA gaps, scalar tails, and non-AArch64 behavior remain intact.
- Unrotated linear Y2R output now streams completed tile rows directly into the final strip. The
  old identity remap wrote and reread a 256-byte temporary for every full tile; bypassing it halves
  arrangement load/store traffic and saves eight logical bytes per converted pixel. Final AArch64
  ThinLTO uses one paired Q load and one paired Q store per eight-pixel band, with exact partial-row
  and padded-stride coverage. This affects Y2R video/camera work rather than every rendered frame.
- Ordinary zero-gap 8-bit Y2R input now feeds the converter directly from contiguous guest memory
  instead of first copying every Y, U, V, or YUYV byte into a strip buffer. Gapped CDMA input keeps
  exact compaction, and 16-bit input keeps its required low-byte extraction. This removes 288,000
  logical bytes of copy traffic from a 400x240 YUV420 conversion or 384,000 bytes from YUV422/YUYV;
  the final AArch64 ThinLTO direct route is seven instructions and performs no data copy.
- Zero-gap unrotated linear Y2R output now combines the tile-row gather with final RGBA8, RGB8,
  RGB5A1, or RGB565 packing and writes guest memory directly. It bypasses both sides of the
  intermediate 32-bit strip, removing another eight logical bytes per pixel, or 768,000 bytes for
  a 400x240 conversion. Final ThinLTO keeps compact AdvSIMD loops with ordinary stores and no
  `ST3`/`ST4`; rotated, tiled, and gapped CDMA output retains the established paths.
- When those direct input and output conditions coincide, Y2R no longer allocates its now-unused
  strip buffer. This removes one `new[]`/`delete[]` pair and a 12,800-byte transient reservation at
  400-pixel width (32 KiB maximum) from each qualifying conversion. Every 16-bit, gapped, rotated,
  or tiled route retains uninitialized staging without adding a clearing pass.
- The AArch64 PICA vertex-shader JIT lowers 149 of 256 source selectors to at most two
  register-only AdvSIMD permutations. The other 107 retain exact native table lookup.
- Partial PICA destination masks use native AArch64 SIMD lane stores instead of loading,
  blending, and rewriting the entire destination vector. Full-vector stores stay native `STR Q`.
- The AArch64 PICA JIT caches the selected output-register bank pointer once per shader invocation
  and refreshes it only after geometry `EMIT`, removing repeated bank loads and address generation
  from every ordinary output write.
- Large indexed draws scan their index bounds through four independent AArch64 min/max chains and
  two paired Q loads per 64-byte band. Short draws retain the compact single-vector loop.
- Indexed draws that reach the PICA CPU fallback search its fully associative 64-entry vertex cache
  sixteen IDs per AArch64 band with paired Q loads, exact equality masks, and one `UMINV`. Full-cache
  miss search work falls 87.2% in final ThinLTO while preserving first-match and circular replacement
  behavior; exhaustive ARM64 coverage passes on the Thor.
- The hottest PICA command-list parser has an AArch64 four-pair `LD2` path that validates ordinary
  headers together, vector-updates consecutive registers, and coalesces their dirty-bit writes.
- The AArch64 PICA `EX2` and `LG2` helpers pack their exact approximation constants into aligned
  paired-Q blocks, replacing repeated scalar address/load sequences with 128-bit paired loads.
- Repeated PICA program-code and swizzle uploads scan eight words per AArch64 NEON block, sharing
  the expensive unchanged-data reduction and loop bookkeeping across two vectors.
- ETC1 and ETC1A4 texture uploads decode each 4x4 block as two eight-pixel AArch64 AdvSIMD bands.
  Native variable shifts gather the column-major selector bits, saturating narrows clamp RGB, one
  table lookup expands ETC1A4 alpha, and four Q stores write the completed block.
- Converted RGB5A1, RGB565, and RGBA4 texture copies now process sixteen pixels per AArch64 loop,
  including full Morton tiles and linear surfaces, instead of converting one packed pixel at a
  time. Decode uses ordinary RGBA stores rather than the Cortex-A510-hostile `ST4` form. Encode
  shares channel masks across both halves, and linear input uses one Q-form `LD4` per block.
- Converted linear RGB8 copies remove every four-register table lookup: decode uses one `LD3` plus
  register ZIPs per sixteen pixels, while encode narrows each lookup to two adjacent input vectors.
- IA8, RG8, I8, A8, and IA4 texture expansion now uses `ZIP` plus ordinary paired Q stores rather
  than D-form `ST4`. Native packed RGB8/D24 Morton output similarly replaces D-form `ST3` with
  exact two-table shuffles and ordinary stores, avoiding the A510's slow structured-store paths.
- Converted D24 Morton tiles now expand to and pack from D32 float sixteen depths per AArch64
  two-row band with exact vector divide/conversion, avoiding the former per-pixel scalar loop and
  the Cortex-A510's exceptionally slow four-table shuffle form.
- Vulkan D24S8 uploads split sixteen packed pixels per AArch64 loop with ordinary paired loads,
  shifts, `UZP`, and contiguous depth/stencil stores instead of a scalar loop per pixel. The exact
  D32-float fallback is vectorized too, without reciprocal approximations.
- Recycled Vulkan command chunks derive empty state from their actual linked-list head, avoiding
  an empty dispatch and worker wake after the old never-reset command counter became stale.
- Routine Vulkan timeline-counter polling runs once per four submissions; explicit waits and
  resource-pool pressure still refresh immediately, removing up to 75% of scheduled driver polls.
- Vulkan sequence/completion counters now use numerical-only relaxed ARM64 atomics and a monotonic
  atomic max; a refresh queries the driver once even if its cache update races.
- OpenGL and Vulkan presentation skip the top-right framebuffer resolve when every active layout
  uses mono-left or bottom-only output. When the right-eye rendering hack actually skips an eye,
  presentation duplicates the current left image instead of sampling the stale right buffer. This
  removes one right-eye surface lookup/resolve per qualifying presented frame; it is a renderer-work
  reduction, not a measured whole-game FPS or battery-watt result.
- Integer SoundTouch stereo overlap uses exact AArch64 NEON widening multiply-accumulate and
  power-of-two shifts, processing four frames per vector loop without the old per-channel scalar
  divides. SoundTouch is vendored here so this ARM64 path does not depend on a separate fork.
- HLE GC-ADPCM decode loads each compressed byte once for its two recurrent samples and uses
  native signed bitfield extraction instead of two indexed nibble-table loads.
- PCM8/PCM16 source decoding advances one sequential deque iterator instead of rebuilding and
  loading the destination block-map entry for every decoded sample.
- Exact aligned 1x HLE linear resampling routes through the existing sample-copy loop, avoiding the
  otherwise redundant NEON interpolation and packing sequence for every output sample.
- SoundTouch WSOLA correlation now keeps its designed 32-bit accumulator and normalizer on
  Android's LP64 ABI. A spill-free AArch64 NEON loop cuts the repeatedly used correlation body by
  20% versus the prior linked code while preserving the rolling-normalizer arithmetic.
- Azahar's pure-tempo SoundTouch stream bypasses the inactive unity-rate resampler, removing a
  needless 64-tap anti-alias FIR, scalar interpolator, and intermediate FIFO traffic from every
  time-stretched audio block. Generic SoundTouch rate/pitch crossover behavior remains opt-in-safe.
- The HLE DSP keeps its temporary quadraphonic mixes channel-planar, eliminating structured
  `LD4`/`ST4` transposes from source accumulation and final downmix and turning enabled auxiliary
  bus exchange into contiguous copies.
- HLE simple and biquad source filters keep their coefficients and feedback history in NEON
  registers for a full audio frame and process the independent left/right channels together,
  without incorrectly vectorizing the sample-to-sample recurrence.
- The APK target for Thor is `:app:assembleVanillaRelWithDebInfoLite`, a release-optimized/debug-signed build using the `-thor` version suffix and the `.debug` package slot.
- Thor dual-display emulation is fixed to top screen on the primary panel and bottom screen on the secondary panel; the old hidden virtual secondary display fallback is removed.
- The Thor GPU Driver Manager has a guided driver picker with visible download buttons, notes, recommended generic Turnip first, recent Turnip rollback builds, Qualcomm and Turnip variants as troubleshooting choices, manual ZIP install, and system-driver fallback.
- Thor game profile manifests live under `src/android/app/src/main/assets/game_profiles/`.
- E.X. Troopers has a Thor-specific compatibility profile and native title hack notes for smoother testing.
- Thor Base/Pro/Max optimization notes are tracked in `docs/thor-optimization-notes.md`.
- Cheat gap tracking is documented in `docs/thor-cheat-gaps.md`.

## ARM64 Dynarmic Updates

The fork vendors Dynarmic in-tree so its Snapdragon/AArch64 work can be reviewed, built, and
bisected with Azahar instead of depending on a moving external checkout. Current ARM11 JIT changes
include:

- an ARM64 FastDispatch path with a measured **1.69x-1.95x isolated dispatch-throughput gain** on
  the Thor; this is a microbenchmark result, not a whole-game FPS claim;
- absolute-offset page-table entries that remove one address-add instruction from ordinary mapped
  guest loads and stores;
- a callee-saved A32 NZCV register cache that removes repeated guest-flag state loads/stores across
  linked blocks while preserving callback-visible CPSR behavior;
- direct capture of arithmetic NZCV into that reserved register, eliminating the temporary GPR and
  `MOV` from the recurring flag-transfer sequence. The exact sequence measured **2.3%-20.1%
  faster** across the Thor's four CPU core classes;
- final-use read/write coalescing that lets eligible vector operations update their existing host
  SIMD register instead of allocating another register and copying 128 bits first. Corrected
  independent-chain FMLA/BIC microbenchmarks measured **1.86x-3.50x isolated throughput**, or
  **46.1%-71.5% less time** in those exact recurring sequences;
- follow-on packing and select move elimination: final-use `Pack2x32To1x64` writes its upper word
  directly with `BFI`, `LeastSignificantWord` aliases the source's low 32 bits, and `PackedSelect`
  consumes its final-use GE mask in place with `BSL`. Disassembly-checked four-chain Thor
  microbenchmarks measured **1.05x-2.51x** for those exact sequences, with the largest improvement
  on the Cortex-A510 efficiency core;
- signed narrow fusion that recognizes an immediately adjacent, single-use byte/halfword sign
  extension and emits only `SXTB`/`SXTH`, instead of first canonicalizing with `UXTB`/`UXTH`.
  Exact-sequence Thor measurements were **1.67x-4.50x faster** on A510/A710/A715; register shifts,
  zero extensions, stores, shared values, and non-adjacent uses retain the original narrowing;
- register-shift byte-mask elision for ordinary no-flags A32 `LSL`, `LSR`, and `ASR`. When the
  shift amount is already the result of `LeastSignificantByte`, its required `UXTB` is reused
  directly instead of emitting a second `AND #0xff`. Generic U8 producers and carry-producing
  shifts keep the conservative mask. Exact-sequence Thor measurements were **1.19x-1.42x for
  LSL** and **1.24x-1.29x for ASR** across the measured A510/A710/A715 cores;
- sole-consumer register-shift fusion for A32 `LSL`, `LSR`, and `ROR`. The byte extraction now
  aliases the raw count, while AArch64's low-five-bit variable shifts plus `TST #0xe0` preserve
  the complete A32 byte-count rules. This removes one more host instruction from LSL/LSR and the
  complete `UXTB` from ROR. Exact-sequence Thor measurements were **1.21x-2.41x for LSL/LSR** and
  **1.30x-4.53x for ROR** on measured A510/A710/A715 cores. An ASR variant was rejected after a
  repeatable A715 regression, so ASR keeps the prior canonical path;
- scalar NEON `VMULL`/`VMLAL`/`VMLSL` lowering that broadcasts the selected source lane directly
  inside SIMD. It replaces an element-to-GPR `UMOV` followed by a GPR-to-SIMD `DUP` with one
  element `DUP`. Disassembly-checked 16/32-bit broadcast measurements were **6.00x faster on the
  Cortex-A510** and **2.00x on both measured Cortex-A715 cores** for this exact preparation
  sequence;
- D-register `VZIP.8`/`VZIP.16` lowering that keeps both interleaved results in SIMD. The ARM64
  result path falls from `ZIP1 + 2x UMOV + 2x FMOV` to `ZIP1 + EXT`, avoiding four cross-register-
  bank transfers. A checksum-locked exact-sequence benchmark measured **1.66x on Cortex-A510** and
  **1.45x-1.46x on the two usable Cortex-A715 cores**;
- native widening absolute difference for guest `VABDL`/`VABAL`. A32 previously crossed from SIMD
  to GPR and back twice, widened both inputs separately, then subtracted: seven ARM64 instructions.
  The new widening-difference IR lowers directly to one baseline `SABDL`/`UABDL`. A checksum-locked
  exact-sequence benchmark measured **15.98x on Cortex-A510**, **5.72x-5.78x on Cortex-A715**, and
  **8.17x on the usable Cortex-A710 core** for that preparation;
- native widening/wide add and subtract for guest `VADDL`/`VADDW` and `VSUBL`/`VSUBW`. The ARM64
  host path now emits one `SADDL`/`UADDL`/`SSUBL`/`USUBL` instead of two extensions plus add/sub,
  or one `SADDW`/`UADDW`/`SSUBW`/`USUBW` instead of extension plus add/sub. Disassembly-checked
  exact-path measurements were **4.00x-4.51x for long forms** and **2.00x-4.01x for wide forms**
  across the measured Cortex-A510/A715/A710 cores;
- native widening multiply-accumulate/subtract for guest vector and scalar-by-lane
  `VMLAL`/`VMLSL`. The ARM64 host now emits one `SMLAL`/`UMLAL`/`SMLSL`/`UMLSL` instead of a long
  multiply followed by add/sub. An eight-chain disassembly-checked exact-path benchmark measured
  **5.017x on Cortex-A510** and tied within **0.6%** on the measured Cortex-A710/A715/X3 cores,
  while halving the recurring instruction count;
- native mixed wrapping add/subtract and GE generation for ARM11 `SASX`/`SSAX`/`UASX`/`USAX`.
  ARM64 now exchanges halfwords with `REV32`, computes the wrapped result directly with narrow
  `ADD`/`SUB`, and derives exact signed/unsigned GE bits with native halving arithmetic and compares.
  The recurring path falls from 10 signed or 11 unsigned instructions to eight when GE is live,
  and to four when it is dead. The signed dependency-chain benchmark measured **1.024x-1.334x**
  across the Thor's tested A510/A715/A710/X3 cores;
- native mixed halving add/subtract for ARM11 `SHASX`/`SHSAX`/`UHASX`/`UHSAX`. ARM64 now emits
  `REV32`, native halving add/subtract, and one lane insert instead of widening both operands,
  synthesizing signs, shifting, and narrowing. The recurring path falls from nine to four host
  instructions and measured **2.316x-2.506x faster** across the tested A510/A715/A710 cores;
- native mixed saturated add/subtract for ARM11 `QASX`/`QSAX`/`UQASX`/`UQSAX`. One packed IR
  operation now lowers to `REV32`, native `SQADD`/`SQSUB` or `UQADD`/`UQSUB`, and one lane insert
  instead of scalar extraction, extension, two clamp sequences, and repacking. The recurring host
  path falls from 21 to four instructions and measured **1.11x on A510**, **2.12x-2.14x on A715**,
  and **1.81x on A710** in the dependency-chain benchmark;
- native signed high-word multiply for ARM and Thumb-2 `SMMUL{R}`, `SMMLA{R}`, and `SMMLS{R}`.
  ARM64 now uses `SMULL`, or `LSL` plus fused `SMADDL`/`SMSUBL`, instead of two sign extensions,
  X-form `MUL`, generic add/subtract, and zero-plus-`BFI` accumulator packing. Checksum-locked
  exact-sequence measurements were **1.586x-2.000x for SMMUL** and **1.573x-2.130x for the fused
  accumulate/subtract forms** across the measured Cortex-A510/A715/A710 cores;
- native signed word-by-halfword multiply for ARM and Thumb-2 `SMULWB`/`SMULWT`. The recurring
  ARM64 path now uses `SXTH` or `ASR`, one `SMULL`, and `LSR` instead of separately extending both
  operands around X-form `MUL`. Checksum-locked exact-sequence measurements were
  **1.458x-2.237x** across every Snapdragon 8 Gen 2 core class. The similar `SMLAWB`/`SMLAWT`
  candidate was rejected because its complete sticky-Q path regressed A715 and X3 slightly; and
- direct packed-flag condition tests plus cycle-count flag reuse, removing the redundant compare at
  normal linked-block exits. A common simple conditional linked-block path falls from five ARM64
  control/cycle instructions to three.

These changes target CPU-bound emulation and sustainable performance. They do not stack as simple
percentages, and no whole-game wattage claim is made without a matched device A/B. Exact emitted
sequences, build evidence, limitations, and the required benchmark controls are recorded in the
[Thor optimization notes](docs/thor-optimization-notes.md).

## AArch64 PICA Updates

Every indexed draw must find the minimum and maximum `u8` or `u16` index before vertex analysis.
For scans of at least 128 bytes, the AArch64 path now keeps four independent unsigned-minimum and
four independent unsigned-maximum accumulators across each 64-byte band. Final ThinLTO lowers the
band to two Q-form `LDP`, eight `UMIN`/`UMAX`, and five address/control instructions with no spills:
15 repeated instructions instead of four passes through the old seven-instruction 16-byte loop,
or 46.4% fewer instructions per 64 bytes. The four chains also break the two- to three-cycle
min/max dependency carried by the old loop across every vector. Scans below 128 bytes keep the
smaller loop so setup and tree-reduction work do not overwhelm the saving. Every prefix through
both crossover boundaries has reference coverage; these are linked-code reductions, not a
whole-game FPS or battery-watt measurement.

Indexed draws that fall back to CPU-side PICA vertex processing now search their fully associative
64-entry vertex cache sixteen `u16` IDs at a time on AArch64. Final ThinLTO uses one Q-form `LDP`,
two `CMEQ`, `UZP1`, `ORN`, and one `UMINV` per band with no spill. A full valid-cache miss falls
from about 579 executed lookup instructions to 74, an 87.2% path-local reduction; the containing
function grows by 92 bytes. Every valid-prefix length and every 16-bit lookup value, plus duplicate
first-match behavior, passes the focused test on the Thor. Hardware-accelerated vertex draws bypass
this fallback, so this is not a whole-game FPS or wattage claim.

The PICA vertex-shader JIT now attacks several common AArch64 lowering costs. Source swizzles use
register-only AdvSIMD permutations where possible, `ST1` lane stores handle partial destination
masks without reading untouched lanes, and a cached output-bank pointer removes repeated bank
loads and address generation. Its `EX2` approximation also packs eight exact constants into two Q registers:
constant setup falls from eight `ADR` plus eight scalar `LDR` instructions to one `ADR`, one `LDP`,
and one lane `DUP`. That is 13 fewer instructions inside each helper execution, or a net 12 for an
otherwise minimal one-`EX2` shader after its required one-time `1.0` register initialization. These
are exact generated-instruction and memory-traffic reductions validated by ARM64 compilation and
focused regression sources. Whole-game FPS and battery-watt effects still require a controlled
Thor A/B and are not estimated from static counts.

When both PICA `CMP` condition lanes request the same operation, the JIT now compares X and Y with
one AdvSIMD `FCMEQ`, `FCMGT`, or `FCMGE` and extracts both result bits from the mask. Five ordered
operators fall from six generated instructions to four; `NotEqual` uses a fifth mask inversion so
unordered/NaN inputs remain true. Mixed operations keep the scalar path. Both the interpreter and
JIT versions of the focused PICA state test pass on the real Thor; this is a shader-path instruction
reduction, not a measured whole-game speed or watt claim.

The source-swizzle planner exhaustively composes `EXT`, `REV64`, `ZIP`, `UZP`, `TRN`, `DUP`, and
lane moves. One identity, 26 one-operation, and 122 two-operation selectors avoid the old 16-byte
index literal; the remaining 107 selectors still use `LDR` plus `TBL`. Relative to the prior
emitter, 10 selector values also fall from two generated instructions to one and 122 replace the
literal load with a second register permutation. Compile-time assertions prove every accepted plan
against all 256 selector maps, and the permanent shader test executes all 256 generated results.
This helps immediate-mode, geometry-shader, and software-fallback vertex processing; draws that
successfully use hardware vertex shaders bypass this CPU JIT.

The normal positive-input `LG2` path uses the same paired-load strategy for its five exact
polynomial coefficients. Two separately addressed groups that required five setup instructions now
use one `ADR` plus one Q-form `LDP`, removing three instructions from every positive `LG2` helper
execution. NaN, zero, negative, and infinity paths retain their existing branches and literal
vectors. The original ARM64 port also treated the signed unbiased exponent as unsigned, so `0.5`
could become `4294967296.0` instead of `-1.0`. It now uses one direct scalar `SCVTF` from the GPR,
matching x64 signed conversion while removing the old GPR-to-vector move. The complete ARM64 shader
suite passes all 2,276 assertions on the Thor.

The AArch64 PICA `RSQ` helper now follows the x64 backend's approximate reciprocal-square-root
contract with one scalar hardware estimate and one Newton refinement instead of exact `FSQRT` plus
`FDIV`. A pinned Thor microbenchmark measured the isolated operation 16.2% to 43.2% faster across
the little, middle, and prime core classes. One million positive-normal samples had maximum relative
error `1.613e-5`, while zero, infinity, negative, and NaN handling matched the prior helper; the full
on-device shader suite passes all 18,278 assertions. `RCP` deliberately remains exact because its
estimate-and-refine candidate was slower on every tested core. These are path-local measurements,
not a whole-game FPS or battery-watt result.

Two more PICA JIT operations now use lower-cost AArch64 forms. `MOVA` converts only the X/Y lanes
that the instruction can consume, replacing Q-form `FCVTZS` with D-form; a 67.1-million-operation
Thor benchmark measured essentially **2x throughput** on every core class. Partial X-only or Y-only
forms now transfer and sign-extend the selected 32-bit lane with one `SMOV`, removing another
generated instruction. That route was 63.2% faster on A510, 20.0% faster on A715, and effectively
tied on A710/X3 in an interleaved isolated test. XY keeps one packed transfer because two `SMOV`s
were 26.1% to 97.5% slower on the larger cores. `DP3` now forms X+Y and broadcasts Z independently
before the final scalar add, removing the GPR-to-vector zero insertion from its dependency chain
while preserving sanitized multiplication, x64's `(X + Y) + Z` grouping, and ignored W. Its
isolated 33.6-million-operation benchmark was 16.7% to 26.0% faster. The complete ARM64 shader suite
passes all 18,304 assertions; whole-game and watt effects remain unmeasured.

PICA conditional control flow now evaluates every OR/AND/reference truth-table shape with exactly
one AArch64 flag-setting instruction and returns the matching host condition code to IFC, CALLC,
JMPC, and BREAKC. The old lowering materialized inverted booleans and needed two to four
instructions for OR or one to three for AND. Disassembly-checked Thor tests measured the isolated
condition evaluator 29.7% to 63.6% faster across A510, A710, A715, and X3 cases. The permanent
truth-table and control-flow coverage plus the complete on-device shader suite pass all 18,316
assertions. This removes PICA CPU shader-JIT work; it does not imply the same whole-game FPS gain or
a measured battery-watt reduction.

The command-list parser now deinterleaves four ordinary `[value, header]` pairs with one AArch64
`LD2`. Consecutive register IDs use one vector load/blend/store and one dirty-word update;
nonconsecutive or duplicate IDs retain ordered writes, while special, extended, invalid, and short
batches retain their scalar behavior. Final ThinLTO code shrinks the hot function by 9.5% despite
adding the fast path.

Program-code and swizzle range updates now compare eight words per first-stage AArch64 NEON loop.
The two vector masks share one unchanged-data `UMAXV`; changed data uses paired stores and still
returns the exact highest changed lane for dirty-hash and biggest-range tracking. In the final
ThinLTO binary, an eight-word unchanged block falls from 42 to 25 executed loop instructions. The
existing four-word NEON tail and scalar remainder preserve short and odd-length behavior.

ETC1 and ETC1A4 block uploads now replace the remaining 16-iteration AArch64 pixel loop with two
eight-pixel AdvSIMD bands. Final ThinLTO uses `USHL` to gather selectors and sign bits, `SQXTUN` to
perform the old signed add plus `[0,255]` clamp, a single `TBL`/`SLI` pair for ETC1A4 alpha, and
four 16-byte stores for the whole 4x4 RGBA block. The scalar non-AArch64 decoder is unchanged, and
focused coverage preserves flip/differential modes, selector/sign extremes, alpha nibble order,
padding, and positive or negative output stride. This is a texture-upload CPU-work reduction; its
whole-game speed and battery effect still needs a controlled Thor A/B.

Converted RGB5A1, RGB565, and RGBA4 surfaces now have exact AdvSIMD decode and encode paths for
both 8x8 Morton tiles and linear copies. Each loop converts sixteen pixels while preserving the
formats' bit-replication and high-bit-truncation rules. Full-tile decode deinterleaves Morton rows
with `LD2`, narrows and interleaves RGBA with vector operations, and finishes with ordinary paired
Q stores; this intentionally avoids `ST4`, whose Q-form byte/halfword throughput is especially poor
in the Cortex-A510 guide. Encode now masks and packs both eight-pixel halves together. Linear input
uses one 64-byte Q-form `LD4` instead of two D-form loads; Morton keeps two D-form loads for its
non-contiguous rows but shares their channel preparation before `ST2` restores the tile layout.
Exhaustive coverage checks every possible packed 16-bit value for all three formats, and odd-length
linear tests protect the scalar tail and buffer canaries. This is a verified format-conversion
CPU-work reduction, not yet a whole-game FPS or wattage result.

Converted linear RGB8 traffic also uses a Thor-specific AdvSIMD shape. Decode deinterleaves the
complete 48-byte BGR block with one Q-form `LD3`, then emits sixteen RGBA pixels with register ZIPs
and opaque alpha instead of four `TBL4` operations. Encode keeps its four ordinary Q loads and
three ordinary Q stores, but proves that each output block touches only two adjacent inputs and
uses three `TBL2` operations. The encode loop therefore retains its instruction count while
removing the most expensive table width on Cortex-A510. The permanent 37-pixel case covers two
vector iterations, the scalar tail, both directions, component order, opaque alpha, and canaries.

The remaining D-form structured stores in the AArch64 Morton codec are also removed. IA8, RG8,
I8, A8, and IA4 expansion combines two rows at a time with `ZIP1`/`ZIP2` and paired Q stores.
Native RGB8 and D24 retain efficient `LD3` deinterleaving, but two exact `TBL2` permutations pack
each 24-byte row for ordinary Q/D stores. This matters most on the efficiency cluster: the
Cortex-A510 guide lists D-form byte `ST3` and `ST4` at only `1/17` and `1/25` throughput,
respectively, versus `1/cycle` for a one-register `ST1`. Existing full-tile tests preserve both
directions, component order, bottom-up row placement, and padded strides; final ThinLTO confirms
the target symbols contain no `ST3` or `ST4`. Whole-game and battery effects still require a
controlled Thor A/B.

Converted D24 Morton tiles now use a separate exact AArch64 path for Vulkan's D32-float staging.
Each two-row band de-interleaves two packed eight-pixel chunks with D-form `LD3`, uses three
single-table Morton permutations plus ZIP/UZP/narrow operations, and performs four-lane
`UCVTF`/`FDIV` decode or `FMUL`/`FCVTZU` encode. The Cortex-A510 guide's one-per-nine-cycle
four-table `TBL` form was deliberately rejected. In final ThinLTO, decode falls from 816 scalar
inner-loop instructions per tile to 148 vector instructions (81.9% fewer), while encode falls from
744 to 228 (69.4% fewer), without hot-loop spills or approximate reciprocal math. Exact depth-edge,
pattern, Morton-order, stride-padding, and encode-truncation coverage compiles into the ARM64 test
binary. These are path-local CPU/code-generation reductions, not measured whole-game FPS or watts.

Vulkan D24S8 staging deinterleave now processes sixteen packed S8D24 pixels per AArch64 band. The
primary D24 path uses two ordinary paired Q loads, four `USHR`, three `UZP1`, two paired Q depth
stores, and one Q stencil store in the final ThinLTO loop. Its core loop falls from ten scalar
instructions per pixel to seventeen vector/control instructions per sixteen pixels. The exact
D32-float fallback performs four vector `UCVTF`/`FDIV` operations per band instead of sixteen
scalar divisions. Boundary, depth-edge, layout, mode, and canary tests compile into the ARM64 test
binary. These are path-local generated-code improvements; Thor FPS and battery effects remain to
be measured under controlled conditions.

Raster fill-surface downloads no longer call `memcpy` once for every two, three, or four bytes.
They preserve the pattern phase at an arbitrary requested start, seed one complete pattern, then
double the initialized range with non-overlapping bulk copies; solid-byte patterns go directly to
`memset`. An aligned 1 MiB four-byte fill therefore needs about 19 copy operations rather than
262,144 tiny iterations. `CanFill()` also checks its at-most-16-byte compatibility pattern on the
stack instead of allocating a vector. Permanent phase, length, large-range, and guard-canary
coverage compiles into the ARM64 test binary. A seven-round order-alternated x64 mechanism
benchmark reduced a 1 MiB three-byte fill from 830.77 to 29.05 microseconds (28.6x) and a solid
four-byte fill from 706.63 to 19.57 microseconds (36.1x). These isolated host figures prove removal
of loop/call overhead; they are not whole-game Thor FPS or battery-watt results.

## AArch64 Audio Updates

GC-ADPCM source buffers previously decoded every four-bit sample through a sixteen-entry integer
table and read each packed byte twice. The decoder now retains one packed byte across its high- and
low-nibble recurrence and expresses signed four-bit expansion directly. Final AArch64 ThinLTO uses
one packed-byte load and native signed bitfield operations for each two-sample body, with no nibble
table load or constant. The repeated body falls from 50 to 46 instructions, removing 28 executed
instructions and 21 data loads per complete 14-sample frame; the function shrinks from 500 to 476
bytes and its 64-byte table disappears. Permanent table-reference coverage spans all nibble values,
scales, coefficient pairs, initial histories, clipping edges, partial frames, and odd lengths. This
reduces DSP decode work when titles stream GC-ADPCM, not whole-game FPS or measured battery watts.

PCM8 and PCM16 source buffers also used `std::deque::operator[]` for every decoded sample. Final
AArch64 code repeatedly recalculated the destination block and loaded its block-map entry even
though every write moves forward by exactly one stereo sample. A counted sequential iterator now
keeps the current destination pointer live and performs the rare 4 KiB block transition directly.
The repeated PCM8 mono/stereo bodies fall from 12/14 to 10/13 instructions, while PCM16 mono/stereo
fall from 11/11 to 9/8. Per-iteration data loads fall from 2/3/2/4 to 1/2/1/2 respectively. Exact
mono duplication, stereo ordering, byte expansion, little-endian PCM16 values, zero-length input,
and 1023/1024/1025-sample deque boundaries have permanent coverage. This is a sustained source-
decode reduction when games use PCM buffers, not measured whole-game FPS or battery watts.

Active HLE sources no longer clear their complete 640-byte output frame immediately before the
resampler overwrites it. A full source frame now reaches filtering without any silence clear;
an underrun clears only its unwritten tail, while empty/dequeue frames retain the complete clear
required to replace old audio with silence. At 24 active sources and the 32,728 Hz / 160-sample DSP
cadence, the removed write-before-write traffic is at most 15,360 bytes per tick, or about 3.14 MB/s.
Final AArch64 ThinLTO removes the entry-path 640-byte `memset`; the precise empty and tail calls
remain. Full, partial, and empty dirty-frame regression cases preserve output and state. This is
continuous store/cache work removed from source-heavy HLE audio, not measured whole-game FPS or
battery watts.

Thor's integer SoundTouch stereo-overlap loop uses explicit baseline AArch64 NEON for four frames
at a time and eliminates eight `SDIV` instructions over that span. Negative results retain C++
truncation-toward-zero behavior. ARM64 compile/link and differential regression sources validate
the path; sustained speed and power effects still require a controlled Thor A/B.

WSOLA cross-correlation had a second Windows-to-Android width mismatch: C++ `long` kept its
correlation state at 32 bits on Windows but widened it to 64 bits on Android AArch64, despite the
algorithm's explicit 32-bit scaling and adaptive thresholds. Exact-width state plus a
manual-guided Clang interleave limit keeps the linked loop spill-free. The direct loop falls from
24 to 20 instructions per eight stereo frames, while the repeatedly called rolling-correlation
loop falls from an equivalent 30 to 24 instructions per sixteen frames (20%). At a 512-frame
overlap that removes 256 and 192 inner-loop instructions, respectively. Permanent scalar-reference
coverage preserves paired correlation rounding, per-sample rolling-normalizer rounding, signed
results, and 16/256/1024-frame configurations. These are path-local code-generation results, not
measured game FPS or battery watts.

Azahar changes SoundTouch tempo only: pitch and playback rate stay exactly `1.0`. SoundTouch's
generic crossover-safe pipeline nevertheless ran every input through the unity-rate 64-tap
anti-alias filter, linear interpolator, and intermediate FIFOs before WSOLA. The fork now opts into
a pure-tempo path before processing begins. It tail-calls TDStretch directly, automatically turns
itself off if effective rate ever leaves unity, and leaves default vendored SoundTouch behavior
unchanged for other clients. Steady-state ARM64 work removed per stereo frame is about 68 FIR plus
32 interpolation instructions, 396 bytes of logical DSP reads, and 12 bytes of intermediate
writes. A five-round order-alternated x64 microbenchmark improved the isolated SoundTouch pipeline
from a 48.70 ms median to 38.97 ms for 192,000 frames (1.250x); permanent tests match the standalone
TDStretch stage byte-for-byte across three tempos, awkward chunks, flush, and clear. This is a
host mechanism benchmark and static ARM64 reduction, not a whole-game Thor speed or wattage claim.

SoundTouch's 64-tap anti-alias FIR also carried a hidden x86/Windows assumption: its documented
32-bit accumulator was C++ `long`, which is 64-bit on Android AArch64. Making the width explicit
lets Clang use 32-bit NEON `SMLAL` lanes. The AArch64 stereo loop also loads one canonical
coefficient vector for both channels instead of fetching the duplicated stereo coefficient table.
Final linked core-loop work falls from about 800 scalar instructions per output frame to 68 NEON
instructions, a 91.5% path-local reduction; coefficient traffic falls from 256 to 128 bytes per
output frame. Exact-output/canary tests cover 64-tap anti-alias coefficients and signed-16 extremes.
This helps the audio time-stretch/anti-alias path when active, but is not a measured game FPS or
battery-watt result.

The HLE DSP's four-channel intermediate frame is also planar throughout source accumulation,
auxiliary-bus exchange, and final downmix. This avoids the Q-form `ST4` used by the old
sample-interleaved layout; Arm's Cortex-A510 guide lists that 32-bit structured store at execution
throughput `1/50`. Final ThinLTO uses ordinary paired/vector loads and stores in source mixing,
ordinary channel loads in downmix, and one contiguous 2.5 KiB copy per enabled aux bus direction.
The old sample-major save-state archive layout is preserved explicitly. This is a manual- and
code-generation-backed hot-path change, not a whole-game speed or wattage claim.

Each enabled HLE source also applies four gains over 160 samples for each of three intermediate
mix buses. The AArch64 mixer now handles eight stereo samples at once with one paired Q load,
`UZP` deinterleave, shared signed widening/float conversion, and planar vector accumulation. The
steady loop falls from 31 scalar instructions per sample to 52 instructions per eight samples
(79.0% fewer), while the ramped loop falls from 38 per sample to 74 per eight samples (75.7%
fewer). Gain interpolation and truncation remain exact, and the ramp choice moves outside the
sample loop. These are final ThinLTO instruction-count reductions on this DSP path; whole-game
speed and battery effects still require a controlled Thor A/B.

Source routing now evaluates all three intermediate buses in that one frame-level call. The HLE
frame caller drops from 72 to 24 mixer calls, and an exact-zero auxiliary bus skips the sample loop
without skipping ramp-state transitions. Final AArch64 code tests four gains with one Q load,
`FCMEQ`, and `UMINV`: a steady silent bus takes about 13 predicate/control instructions instead of
the 1,040 instructions in the full 160-sample NEON loop, a path-local reduction of about 98.8%.
The included MerryAudio fixture configures only the main bus while dirtying all three, which is
strong evidence for this common routing shape but not proof that every game behaves that way.
Signed zero remains silent; NaN and every nonzero ramp still mix. No whole-game speed or battery
gain is claimed without a matched Thor run.

Active buses with only front-left/front-right routing now use a second exact AArch64 specialization.
One 64-bit bit-mask test treats both signs of rear zero as silent but sends subnormals, infinities,
NaNs, and every nonzero rear ramp through the unchanged four-channel path. Final ThinLTO reduces
the steady front-stereo loop from 52 to 32 instructions per eight samples (38.5%) and the ramped
loop from 74 to 46 (37.8%), while leaving the full loops at 52 and 74. Omitting rear destination
loads and stores also halves active-bus destination traffic from 5,120 to 2,560 bytes per frame.
The containing function grows from 832 to 1,244 bytes for the two specialized loops. Focused tests
verify exact front output and untouched rear buffers for steady and ramped routing; whole-game FPS
and battery effects still require a matched Thor run.

The complete three-bus source-mix set also remains uninitialized until the first source with any
audible contribution. That source writes each routed bus directly, avoiding the old clear followed
by destination loads and adds; its silent buses are cleared together, and every later source uses
the original accumulation path with no recurring initialization checks. For the common first
front-stereo main bus with two silent auxiliaries, final AArch64 ThinLTO reduces the direct steady/
ramped loops from the accumulated 32/46 to 26/40 instructions per eight samples. That removes 120
repeated inner-loop instructions and 2,560 bytes of load/store traffic per DSP frame, or 523,648
bytes/second. A full bus falls from 52/74 to 38/60, saving 280 inner-loop instructions and 5,120
bytes per frame, or 1,047,296 bytes/second. If that first source routes all three buses fully, the
bound is 840 instructions and 15,360 bytes per frame, or 3,141,888 bytes/second. The all-silent
route still makes one 7,680-byte clear. The retained source/driver code grows by 1,564 bytes, while
the original accumulation function stays byte-for-byte at 1,244 bytes. These are exact inner-loop
and traffic bounds, not measured whole-game FPS or battery-watt gains.

The final HLE mixer also bypasses a bus's complete 160-sample downmix when its frame-wide volume is
exact `+0` or `-0`. NaN and every nonzero volume retain the original arithmetic, while aux exchange
and intermediate state remain unchanged. The earlier AArch64 rewrite reduced the then-scalar
Stereo/Mono loops from 48/46 to 39/37 instructions per eight samples; current ThinLTO immediately
before the next change measured the inlined loops at 40/38. A zero bus skips the downmix completely,
so MerryAudio's one-audible, two-zero Stereo shape still removes two thirds of this final-mix work.

The first audible bus now defines the output directly from its already-clamped samples instead of
first clearing 640 bytes, reloading those zeros through twenty `LD2` instructions, and performing
forty `SQADD` operations. The common first-main-bus Stereo/Mono loops fall from the current 40/38
instructions to 36/35 per eight samples, saving 80/60 repeated instructions per DSP frame. It also
avoids 1,280 bytes of clear-plus-reload traffic per frame, or 261,824 bytes/second at the native DSP
cadence. Leading signed-zero buses are allowed, later audible buses retain exact clamp-then-
saturating-add order, and an all-silent frame still clears stale output. `MixCurrentFrame()` remains
outlined so ThinLTO does not duplicate it into `Tick()`. These are exact path-local code-generation
results, not measured whole-game FPS or battery-watt gains.

Final mixing now consumes every native little-endian planar source in place. Main and disabled-
auxiliary buses view the current input, while enabled auxiliaries view the ARM11-edited shared
return buffer through four independent channel pointers. Only the portable non-native-endian path
stages returns for conversion; enabled sends still copy each new source bus into shared memory.
All arithmetic, auxiliary exchange, sleep/wakeup state, and historical save-state fields remain
intact. Compared with the original route, this eliminates all three 2,560-byte state-staging
copies on every native ARM64 DSP frame: 15,360 bytes of load-plus-store traffic per frame, or
3,141,888 bytes/second. The only remaining copies are zero, one, or two required enabled sends.
Current AArch64 ThinLTO leaves `Mixers::Tick()` at 136 bytes, `AuxReturn()` as a 4-byte return, and
the complete retained mixer-function set four bytes smaller than the preceding live-input version.
The source pointers load once before each unchanged NEON loop. These are continuous DSP-path
traffic reductions, not whole-game FPS or watt measurements.

The HLE source filters now vectorize stereo lanes while preserving time order. In final ThinLTO,
the simple filter replaces two scalar channel multiply chains, shifts, and clamp sequences with one
`SMULL`, one `SMLAL`, one `SSHR`, and one `SQXTN`. The biquad uses one `SMULL`, four `SMLAL`, one
`SSHR`, and one `SQXTN` for both channels. Coefficients load once per 160-sample frame and feedback
history remains in registers; reset passthrough configurations skip redundant arithmetic but still
record the exact final history. This is a bounded DSP-thread instruction reduction. Whole-game
speed and battery effects remain unmeasured until device testing is allowed.

Linear HLE resampling now uses the same stereo-lane strategy. A saturated signed delta and the
existing 24-bit phase are mapped exactly onto one baseline AdvSIMD `SQDMULH`, replacing two scalar
64-bit multiplies, their duplicated clamp chains, and their shifts. The shared None/Linear stepping
path also keeps the two history samples as a virtual prefix instead of inserting them into the
deque. Reused positions perform no deque sample lookup; a normal one-sample advance performs one
sequential load instead of recomputing and loading two deque entries. When the requested rate is
exactly `1.0f` and the Q24 phase is aligned, Linear now tail-routes to that existing None loop:
zero-fraction linear interpolation is exactly the copied `x0` sample, so this removes ten NEON
interpolation/packing instructions per output without duplicating the copy loop. A fractional phase
or any other rate keeps the unchanged `SQDMULH` path. Final ThinLTO keeps None at 368 bytes and
changes Linear from 408 to 448 bytes for the two-gate dispatch. These are sustained DSP bookkeeping
and instruction reductions, not yet measured whole-game FPS or battery-power gains.

## Dynarmic Signed Dual Multiply-Long Update

ARM and Thumb-2 `SMLALD`/`SMLALDX`/`SMLSLD`/`SMLSLDX` now preserve their widening
multiply-accumulate semantics in Dynarmic IR. The ARM64 backend emits four signed-halfword extracts
and two native `SMADDL`/`SMSUBL` operations instead of two `MUL`, two `SXTW`, and two separate
64-bit add/subtract operations after the same extracts. The result stays in general-purpose
registers and retains exact 64-bit wrap, exchange, source/destination alias, and unchanged-flag
behavior for both guest instruction sets.

An exact Thor microbenchmark measured this affected host sequence at 1.95x-2.18x on A510 CPU 0,
1.38x-1.40x on A715 CPUs 3-4, and 1.28x-1.29x on A710 CPU 5. A packed AdvSIMD alternative was
correct but slower and was rejected. The complete focused ARM/Dynarmic suite passes 685 assertions
in 18 cases, and the ARM64 release build passes. This is optimization 93 in the overlapping work
tally; it is not a whole-game FPS or battery-watt claim.

## Dynarmic Signed Multiply-Accumulate-Long Update

ARM and Thumb-2 plain `SMLAL` now lower through Dynarmic's signed multiply-add-long IR. On ARM64,
the old `SXTW` + `SXTW` + 64-bit `MUL` + `ADD` arithmetic sequence becomes one native `SMADDL`.
The `SMLALBB`/`SMLALBT`/`SMLALTB`/`SMLALTT` halfword forms keep their two required signed
halfword extracts, but replace `MUL` + `SXTW` + `ADD` with the same fused instruction. Exact 64-bit
wrap, ARM `S`-bit N/Z updates, unchanged C/V/Q/GE state, Thumb behavior, and accumulator/source
aliasing are preserved.

An alternating-order Thor microbenchmark measured the exact plain path at 5.244x on A510 CPU 0,
1.251x on both A715 CPUs 3-4, and 1.064x on A710 CPU 5. The halfword path measured 2.084x,
1.547x-1.558x, and 1.316x respectively. CPUs 6-7 rejected shell affinity, so no X3 device result is
claimed. The new focused test passes 282 assertions; the complete ARM/Dynarmic suite passes 967
assertions in 19 cases, and the exact ARM64 release build passes. This is optimization 94 in the
overlapping work tally; these figures describe only affected guest multiply-accumulate-long
instructions, not whole-game FPS or battery watts.

## Dynarmic Unsigned Widening-Multiply Update

ARM and Thumb-2 `UMULL` and `UMLAL` now retain their unsigned 32x32-to-64-bit operation in
Dynarmic IR. The ARM64 backend emits native `UMULL Xd, Wn, Wm`; `UMLAL` then adds its packed
64-bit accumulator. This replaces accidental X-form `MUL` inputs produced by separately
zero-extending both operands. `UMAAL` deliberately keeps its existing lowering because the same
change regressed the Thor's X3 core.

An alternating-order Thor benchmark measured the affected `UMULL`/`UMLAL` sequences at
1.997x/1.794x on A510 CPU 0. A715 CPUs 3-4 and A710 CPU 6 were within 0.31% of parity; X3 CPU 7
was within 0.05% for the two accepted paths. Direct `UMADDL` and reordered/fused `UMAAL`
alternatives were faster on A510 but materially slower on one or more big cores, so they were
rejected. The complete on-device ARM/Dynarmic suite passes 1,217 assertions in 19 cases, and the
exact ARM64 release build passes. This is optimization 95 in the overlapping work tally; it is not
a whole-game FPS, battery-watt, or additive speedup claim.

## Dynarmic Signed Widening-Multiply Update

ARM and Thumb-2 `SMULL` now retain their signed 32x32-to-64-bit operation in Dynarmic IR. The
ARM64 backend emits one native `SMULL Xd, Wn, Wm` instead of two `SXTW` instructions followed by
X-form `MUL`. ARM flag-setting, Thumb behavior, signed extremes, complete 64-bit results, and
source/destination aliases remain exact.

An alternating-order Thor benchmark measured that affected host sequence at 3.500x on A510 CPU 0,
1.826x-1.849x on A715 CPUs 3-4, 1.626x on A710 CPU 6, and 1.600x on X3 CPU 7. The complete
on-device ARM/Dynarmic suite passes 1,364 assertions in 20 cases, and the exact ARM64 release build
passes. This is optimization 96 in the overlapping work tally; these figures apply only when the
guest executes `SMULL`, not to whole-game FPS or battery watts.

## Recent Dynarmic Multiply and Widening-Shift Updates

The next signed DSP passes keep `SMMUL{R}`/`SMMLA{R}`/`SMMLS{R}` and `SMULWB`/`SMULWT` in
native-width Dynarmic IR. ARM64 consequently uses `SMULL`, `SMADDL`/`SMSUBL`, and the required
halfword select/shift operations instead of redundant long extensions around X-form `MUL`.
Exact-sequence Thor measurements were **1.57x-2.13x** for the signed high-word forms and
**1.46x-2.24x** for the signed word-by-halfword forms. A proposed `SMLAWB`/`SMLAWT` conversion was
rejected because it slightly regressed A715 and X3, so those accumulate forms keep their established
lowering. These are optimizations 97 and 98 in the overlapping work tally.

A32 NEON `VSHLL.S/U8`, `.S/U16`, and `.S/U32` now keep their immediately adjacent, sole-use
widen-and-shift shape through the ARM64 backend. The previous `SXTL`/`UXTL` plus `SHL` pair becomes
one native `SSHLL`/`USHLL`. Shared, non-adjacent, mismatched, and out-of-range IR keeps the original
two-instruction path. A disassembly-checked, nonzero-checksum Thor benchmark measured the exact
affected sequence at **4.01x-4.14x on A510**, **2.00x on A715/A710**, and **2.80x on X3**. All
1,760 assertions in 23 focused ARM/Dynarmic cases pass on Thor, including the largest signed and
unsigned immediates, high registers, and source/destination overlap. This is optimization 99; none
of these path-local figures can be added together or treated as a whole-game FPS or battery-watt
result.

The architectural maximum-width `VSHLL.I8/I16/I32` forms are now covered too. Their adjacent,
sole-use zero extension plus shift-by-8/16/32 becomes one native AArch64 `SHLL`; smaller signed and
unsigned shifts retain the `SSHLL`/`USHLL` fusion above. Exact Thor medians were **4.06x-4.27x on
A510**, **2.00x on A715/A710**, and **2.80x on X3** for this instruction sequence. The expanded
on-device suite passes 1,766 assertions in 23 cases, including partial register overlap. This is
optimization 100, not a 100x emulator or whole-game speed claim.

A32 NEON non-rounding shift-and-narrow operations now fuse too. `VSHRN`, `VQSHRN.S`,
`VQSHRN.U`, and `VQSHRUN.S` across 16-to-8, 32-to-16, and 64-to-32-bit lanes replace the old
`SSHR`/`USHR` plus `XTN`/`SQXTN`/`UQXTN`/`SQXTUN` pairs with native AArch64 `SHRN`, `SQSHRN`,
`UQSHRN`, or `SQSHRUN`. The fusion requires an immediately adjacent exact sole consumer and a
legal constant shift; all other IR keeps the established fallback. Exact Thor measurements for
the saturated forms were **about 4x on A510**, **2x on A715/A710**, and **2.8x on X3**. Plain
`VSHRN` was about **4x on A510** and throughput-neutral on the larger cores while still halving its
vector instruction count. The expanded on-device suite passes 1,823 assertions in 24 cases. This
is optimization 101, and its path-local results must not be added to the other 100 items or treated
as a whole-game FPS or battery-watt result.

The four rounding shift-and-narrow families now use native AArch64 too. A32 `VRSHRN`,
`VQRSHRN.S`, `VQRSHRN.U`, and `VQRSHRUN.S` across 16-to-8, 32-to-16, and 64-to-32-bit lanes used
an overflow-safe rounding expansion followed by a separate narrow. Dynarmic now carries that exact
operation in first-class IR and ARM64 emits one `RSHRN`, `SQRSHRN`, `UQRSHRN`, or `SQRSHRUN`;
x64 and RISC-V retain the established correction sequence through a polyfill. Exact Thor
microbenchmarks measured **13.13x-14.81x on A510**, **2.81x-3.54x on A715**,
**3.23x-3.59x on A710**, and **3.51x-3.96x on X3** for these specific operations. The complete
on-device suite passes 1,880 assertions in 24 cases. This is optimization 102, not an emulator-wide
13x result: whole-game FPS and watts depend on each title's dynamic instruction mix and still need
a matched gameplay A/B.

A32/A64 vector rounding right shifts and rounding right-shift-accumulates now stay first-class too.
The previous overflow-safe shift/broadcast/AND/compare/subtract expansion becomes one native ARM64
`SRSHR`/`URSHR`; its following add becomes one `SRSRA`/`URSRA`. x64 and RISC-V reconstruct the
established exact sequence through a polyfill. Thor exact-path medians measured **9.88x-10.61x on
A510, 2.50x-2.51x on A715, 2.71x-2.72x on A710, and 3.52x-4.77x on X3** for `VRSHR`, and
**5.08x-5.65x, 2.99x-3.01x, 3.39x-3.50x, and 2.54x-3.37x** respectively for `VRSRA`. A plain
non-rounding `VSRA` fusion was deliberately rejected: it was neutral on A715/A710 but regressed X3
by **5.7%-22.3%**. All 1,928 assertions in 25 focused cases pass on Thor. This is optimization 103;
these results apply only to the affected instructions and are not a whole-game FPS or watt claim.

A32/A64 vector shift-inserts now remain first-class operations as well. `VSLI`/`SLI` and
`VSRI`/`SRI` previously expanded on ARM64 to a shift, scalar immediate materialization, vector
broadcast, bit clear, and OR; the backend now emits the matching single native instruction. x64 and
RISC-V retain exact portable polyfills. This removes four of five host instructions on the affected
path. Thor measurements across every lane width were **6.94x-8.32x on A510, 1.99x-2.01x on A715,
2.17x-2.21x on A710, and 2.42x-2.43x on X3**. All 1,976 assertions in 26 focused cases pass on
Thor. This is optimization 104 in the overlapping tally, not a 104x emulator result; title FPS and
battery watts still require a matched gameplay A/B.

A32 ARMv6 `USAD8`/`USADA8`, which is directly relevant to the 3DS ARM11 guest, now lowers on ARM64
from `MOVI` + `UABD` + `AND` + `UADDLV` to `UABDL` + four-halfword `UADDLV`. Widening the eight byte
differences lets the reduction consume exactly the guest instruction's low four bytes without a
materialized mask, cutting this affected path from four host instructions to two. Thor exact-path
medians were **1.76x on A510, 2.52x on A715, 2.51x on A710, and 2.81x on X3**. All 2,176
assertions in 27 focused cases pass on Thor, including ARM/Thumb forms, accumulator wrap, source
aliases, maximum differences, untouched registers, and flags. This is optimization 105 in the
overlapping tally; the measurements are path-local and are not a whole-game FPS or watt claim.

A32 ARMv6 `PKHBT`/`PKHTB`, also part of the 3DS ARM11 guest ISA, now remain first-class through
Dynarmic IR. Their generic shift, two masks, and OR become a native ARM64 `BFI`/`BFXIL`, with one
immediate shift only for non-aligned bottom fields or top fields crossing bit 31. That cuts the
measured forms from three or four host instructions to one or two. Thor exact-path medians ranged
from **1.20x-3.01x on A510, 1.75x-2.46x on A715, 1.34x-2.16x on A710, and 1.49x-2.23x on X3** across
seven representative bottom/top and shift-boundary forms. All 3,646 assertions in 28 focused cases
pass on Thor, including ARM/Thumb encodings and destination/source aliases. This is optimization
106; the results cover only these guest instructions, not whole-game FPS or battery watts.

A32 ARM/Thumb-2 scalar `SXTAB`/`SXTAH`/`UXTAB`/`UXTAH` with no rotation now remain first-class
through Dynarmic IR. ARM64 emits the matching extended-register `ADD`, collapsing the affected
path from a separate byte/halfword extension plus add into one host instruction; x64 and RISC-V
retain the exact portable expansion. Thor rotation-zero medians were **1.33x-1.34x on A510,
1.17x-1.20x on A715, and 1.13x-1.14x on A710**. Nonzero rotations deliberately keep the old path
because their required `ROR` plus extended `ADD` was neutral on A510 and one form regressed 0.50%.
The X3 was parked by Android `core_ctl` during this run, so no physical X3 result is claimed. All
7,486 assertions in 29 focused cases pass on Thor. This is optimization 107; these exact-path
figures do not predict whole-game FPS or battery watts.

A32 ARM/Thumb-2 `SSAT16`/`USAT16` now also remain first-class through Dynarmic IR. The old lowering
extracted and saturated each halfword independently, repacked the result, and updated sticky
`CPSR.Q` twice. ARM64 now shares both lanes' bounds, clamps them with scalar `CMP`/`CSEL`, packs
with `BFI`, compares the packed result once, and performs one sticky-Q update. It deliberately
does not use AdvSIMD saturating shifts because their host `FPSR.QC` side effect could corrupt the
guest's independent VFP saturation state. x64 and RISC-V retain exact portable polyfills. Thor
exact-path medians ranged from **1.09x-1.31x on A510, approximately 2.00x on A715, 1.93x-2.02x on
A710, and 1.50x-2.03x on X3**. The full focused suite passed 51,007 assertions in 30 cases, and the
new test passed 43,521 assertions when pinned separately to each Thor CPU class. This is
optimization 108; it is a path-local result, not a whole-game FPS or battery-watt claim.

A32 ARM/Thumb-2 `SXTB16` now remains first-class through Dynarmic IR as
`PackedSignExtendByteToHalf`. ARM64 extracts the upper selected byte with `SBFX` before writing the
sign-extended low byte with `SXTB`, then inserts the saved halfword with `BFI`; that order is
required when the guest source and destination alias. This replaces the old two masks, constant
materialization, multiply, and OR with three host instructions after any guest rotation. x64 and
RISC-V retain the exact portable expansion. Thor exact-path medians ranged from **1.00x-1.33x on
A510, 1.54x-2.04x on A715, 1.24x-1.33x on A710, and 1.24x-1.62x on X3** across independent and
dependent rotation-zero and rotation-eight forms. The rotated-independent A510 form was effectively
tied at 1.000965x. The new 2,721-assertion test passed separately on all four Thor CPU classes, and
the final 53,728-assertion focused suite passed. This is optimization 109; the measurements cover
only this guest instruction path and are not whole-game FPS or battery-watt results.

A32 ARM/Thumb-2 `SXTAB16` with rotations 8/16/24 now reuses that same first-class packed sign
extension before its packed halfword add. On ARM64 the measured nonzero-rotation path falls from
ten host instructions to eight, including the required `ROR`; x64 and RISC-V polyfill back to the
established portable DAG. ROR8 exact-path medians improved **1.05x-1.07x on A510, 1.16x on A715,
1.09x-1.12x on A710, and 1.07x-1.08x on X3** across four independent chains and a sequential
source-alias chain. Rotation zero deliberately keeps its old lowering because the shorter scalar
composition regressed X3 throughput by 2.0%. A proposed five-instruction `UZP1` + `SADDW` route was
also rejected after regressing X3 by up to 10.6%. The new 6,801-assertion test passed separately on
all four Thor CPU classes, and the full focused suite passed 60,529 assertions in 32 cases. This is
optimization 110; the measurements are path-local, not whole-game FPS or battery-watt results.

A32 ARM/Thumb-2 `RBIT` now remains first-class through Dynarmic IR as `ReverseBits32`. ARM64 emits
one native `RBIT` instead of the old 17-instruction mask/shift/OR network; x64 and RISC-V expand the
new semantic operation back to that exact portable network. A disassembly-checked, 16-million-
operation Thor benchmark measured **11.31x-17.58x** for four independent chains and
**7.02x-9.64x** for a sequential dependency chain across A510, A715, A710, and X3. The new test
passed 612 assertions on every Thor CPU class, and the full focused suite passed 61,141 assertions
in 33 cases. This is optimization 111; it is an exact instruction-path result, not a whole-game
FPS or battery-watt claim.

## Vulkan Worker-Power Updates

Vulkan command chunks are recycled after their commands execute. Their command pointers and storage
offset were reset, but an independent record counter was not, so a recycled chunk could permanently
report non-empty. The per-frame `WaitWorker()` path could then queue an empty job, invoke the
descriptor dispatch callback, notify and wake the Vulkan worker, and traverse queue, execution, and
reserve locks without recording GPU work. `Empty()` now reads the authoritative linked-list head,
which is set only after successful placement and cleared only after all commands execute. Actual
command submission, condition-variable signaling, and scheduler lock ordering are unchanged.

Timeline-semaphore completion polling is also rate-limited to every fourth routine submission,
matching the command-buffer pool depth. The cached tick can only understate completed GPU work;
resource-pool exhaustion and explicit waits still query immediately. This changes scheduled
per-submit timeline-counter driver calls from four to one, leaving only three intermediate submits
between routine queries. Garbage collection may be delayed conservatively, never advanced ahead of
confirmed GPU completion. The logical and completed ticks carry numbers only, so their atomics use
relaxed ordering while Vulkan submission/completion and existing mutexes continue to synchronize
the actual work and resource queues. Final ARM64 code uses relaxed `LDADD`/CAS helpers and ordinary
`LDR` counter reads, and each refresh makes at most one timeline-counter driver query.

When duplicate-frame suppression or Eco Turbo skips host presentation, Vulkan now flushes pending
3DS rendering work to the graphics queue without waiting for the GPU to finish it. The old fallback
performed a full timeline wait on every non-presented VBlank, unnecessarily serializing the
emulation thread with Adreno. Timeline-tagged command/descriptor pools, stream-buffer wrap checks,
and conservative garbage collection still protect in-flight resources. Screenshot readback,
render-frame recreation, swapchain/window destruction, and renderer teardown keep their explicit
completion waits.

Normal native threaded presentation also no longer joins the Vulkan command worker at every frame
boundary. Its render submission and presentation-queue notification now occupy one typed worker
command and one routine dispatch: the worker submits first, releases the Vulkan submit lock,
enqueues the frame, releases the predicate lock, and then wakes presentation. This removes the old
second command-chunk dispatch, descriptor-dispatch callback, scheduler queue push/pop, worker
notification, and reserve-chunk lock cycle from every normally presented frame. Resource
retirement uses
completed timeline ticks and requires completion to advance strictly beyond the sentenced tick, so
queued or in-flight surfaces remain alive. LibRetro and the synchronous presentation fallback
retain their worker drains, and mutable presentation clear data is copied into the worker command.
The final Android AArch64 `TickFrame()` is only a direct cache tick with no `WaitWorker()` call.
These are exact per-frame synchronization removals, not measured whole-game FPS or battery-watt
percentages.

## Vulkan Texture-Filter Fidelity/Power Update

Vulkan no longer silently enables the device's maximum anisotropy for every guest PICA texture.
The 3DS sampler state exposes nearest/linear and mip filtering but no anisotropy choice, and the
OpenGL backend already honors that state without adding anisotropy. Guest Vulkan samplers now do
the same. This restores backend parity while removing adaptive extra texture taps that games never
requested.

The final linear and nearest screen samplers are also isotropic. This keeps nearest presentation
deterministic under Vulkan and avoids paying for anisotropic work on the final screen quads.
Qualcomm's Adreno guide identifies texture fetches, cache misses, and high anisotropy as texture-pipe
costs; the [Vulkan sampling specification](https://docs.vulkan.org/spec/latest/chapters/textures.html)
also makes nearest filtering with anisotropy implementation-dependent. The release ARM64 build and
linked sampler-create fields are verified, but actual FPS, power, and image effects still require a
matched Thor A/B.

## Thor Screenshot

This is a live AYN Thor screenshot of this Azahar Android fork showing the game library and visible bundled-cheat labels. It does not imply games are bundled with this repository.

![Azahar Android library on Thor showing cheat labels](docs/media/screenshots/azahar-library-cheats.png)

## Build Locally

This fork is currently aimed at Android/AYN Thor APK builds:

```powershell
cd src/android
.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite
```

APK output:

```text
src/android/app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk
```

## Cheats

Bundled cheats are copied into the Android app assets for convenience. They are community-style GateShark/CTRPF text files and may be incomplete, wrong for a region/revision, unstable, or game-breaking. Treat them as personal presets, not a curated public database.

Existing user cheat files on-device may not be overwritten by the app if the destination file is already present. If a bundled cheat was fixed in git but a device still shows the old version, manually replace or remove the existing device cheat file first.

## Upstream Credit

Azahar is an open-source Nintendo 3DS emulator project based on Citra. This fork exists because upstream Azahar, PabloMK7's Citra fork, Lime3DS, Citra, and many emulator contributors did the real foundational work.

This repository remains under the upstream license terms. See [license.txt](license.txt).
