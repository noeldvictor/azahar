# Android Game Profiles

These source-controlled manifests document Android/Thor game-specific profiles that are applied in native code.

Target hardware is AYN Thor Base/Pro/Max: Snapdragon 8 Gen 2 and Adreno 740. Do not use Thor Lite / Snapdragon 865 behavior as the default profile target unless it is explicitly documented.

Keep these manifests in sync with any hardcoded native profile logic and `docs/thor-optimization-notes.md`.

- `0004000000053700.ini` - E.X. Troopers: 2x max resolution, custom textures off, normal frame limit, and the texture-copy fallback skip enabled for smoother Thor play.
