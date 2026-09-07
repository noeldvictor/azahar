# CLAUDE.md

`AGENTS.md` is the authoritative engineering ledger for this fork; read it before
changing code, and keep it updated when behavior changes. This file holds the
working rules that are easy to get wrong and expensive to relearn.

## Disk space is a hard constraint

Generated Android build output is the largest thing this project produces, and the
host `C:` drive runs close to full. Treat storage as a budget, not a byproduct.

- Check free `C:` space and the sizes of `src/android/app/.cxx` and
  `src/android/app/build` before and after any large native build. Report both.
- Keep only the active `arm64-v8a` release configuration hash under
  `.cxx/RelWithDebInfo`. Obsolete configuration hashes are stale caches, not history.
- Never build Debug, x86, or x86_64 variants unless explicitly asked for one.
- An opt-in `-PthorFrameProfiling=true` configuration must be removed in the same
  work tranche once its binary evidence is captured.

## Always clean up when the work is done

Cleanup is part of finishing a task, not a separate request. Before handing work
back:

- Remove stale CMake configuration hashes, Gradle intermediates, packaging/mapping/
  symbol staging, and any profiling build tree that is no longer needed. Keep
  `build/outputs/apk` for APKs still under test.
- Delete scratch APKs, `perf.data` captures, screenshots, UI dumps, and temporary
  binaries that were pushed to the device (`/data/local/tmp`) or written to the
  repository or the device's shared storage.
- Stop the Gradle daemon first if it still holds an intermediate open. A
  `.ninja_deps` sharing failure usually means another build owns the configuration:
  wait for that owner instead of deleting the cache.
- Use exact validated paths inside this repository. Never run a broad cleanup that
  could touch source, manuals, saves, research copies, or unrelated user files.
- Report the logical bytes reclaimed.

## Git

- Work directly on `master` and push to `origin/master`
  (`git@github.com:noeldvictor/azahar-thor-experiment.git`) in small, verified slices.
- Use command-line Git over SSH. No PR automation, no GitHub CLI, unless asked.
- Never commit generated Gradle, CMake, or APK output.

## Building and testing on the Thor

- APK for the device:
  `.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite --no-configuration-cache`
  from `src/android`, then install
  `app/build/outputs/apk/vanilla/relWithDebInfoLite/app-vanilla-relWithDebInfoLite.apk`.
- Do not pass `--configuration-cache`; Gradle rejects the Git calls in
  `app/build.gradle.kts` while storing it.
- Always pass `adb -s <serial>`; the Thor can enumerate over both USB and Wi-Fi.
- Read and restore the user's performance mode, fan mode, brightness, GPU driver,
  and resolution after any experiment. Do not silently change device settings.
