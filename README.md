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

For ordinary 30/60 FPS play, start with the Thor's **Standard** device performance mode and move up
only when a title cannot hold speed. In a matched 7th Dragon title-screen check, Standard preserved
the exact pixels and the same 30 FPS frame pacing as High Performance while lowering the fixed live
Adreno clock from 615 to 401 MHz. That is the strongest current under-6-W operating candidate, not
yet a battery-watt result because the device was still AC-powered during the comparison.

The Thor evidence ledger currently has **158 numbered entries, 157 of them active**. Entry 158
batches four adjacent exact SoundTouch full-search correlations on Android AArch64, entry 157 skips
a complete scheduler context handoff when runnable selection returns the exact thread already
running, and entry 156 gates detailed frame-breakdown timing when that overlay is hidden. The
earlier ARM64
absolute-offset page-table entry was withdrawn after it caused reproducible game-start crashes on
the Thor. Smaller figures quoted
for a recent time window or code slice are subsets, not the project total. The active entries are
not additive percentages: many affect different paths, and whole-game FPS or battery watts still
require a matched title/scene/device A/B.

The project has now reached the point where more isolated instruction wins risk missing the real
system bottleneck. The next accepted performance work is selected from opt-in whole-frame counters
for CPU draw fallback, Vulkan submissions and waits, render-pass churn, texture traffic, and final
presentation work. The profiler is diagnostic infrastructure, not optimization 143, and its timing
overhead means profiling APKs are never used for FPS, power, or thermal comparisons.

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
- Fast-forward controls use clearer labels, and toggle-only toasts report the active speed or the
  return to normal speed without showing again when settings are merely reloaded.
- Android Eco Turbo defaults on and caps host presentation/composition to 60 FPS during explicit
  Turbo or fully uncapped emulation while guest emulation continues at the selected speed. It can
  be disabled under General for smoother fast-forward on the Thor's 120 Hz panel. Normal play
  should keep **Limit Speed** enabled at 100%; the UI now warns that disabling it can produce
  hundreds of FPS and high power use.
- During emulation, Android receives both a refresh-only window preference near 60 Hz and a 60 Hz
  game-surface frame-rate hint. This avoids tying ordinary 3DS presentation to the Thor primary
  panel's 120 Hz maximum; frontend menus retain their high-refresh preference.
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
  blending, and rewriting the entire destination vector. Masks `x`/`xy` store the low scalar
  register directly, while `xz`/`xw`/`xyw`/`xzw` do the same before their remaining lane store,
  removing one address instruction from each affected write. Full-vector stores stay native
  `STR Q`.
- The AArch64 PICA JIT caches the selected output-register bank pointer once per shader invocation
  and refreshes it only after geometry `EMIT`, removing repeated bank loads and address generation
  from every ordinary output write.
- Large indexed draws scan their index bounds through four independent AArch64 min/max chains and
  two paired Q loads per 64-byte band. Short draws retain the compact single-vector loop.
- Indexed draws that reach the PICA CPU fallback search its fully associative 64-entry vertex cache
  sixteen IDs per AArch64 band with paired Q loads, exact equality masks, and one `UMINV`. Full-cache
  miss search work falls 87.2% in final ThinLTO while preserving first-match and circular replacement
  behavior; exhaustive ARM64 coverage passes on the Thor.
- When an indexed CPU-fallback draw produces exactly the number of PICA attributes its downstream
  stage consumes, cache hits submit the entry directly and misses write shader output directly into
  the replacement entry. Mismatched and non-indexed layouts retain the complete-buffer route.
- The hottest PICA command-list parser has an AArch64 four-pair `LD2` path that validates ordinary
  headers together, vector-updates consecutive registers, and coalesces their dirty-bit writes.
- The AArch64 PICA `EX2` and `LG2` helpers pack their exact approximation constants into aligned
  paired-Q blocks, replacing repeated scalar address/load sequences with 128-bit paired loads.
- When `EX2` or `LG2` executes inside a PICA guest `CALL`, the AArch64 shader JIT keeps the guest
  link in reserved `X16` across the local helper `BL` instead of pushing and reloading `X30`.
  Normal architectural returns still use `X30`, and the established guest stack frame is unchanged.
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
- Android AArch64 stereo full search now evaluates four adjacent SoundTouch WSOLA offsets together,
  sharing compare/input loads and batching exact sequential normalizer deltas. A same-session Thor
  profile reduced correlation self share from 1.30% to 0.87% and the complete SoundTouch processing
  share from 1.66% to 1.01%; matched whole-process counters were neutral, so this is a measured
  recurring-hotspot reduction rather than a whole-game speed or power claim.
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
- Android upstream is integrated through `f6a3e3aa5`: the wrapper uses Gradle 8.14.5, packages with
  target SDK 37, and applies display-cutout margins to both the emulation surface container and
  in-game menu instead of opting out of enforced edge-to-edge behavior.
