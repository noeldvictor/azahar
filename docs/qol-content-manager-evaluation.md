# Android Game Extras: Cheat and Texture-Pack Evaluation

Date: 2026-08-16

## Recommendation

Build one per-game **Game Extras** screen with separate Cheats and Texture Packs sections. Start with a conservative cheat updater and a validated manual texture-ZIP installer. Add a remote texture catalog only after deciding which pack sources and licenses the fork is willing to maintain.

The project already has most of the plumbing:

- a native GateShark cheat engine, Android editor, per-title cheat files, library badges, and seven bundled personal presets;
- custom-texture discovery at `load/textures/<16-digit title ID>/`, `pack.json`, PNG/DDS decoding, preload, and asynchronous loading;
- a GPU-driver downloader that demonstrates GitHub release discovery, progress-capable background work, manual ZIP fallback, metadata validation, and failed-download cleanup;
- a ZIP extractor with canonical-path traversal protection.

The missing pieces are remote-content trust, version/region matching, non-destructive updates, Storage Access Framework access to `load/`, archive limits, install receipts, rollback, and an appropriate UI.

## Cheat updater: good first feature

Use a fork-controlled, versioned JSON catalog that points to exact upstream revisions. The catalog entry should contain title ID, region, supported game/update revision, source URL and commit, license/attribution, SHA-256, byte size, and an optional Thor-tested status. Fetch only the entry matching the installed title ID; never download or scan an entire database on every refresh.

The initial source should be FlagBrew/Sharkive because it publishes a GPL-3.0 license and explicitly warns that entries are community-submitted and often unverified. JourneyOver/CTRPF-AR-CHEAT-CODES is a useful secondary source, but its repository warning says codes may not work and its reuse terms need to be confirmed before the app republishes its data.

Safe update flow:

1. Download to app cache over HTTPS with timeouts and a strict size limit.
2. Verify the catalog hash, UTF-8 text, exact title ID, section structure, and every GateShark code line with the existing parser rules.
3. Show source, revision, region/game-version warning, and a diff/preview.
4. Back up the current file and preserve it when the user cancels.
5. Install all downloaded cheats disabled by default. Never silently preserve an enabled flag across a changed code body.
6. Record a receipt containing source revision and installed hash so a later update can distinguish an untouched download from a user-edited file.
7. If the file was edited, offer **Keep mine**, **Replace with backup**, or **Import new entries**. Do not use the current byte-size replacement heuristic for network updates.

An automatic scheduled updater is not recommended. Cheat availability changes slowly, an update can be game-version-specific or unsafe, and polling would add needless wakeups. A user-initiated **Check for updates** action is a better power and safety tradeoff.

## Texture-pack installer: useful, but higher risk

Phase one should install a user-selected ZIP. The current Android document-tree whitelist does not expose `load/`, and the existing safe unzip helper writes only to internal `File` storage, so the implementation needs a purpose-built SAF installer rather than reusing the driver installer unchanged.

Validation requirements:

- stage the download/import before touching the live directory;
- enforce maximum compressed size, maximum expanded bytes, expansion ratio, file count, path length, and directory depth;
- reject absolute paths, `..`, symlinks, duplicate/case-colliding paths, and anything escaping the staging root;
- accept only expected texture/config formats such as PNG, DDS, and `pack.json`;
- identify a single pack root and require either a manifest title ID or an explicit user confirmation of the destination title;
- validate texture filenames and `pack.json` options using the same rules as `CustomTexManager` where practical;
- compute SHA-256 and show author, license, source, archive size, and expanded install size;
- move staged files into `load/textures/<TITLE_ID>/` only after validation, preserving a rollback backup;
- keep an install receipt and remove only receipt-owned files during uninstall so hand-added textures survive.

Downloads should use WorkManager with foreground progress, cancellation, `.partial` cleanup, Wi-Fi-only and charging options for large packs, and a free-space check before extraction. A catalog entry additionally needs pack ID/version, all supported regional title IDs, game/update revision, archive URL, SHA-256, compressed/expanded sizes, author/source/license, expected root layout, and known compatibility notes.

Do not scrape arbitrary web results or mirror packs without permission. A small curated catalog of creator-approved GitHub Releases is maintainable; a general search engine is not. Stale links, unclear ownership, region mismatches, and multi-gigabyte archives make an open downloader unsafe.

Installing a pack must not automatically enable custom textures. Upstream has current reports of Android/Thor crashes with custom textures and of cold asynchronous-loading bursts causing Vulkan instability. HD textures can also increase storage reads, decode work, RAM pressure, GPU uploads, bandwidth, and power. The first-run guidance should therefore leave preload off, disclose the expected memory/storage cost, require a game restart, and offer a one-tap disable/rollback path.

## Suggested order

| Priority | Feature | Value | Risk/effort |
| --- | --- | --- | --- |
| P0 | Per-title cheat update check, preview, backup, receipt, rollback | High | Low-medium |
| P0 | Game Extras landing page with installed/source/version status | High | Low |
| P1 | Validated manual texture-ZIP install and receipt-based uninstall | High | Medium |
| P1 | Storage estimator and cleanup for packs, shader cache, and dumped textures | High on handheld | Medium |
| P1 | Export a small Thor benchmark bundle: settings, driver, FPS/frametime, temperature, and logs | High for performance work | Medium |
| P2 | Creator-approved curated texture catalog/downloader | High | High ongoing maintenance |
| Reject | Silent cheat replacement, auto-enabled cheats, open-web pack scraping, or unattended polling | Negative | Safety, licensing, and power cost |

## Product choice required before implementation

Choose whether Game Extras is:

1. a **curated Azahar Thor catalog** with a small allowlist, hashes, compatibility notes, and stronger trust; or
2. a **user-supplied catalog URL** system with broader reach but more warnings and no implied curation.

The recommended first implementation is the curated model, with manual ZIP import always available.
