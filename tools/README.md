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

## Thor battery-power gate

`measure-thor-power.ps1` samples the AYN Thor's battery power and temperature over Wi-Fi ADB. It is
read-only apart from optional temporary screenshots, which it removes from the device. Run its
built-in deterministic checks before using it:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\measure-thor-power.ps1 -SelfTest
```

For the accepted 7th Dragon scene, physically unplug the Thor, select device-wide Standard mode,
leave fan mode 4 and a fixed recorded brightness, launch the production Lite APK into the exact
scene, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\measure-thor-power.ps1 `
  -ExpectedBrightness <recorded-value> `
  -ExpectedScreenshotSha256 E831B2637B609C064C21C0E7531D74DC30ADC5EB3F344466C43D6BF750A3F13C
```

Defaults provide a 60-second warmup followed by 180 seconds of one-second samples. The gate passes
only when both mean and nearest-rank P95 battery power are at most 6 W. It records raw current,
voltage, direct and averaged power nodes, battery temperature/capacity, charger flags, package and
device metadata, config hash, and before/after screenshot hashes under `thor-power-results/`.

The script deliberately fails instead of producing a watt claim if the battery is simulated, any
external-power flag is present, a charger appears during sampling, the app exits, hardware is not an
AYN Thor, ADB is not using a host:port endpoint, the package is debuggable/non-ARM64, or the expected
version, config, performance mode, fan mode, brightness, or frame hash differs. Override an expected
value explicitly when validating a newer accepted build; do not weaken the charger or simulated-
battery checks.

## Upstream Release Checklist

The upstream release checklist was removed from this fork-facing README because it is obsolete for Azahar Thor Experiment. This fork does not publish Google Play, Flathub, Internet Archive, compatibility-list, translation, or RetroArch releases.

### Note:

For reasons unknown, some part of the translation update process can inexplicably produce files with different content depending on the environment in which it is running, even when using the same version of the tool and the same distro.

For consistency, when updating the translations to be committed to the repository, always perform the update within the the Docker environment we use for Azahar's CI (`opensauce04/azahar-build-environment:latest`).
