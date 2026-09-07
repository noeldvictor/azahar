# CLAUDE.md

**[AGENTS.md](AGENTS.md) is the authoritative engineering ledger for this fork. Read it before
changing code, and update it when behavior changes.** It holds every accepted optimization, every
rejected experiment and the reason it was rejected, and the invariants that must not be silently
"cleaned up". This file only adds the operating rules for working here; it does not restate them.

## Documentation map

- [AGENTS.md](AGENTS.md) — engineering rules and invariants. Canonical.
- [docs/thor-optimization-notes.md](docs/thor-optimization-notes.md) — dated evidence behind them.
- [docs/thor-cheat-gaps.md](docs/thor-cheat-gaps.md) — cheat coverage gaps.
- [README.md](README.md) — public-facing description of the fork.
- [AI-POLICY.md](AI-POLICY.md) — how AI assistance is used here.

When behavior changes, update the rule in AGENTS.md and the evidence in the notes. Do not copy
engineering detail into README.md or this file; point at AGENTS.md instead.

## Finishing a task

Cleanup is part of finishing, not a separate request. Before handing work back, remove stale CMake
configuration hashes, Gradle intermediates, and scratch artifacts you created — in the repository
and on the device — then report the bytes reclaimed. AGENTS.md carries the exact storage rules and
the paths that are safe to touch. Never run a broad sweep that could reach source, saves, manuals,
research copies, or unrelated user files.

## Working agreements

- Work directly on `master` and push to `origin/master` in small, verified slices. Command-line Git
  over SSH; no PR automation or GitHub CLI unless asked.
- Never commit generated Gradle, CMake, or APK output.
- Build the device APK with
  `.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite --no-configuration-cache` from `src/android`.
  Do not pass `--configuration-cache`.
- Always pass `adb -s <serial>`; the Thor can enumerate over both USB and Wi-Fi.
- Read and restore the user's performance mode, fan mode, brightness, GPU driver, and resolution
  after any experiment. Do not silently change device settings.
- Separate what was measured from what it proves. An isolated ratio is not an FPS or battery-watt
  claim.
