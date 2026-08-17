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
- Missing/stale ROM entries stop before launch instead of continuing into emulation.
- Game equality was fixed to compare real fields instead of treating hash collisions as equality.
- Thor builds are Android `arm64-v8a` only unless deliberately changed.
- The AArch64 PICA vertex-shader JIT lowers arbitrary source swizzles to native AdvSIMD table
  lookup instead of serial vector copies and lane inserts.
- Partial PICA destination masks use native AArch64 SIMD lane stores instead of loading,
  blending, and rewriting the entire destination vector. Full-vector stores stay native `STR Q`.
- The AArch64 PICA JIT caches the selected output-register bank pointer once per shader invocation
  and refreshes it only after geometry `EMIT`, removing repeated bank loads and address generation
  from every ordinary output write.
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
  time. Decode uses ordinary RGBA stores rather than the Cortex-A510-hostile `ST4` form.
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
- Integer SoundTouch stereo overlap uses exact AArch64 NEON widening multiply-accumulate and
  power-of-two shifts, processing four frames per vector loop without the old per-channel scalar
  divides. SoundTouch is vendored here so this ARM64 path does not depend on a separate fork.
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
  linked blocks while preserving callback-visible CPSR behavior; and
- direct packed-flag condition tests plus cycle-count flag reuse, removing the redundant compare at
  normal linked-block exits. A common simple conditional linked-block path falls from five ARM64
  control/cycle instructions to three.

These changes target CPU-bound emulation and sustainable performance. They do not stack as simple
percentages, and no whole-game wattage claim is made without a matched device A/B. Exact emitted
sequences, build evidence, limitations, and the required benchmark controls are recorded in the
[Thor optimization notes](docs/thor-optimization-notes.md).

## AArch64 PICA Updates

The PICA vertex-shader JIT now attacks five common AArch64 lowering costs: baseline Armv8-A
AdvSIMD `TBL` handles arbitrary source swizzles, `ST1` lane stores handle partial destination masks
without reading untouched lanes, and a cached output-bank pointer removes repeated bank loads and
address generation. Its `EX2` approximation also packs eight exact constants into two Q registers:
constant setup falls from eight `ADR` plus eight scalar `LDR` instructions to one `ADR`, one `LDP`,
and one lane `DUP`. That is 13 fewer instructions inside each helper execution, or a net 12 for an
otherwise minimal one-`EX2` shader after its required one-time `1.0` register initialization. These
are exact generated-instruction and memory-traffic reductions validated by ARM64 compilation and
focused regression sources. Whole-game FPS and battery-watt effects still require a controlled
Thor A/B and are not estimated from static counts.

The normal positive-input `LG2` path uses the same paired-load strategy for its five exact
polynomial coefficients. Two separately addressed groups that required five setup instructions now
use one `ADR` plus one Q-form `LDP`, removing three instructions from every positive `LG2` helper
execution. NaN, zero, negative, and infinity paths retain their existing branches and literal
vectors.

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
in the Cortex-A510 guide. The reverse path uses D-form `LD4` to deinterleave RGBA input and `ST2`
to restore the two Morton rows. Exhaustive coverage checks every possible packed 16-bit value for
all three formats, and odd-length linear tests protect the scalar tail and buffer canaries. This is
a verified format-conversion CPU-work reduction, not yet a whole-game FPS or wattage result.

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

## AArch64 Audio Updates

Thor's integer SoundTouch path already receives useful compiler-generated NEON for WSOLA
cross-correlation. The remaining scalar stereo-overlap loop now uses explicit baseline AArch64 NEON
for four frames at a time and eliminates eight `SDIV` instructions over that span. Negative results
retain C++ truncation-toward-zero behavior. ARM64 compile/link and differential regression sources
validate the path; sustained speed and power effects still require a controlled Thor A/B.

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

The final HLE mixer also bypasses a bus's complete 160-sample downmix when its frame-wide volume is
exact `+0` or `-0`. NaN and every nonzero volume retain the original arithmetic, while aux exchange,
intermediate state, and output clearing remain unchanged. Final ThinLTO adds only `FCMP`/`B.EQ` per
bus. The active AArch64 downmix now handles eight samples per loop with Q-form `LD2`/`ST2`, while
preserving the exact multiply/FMA, conversion, saturation, and accumulation order in each half.
Final linked stereo work falls from 48 to 39 instructions per eight samples (960 to 780 per active
bus/frame, 18.75%); mono falls from 46 to 37 (920 to 740, 19.6%). Buffer traffic remains 3,840 bytes
per active bus/frame. A zero bus now skips those smaller bodies completely; MerryAudio's one-audible,
two-zero stereo shape still removes 66.7% of final downmix-loop work and 7,680 bytes per frame.
These are path-local code-generation results, not measured whole-game or battery gains.

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
sequential load instead of recomputing and loading two deque entries. Final ThinLTO keeps the exact
two-lane `SQDMULH`, shrinks Linear from 636 to 408 bytes, and shrinks None from 560 to 368 bytes.
These are sustained DSP bookkeeping and instruction reductions, not yet measured whole-game FPS
or battery-power gains.

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
