# Imported Thor Performance Research

This directory keeps the small, directly relevant research notes that informed Azahar Thor work. They are historical inputs from sibling personal emulator workspaces, not proof that a PS3 or Xbox 360 optimization applies to the 3DS emulator.

Imported on 2026-08-16:

| File | Original workspace | Why it is here | SHA-256 |
| --- | --- | --- | --- |
| `imported/20260601-arm64-adreno-speed-techniques.md` | `xenia-thor` at `5e6d5fe06409e5a166aac5579b321903e23d96cd` | Snapdragon 8 Gen 2 feature and Adreno/NEON applicability audit | `26396FF3058A00B8F75C46C0E728E2143933B08F114385EA3697DDC5A1448E55` |
| `imported/20260805-rpcs3-arm64-optimizations-applicable.md` | `xenia-thor` at `5e6d5fe06409e5a166aac5579b321903e23d96cd` | Mapping of current RPCS3 ARM64 techniques onto another SM8550 emulator | `3D83A10BEFEF4B2A8409D3DC0437C9C77D8FFBD03B9C122E0097AA6D93E0D374` |
| `imported/20260719-thor-sustainable-performance-research.md` | `rpcsx-ui-android` at `2d71453251a617dc9c74746e13b3129f485262dd` | Power, wakeup, thermal, and Android scheduling discipline | `EEBA117FCB4D542B88C3A78EA70FCB1BDBDF28150B4DB4DFF85586566EEA13F3` |
| `imported/20260805-arm64-upstream-perf-uplift.md` | `rpcsx-ui-android` at `2d71453251a617dc9c74746e13b3129f485262dd` | Chapter-by-chapter notes for Whatcookie's ARM64 technical video | `3D646E94227876B80F5BF4589F553837FA26587387A28BBFDA82F3C85F40048E` |

The Azahar-specific conclusions and rejected ports live in [`../thor-optimization-notes.md`](../thor-optimization-notes.md). Re-run on-device benchmarks before promoting any historical observation to a current claim.
