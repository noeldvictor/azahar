# Agent Notes

- Work directly on `master` for this repository unless the user explicitly asks for a branch.
- Android work lives under `src/android`; keep cheat-build branding and UI changes scoped there when possible.
- For local Android builds, use JDK 17 and the Android SDK, then run `.\gradlew.bat :app:assembleVanillaDebug` from `src/android`.
- Before pushing Android changes, verify at least `:app:compileVanillaDebugKotlin`; prefer a full `:app:assembleVanillaDebug` when native code or packaging is involved.
- Do not commit generated Gradle, CMake, or APK output.