- Thor dual-display emulation is fixed to top screen on the primary panel and bottom screen on the secondary panel; the old hidden virtual secondary display fallback is removed.
- The Thor GPU Driver Manager has a guided driver picker with visible download buttons, notes, recommended generic Turnip first, recent Turnip rollback builds, Qualcomm and Turnip variants as troubleshooting choices, manual ZIP install, and system-driver fallback.
- Per-game settings on Android: long-press a game and open **Game Settings** to override
  graphics, layout, audio, and system options for that title only. Overrides live in
  `GameSettings/<title id>.ini`, apply at launch without touching your global config, and store only
  the values you changed. Bundled Thor profiles under
  `src/android/app/src/main/assets/game_profiles/` seed that folder on first run and never overwrite
  your edits.
- E.X. Troopers ships a Thor compatibility profile; Conception II ships a crisp-presentation
  profile that renders at 5x so both Thor panels downscale instead of stretching.
- Snapdragon Game Super Resolution 1 is available as a Screen Filter, vendored verbatim from
  Qualcomm's BSD-3-Clause shader for the Vulkan present path.
- Thor Base/Pro/Max optimization notes are tracked in `docs/thor-optimization-notes.md`.
- Cheat gap tracking is documented in `docs/thor-cheat-gaps.md`.

## Engineering Detail

The per-subsystem write-ups that used to live here are consolidated so there is one place to
change when behavior changes:

- **[AGENTS.md](AGENTS.md)** is the authoritative engineering ledger. Every accepted optimization,
  every rejected experiment and why it was rejected, and the invariants that must not be silently
  "cleaned up" live there as rules. Read it before changing code.
- **[docs/thor-optimization-notes.md](docs/thor-optimization-notes.md)** holds the dated evidence
  behind those rules: what was measured, on which title and scene, with which driver, and what the
  numbers do and do not prove.
- **[docs/thor-cheat-gaps.md](docs/thor-cheat-gaps.md)** tracks cheat coverage gaps.
- **[CLAUDE.md](CLAUDE.md)** carries the working rules for AI agents in this repository.
- **[AI-POLICY.md](AI-POLICY.md)** states how AI assistance is used here.

Areas covered there include the vendored ARM64 Dynarmic work, AArch64 PICA and audio paths, the
Vulkan presentation and surface-lifetime changes, the scheduler and page-table guards, and the
Thor power-measurement methodology.

A standing caution that applies to all of it: isolated microbenchmark ratios are not whole-game FPS
or battery-watt claims, and this repository deliberately keeps those separate.

## Thor Screenshot

This is a live AYN Thor screenshot of this Azahar Android fork showing the game library and visible bundled-cheat labels. It does not imply games are bundled with this repository.

![Azahar Android library on Thor showing cheat labels](docs/media/screenshots/azahar-library-cheats.png)

## Build Locally

This fork is currently aimed at Android/AYN Thor APK builds:

```powershell
cd src/android
.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite --no-configuration-cache
```

APK output:

```text
src/android/app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk
```

## Cheats

Bundled cheats are copied into the Android app assets for convenience. They are community-style GateShark/CTRPF text files and may be incomplete, wrong for a region/revision, unstable, or game-breaking. Treat them as personal presets, not a curated public database.

Existing user cheat files on-device may not be overwritten by the app if the destination file is already present. If a bundled cheat was fixed in git but a device still shows the old version, manually replace or remove the existing device cheat file first.

The Android Cheats screen also has `Find value` for legally owned offline single-player games. It
waits for an acknowledged guest pause, searches only mapped guest application memory, and supports
a single verified temporary write with guarded restoration. Slow initial searches can be canceled
without keeping partial results. Search matches stay temporary: Azahar does not turn a one-session
raw address into a persistent cheat without a stable pointer, signature, or relaunch validation.

## Upstream Credit

Azahar is an open-source Nintendo 3DS emulator project based on Citra. This fork exists because upstream Azahar, PabloMK7's Citra fork, Lime3DS, Citra, and many emulator contributors did the real foundational work.

This repository remains under the upstream license terms. See [license.txt](license.txt).
