# Tools

This directory contains several scripts which are intended to both document and ease the convenience of certain development processes.

The scripts in this directory assume that your current working directory is the Azahar root directory and the script is being called via `./tools/xxx.sh`.

## Thor Fork Note

This README is mostly inherited from upstream Azahar. It is not the release flow for Azahar Thor Experiment.

For this fork, Android/Thor builds use:

```powershell
cd src/android
.\gradlew.bat :app:assembleVanillaRelWithDebInfoLite
```

Do not treat the Google Play, Flathub, Internet Archive, or RetroArch checklist below as active work for this personal Thor fork unless the user explicitly asks.

## Upstream Release Checklist

The upstream release checklist was removed from this fork-facing README because it is obsolete for Azahar Thor Experiment. This fork does not publish Google Play, Flathub, Internet Archive, compatibility-list, translation, or RetroArch releases.

### Note:

For reasons unknown, some part of the translation update process can inexplicably produce files with different content depending on the environment in which it is running, even when using the same version of the tool and the same distro.

For consistency, when updating the translations to be committed to the repository, always perform the update within the the Docker environment we use for Azahar's CI (`opensauce04/azahar-build-environment:latest`).
