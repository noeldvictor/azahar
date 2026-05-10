# Agent Notes

- Work directly on `master` for this repository unless the user explicitly asks for a branch.
- The active GitHub fork is `git@github.com:noeldvictor/azahar-thor-experiment.git`; keep fork-facing docs branded as Azahar Thor Experiment, not upstream Azahar.
- Public-facing docs should clearly disclose that this is a personal, AI-assisted/vibe-coded, no-support experiment with no stability guarantee.
- Android work lives under `src/android`; keep cheat-build branding and UI changes scoped there when possible.
- Performance work targets AYN Thor Base/Pro/Max: Snapdragon 8 Gen 2, Adreno 740, active cooling, LPDDR5X, and UFS4 storage. Do not tune defaults around Thor Lite / Snapdragon 865 unless the user explicitly asks.
- For local Android builds, use JDK 17 and the Android SDK from `src/android`.
- The Android APK target for this repo is the AYN Thor, so keep `abiFilter` set to `arm64-v8a` only. Do not build x86_64 unless the user explicitly asks for it.
- When building an APK to send to the AYN Thor, use `.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite` and install `app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk`. This is release-optimized, debug-signed, uses the `-thor` version suffix, and keeps the `.debug` package so it installs over the Thor test app without the debug/JNI-debug performance hit.
- Use `:app:assembleVanillaDebug` only when an actual debuggable APK is needed.
- Before pushing Android changes, verify at least `:app:compileVanillaDebugKotlin`; prefer a full `:app:assembleVanillaRelWithDebInfoLite` when native code, packaging, or Thor installs are involved.
- Do not commit generated Gradle, CMake, or APK output.
- E.X. Troopers (`0004000000053700`) has a custom Thor compatibility profile: Android launch caps resolution to 2x, forces JIT/HW shader/shader cache basics, disables custom texture loading, and the core hack list enables the texture-copy fallback skip for that title. Keep its recommended cheat preset at 30 FPS unless on-device testing proves 60 FPS is stable.
- Keep Android/Thor profile manifests under `src/android/app/src/main/assets/game_profiles/` in sync with any hardcoded game-specific profile logic.
- Keep first-party Markdown current when behavior changes: `README.md`, `AGENTS.md`, `AI-POLICY.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `docs/*.md`, `tools/README.md`, and Android asset READMEs. Leave vendored dependency Markdown and license files alone unless a dependency itself changes.
- Track Thor performance findings in `docs/thor-optimization-notes.md`. Current top candidate is avoiding the hidden secondary-display render path when no separate secondary layout is active; do not skip the real 3DS bottom screen in Thor dual-screen mode.
