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
