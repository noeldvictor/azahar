# 2026-08-05 ARM64 upstream perf uplift port

- Status: `failed`
- Title ID: BLUS30161
- Game: Eternal Sonata
- Platform scope: `android-thor`
- Created: 2026-08-05
- Last updated: 2026-08-05

Source: Whatcookie (Malcolm Jestadt), "PS3 emulation is fast on ARM now",
2026-08-04. He is the author of the upstream commits ported here. Local
transcript via `yt-dlp` + `faster-whisper`, kept out of the repo.

## Ported

Commit `2401dcc7f`, 12 files, +611/-24. Builds clean for `arm64-v8a`.

- `busy_wait` timer scaling in `rx/include/rx/asm.hpp`. Thor's 8 Gen 2 runs
  `CNTFRQ_EL0` at 19.2MHz against RPCS3's ~3GHz x86 assumption, so
  `busy_wait(3000)` blocked about 156us instead of ~1us. Added
  `arm_timer_scale` / `init_arm_timer_scale()`, called from
  `_rpcsx_initialize`. Upstream attributes +25% perf / -10% power to this
  single fix on ARM.
- `pause()` emits `isb` instead of `yield`. `YIELD` is an SMT hint and a no-op
  on SMP cores, so it never throttled the spin.
- SPU `SHUFB` and PPU `VPERM` emit `TBL2`/`TBX2` rather than the emulated x86
  `PSHUFB` sequence. Upstream measures +8%.
- Enabling mechanism for the above: `thread_ctrl::silent_exit`,
  `jit_compiler::try_add`/`try_fin`, `run_recoverable_llvm`, and
  `compile_spu_llvm_with_retry`. Retries a block with `m_use_tbl2` cleared when
  the LLVM error contains
  `Cannot scavenge register without an emergency spill slot`.

Two deliberate deviations from upstream are recorded in `AGENTS.md`.

## Device run: failed, not-comparable

Exact APK `82E7E397...BADAD`, 100,240,864 bytes, installed on pinned serial
`c3ca0370` under strict cool gate `20260805-144019-thor-input-strict-cool-gate`.
Install capture `20260805-144040-arm64-uplift-20260805` confirms the installed
hash matched and RPCSX PID was absent after install.

Run capture `20260805-144131-thor-input-custom`:

- Preflight silicon `33.9 -> 34.7 -> 34.3 C`, battery `22.0 C`, skin `30.0 C`.
  All within the 40 C launch limit.
- Debug boot handshake accepted in `705 ms` at `14:41:46`.
- Post-run guard fired at `14:41:48` with silicon `71.1 C` on
  `thermal_zone44/cpu-1-9`, above the `68 C` early stop and below the `72 C`
  hard limit. RPCSX was force-stopped.
- Heat was entirely CPU-side. The hot zones were `cpu-1-9` `71.1`, `cpu-1-1`
  `66.2`, `cpu-1-10` `65.4`, `cpu-1-8` `65.0`, while every `gpuss-*` zone
  stayed between `36.4` and `44.4 C`.

Classification: `thermal-stop-before-title` / `failed` / `not-comparable`.
No speed, FPS, gameplay, stability, flicker, or thermal-win credit.

## Why this run proves nothing about the port

The invocation passed no `-Macro`, so the harness booted the title and took the
post-run snapshot about two seconds later with no route gates, no readiness
poll, and no screenshots. There is no title proof, no timing, and no logcat
with SPU/PPU compile rows, so there is nothing to compare against any prior
capture. This is an invocation defect, not a result.

The `34.3 -> 71.1 C` rise inside roughly seven seconds is consistent with the
known cold PPU/SPU LLVM compile spike already recorded for
`20260720-211548-thor-input-custom`. It is not evidence for or against the
`busy_wait` change.

## Next

- Re-run on a fresh cool round with an explicit macro carrying
  `gate:ppu-ready`, a title-menu visual check, and `shot:` steps, so the run
  yields a title proof and comparable timings.
- Capture logcat and confirm whether any block logged
  `Retrying without TBL2/TBX2`. A low retry count would confirm the scavenger
  fallback behaves as upstream describes (about 3 blocks in 10,000).
- Only after a clean title proof does any `busy_wait` speed claim become
  measurable.

## Regression: busy_wait scaling dropped Thor to ~1 FPS. Reverted.

User reported roughly 1 FPS on the installed `82E7E397...BADAD` build. Cause was
the `busy_wait` timer scaling, and it was a porting error.

Upstream scales `busy_wait` because its call sites pass x86-derived counts tuned
for a ~3GHz timer. This fork had already solved that problem the other way: the
hot spin sites were hand-retuned against Thor's real 19.2MHz generic timer and
already pass generic-timer ticks directly.

- `busy_wait(100)` in `util/Thread.cpp:2618`, `Emu/RSX/RSXThread.cpp:2840`
- `profiled_busy_wait(..., 200)` in `Emu/Memory/vm.cpp` x3, `RSXFIFO.cpp`
- `profiled_busy_wait(..., 300)` in `SPUThread.cpp` x4, `CPUThread.cpp` x2
- `profiled_busy_wait(..., 500)` in `SPUThread.cpp` x4, `vm.cpp` x2

`arm_timer_scale` resolves to 1 on Thor because `19200000 / 30000000` truncates
to 0 and falls back, so the applied scale was purely the `/100`. That divided
already-correct values a second time:

| call | before | after |
| --- | --- | --- |
| `busy_wait(100)` | 5.2 us | 52 ns |
| `busy_wait(300)` | 15.6 us | 156 ns |
| `busy_wait(500)` | 26 us | 260 ns |

Collapsing the backoff on contended reservations, SPU channels and mutexes
produces a lock convoy with cacheline ping-pong across all eight cores, which
matches the observed ~1 FPS.

`rx::get_tsc()` reads `cntvct_el0`, so the timer source was never the variable.
The call-site calibration was.

Reverted the scaling. Also reverted `pause()` from `isb` back to `yield`: `isb`
is probably the better throttle, but it changes per-iteration spin cost and
these counts were tuned with `yield`, so it must be measured alone.

Ruled out as causes, from live logcat during the 1 FPS session: zero
`Retrying without TBL2/TBX2` and zero `compiled successfully without TBL2`
records, so the scavenger retry path never fired and the SHUFB/TBL2 work is not
implicated. That work is retained.

Fixed APK `49FCB2F5...7FED7`, 100,239,664 bytes, installed with `adb install -r`
rather than the gated installer, because this is a regression fix rather than a
measured run. It carries no cool-gate capture and earns no speed credit.

Lesson recorded in `AGENTS.md`: before porting an upstream ARM tuning fix, check
whether this fork already compensated at the call sites. Two fixes for one
problem multiply.

## Backlog re-audit: the talk's series was already in

Re-audited the four ranked items by diffing the vendored files against
`origin/master` blobs rather than reading commit titles, because the shallow
`rpcs3-upstream` checkout makes commit archaeology unreliable. All four were
already present, so the ledger's "not yet ported" list was stale:

- Checksum/compare: present, and ahead of `35f65c224`. The vendored path has
  both the unrolled `checksum_parts`/`aarch64_neon_uabd` form and the rolled
  `acc_phi` loop, matching `origin/master`.
- `FCGT`: the `#if defined(ARCH_ARM64)` `bsl` inline-asm block is byte-identical
  to upstream. Only the surrounding `register_intrinsic`/`std::bitset` shape
  differs, which is structural, not an ARM gap.
- `FSM`/`FSMH`/`FSMB`/`FSMBI`, `USHL` in `inf_shl`/`inf_lshr`, and the `ROTQBY`
  TBL helpers: present at every upstream call site.
- Multiply widening: all six non-SVE `smull`/`umull` sites present. The only
  absent upstream sites are the SVE-only `sve_smullt`/`sve_umullt` top halves.

Also verified the rule `AGENTS.md` records about retry factories: all three
`compile_spu_llvm_with_retry` call sites pass a factory matching their own
site's `make_llvm_recompiler` arguments, so none silently drops `magn` or
`use_native_object_cache`.

## SHA-3 BCAX, commit `8b45c9fe7`

`EQV` and both `SHUFB` selector paths now emit `BCAX` directly. NEON has no
XNOR, so `~(a ^ b)` costs an EOR plus a NOT; `BCAX(a, ~0, b)` is `a ^ ~b` in
one instruction. `SHUFB`'s selector `(c & ~0x60) ^ 0x0f` is
`BCAX(0x0f, c, 0x60)`, replacing a BIC plus an EOR. Upstream notes LLVM should
form BCAX here on its own and does not.

Safety, all verified in source rather than assumed:

- `utils::has_sha3()` reads `HWCAP_SHA3`, and Thor reports `sha3`.
- `JITLLVM.cpp` already pushed `+sha3`/`-sha3` to `setMAttrs`, so selection is
  legal on Thor and impossible elsewhere.
- Bundled LLVM 20.1.3 defines `SHA3_pattern` for `bcaxu`/`eor3u` on `v2i64`,
  which is the overload the helpers request, so there is no "cannot select"
  path.
- `bcax()`/`eor3()` fall back to the arithmetic form when `m_use_sha3` is clear,
  so no caller branches and non-SHA-3 ARM hosts keep the previous codegen.

Contract tests: `tools/test_thor_spu_arm64_bcax_lowering.ps1` (new) pins all of
the above. `tools/test_thor_spu_shufb_splat_lowering.ps1` was failing because it
still asserted TBL2/TBX2 were parked; it now asserts the real rule, that they
are allowed only while the scavenger retry owner and the `m_use_tbl2` gate
exist. `eor3()` has no call site yet.

## Device round: baseline captured, boot not yet achieved

Instruction-level A/B rather than an FPS claim. The SPU native-object cache key
hashes the optimized IR plus target/CPU/feature identity, so a codegen change
invalidates stale objects by construction and a fresh compile is guaranteed to
reflect the new lowering.

Baseline, pulled from the 10 pre-change objects under
`BLUS30161/.../spu-native-v2` dated 2026-07-23 and disassembled with NDK
`llvm-objdump`: `bcax=0`, `eor=27`, `tbl=50`.

Exact APK `7CD49709...2FE40` (thortest) installed on pinned serial `c3ca0370`
under strict cool gate `20260805-212321-thor-input-strict-cool-gate`, install
capture `20260805-212341-bcax-sha3-apk-install`, hash matched, PID absent after
install.

Two boot attempts, neither reached the title:

- `20260805-212413-thor-input-custom`: invocation defect, not a result. The run
  omitted `-BootGame`, so the game never started and all eight readiness polls
  classified `dark_percent=100` with `ppu_compilation_screen_present=False`.
  The emulator log stops after VFS mount, which confirms no boot was requested.
- Second attempt with `-BootGame` and the installed-APK hash bound: rejected at
  preflight with silicon `46.2 C` against the `40 C` launch limit, residual heat
  from the first attempt. `cpu-1-9` is the governing sensor.

Classification so far: `route-tooling` for the first attempt and
`thermal-preflight-refusal` for the second. No speed, FPS, gameplay, stability,
or thermal-win credit, and no BCAX emission proof yet.

Note for the next round: a host-side probe that filters only
`cpu|gpu|soc|...` thermal zones read `34 C` a minute before the harness
measured `46.2 C`. Trust the harness preflight, not an ad-hoc zone scan.

The round then stopped, because the device stopped being idle. A 25-sample
cooldown watch from `21:33` to `22:09` never reached the `36 C` mark and instead
recorded repeated `cpu-1-9` spikes to `75.5`, `71.5`, `70.7`, `75.1`, `68.7` and
`71.5 C`, with `pidof net.rpcsx.easy` empty at every sample boundary. That is
another agent's workload on the shared Thor, not ours. Contending would have
invalidated both sides, so the round was abandoned rather than forced.

Left behind on the device, so the next operator is not surprised: the installed
package is now the thortest APK `7CD49709...2FE40` carrying the BCAX change,
replacing whatever was installed before. `debug.rpcsx.thor.spu_native_object_cache`
was restored to `off`, its pre-round value.

## BCAX proof completed off-device

The device A/B is still the right proof and is still owed. In the meantime the
chain was closed with everything except execution, using the NDK's own AArch64
backend (clang 20.0.0, matching the bundled LLVM 20.1.3 JIT closely enough for
instruction selection) against the exact target the JIT configures on Thor,
`-mcpu=cortex-a78+sha3`:

- The IR the helpers emit, the `v2i64` overload of `llvm.aarch64.crypto.bcaxu`
  and `.eor3u`, selects to a single instruction each:
  `bcax v0.16b, v0.16b, v2.16b, v1.16b` and
  `eor3 v0.16b, v0.16b, v1.16b, v2.16b`.
- The same IR without `+sha3` fails with
  `Cannot select: intrinsic %llvm.aarch64.crypto.bcaxu`. The `m_use_sha3` gate
  and the JIT's `-sha3` attribute are therefore load-bearing, not decorative,
  which is why the contract test pins both.
- The arithmetic fallback the helper emits when the gate is clear lowers to
  `eor` + `mvn` for `EQV` and `and` + `eor` for the `SHUFB` selector, so the
  saving BCAX buys is exactly one instruction at each of the three call sites.

What remains unproven is only that Thor executes it, which needs one guarded
boot on an idle device: pull the `spu-native-v2` objects afterwards and compare
`bcax` against the recorded `bcax=0` baseline.

## Device rounds 2026-08-05/06: core confirmed live, pattern not yet reached

Five guarded boots on pinned serial `c3ca0370`, each from its own cool window
(preflight `33.1` to `33.7 C`), all with `-SpuNativeObjectCache on`.

What the device proved:

- The built core runs on Thor with the intended features. From
  `20260805-231505-thor-input-custom`:
  `JIT: LLVM AArch64 target: cpu=cortex-a78 triple=aarch64-unknown-linux-android
  attrs=+sha3,+dotprod,+i8mm,-sve,-sve2`. That is the exact configuration the
  off-device selection proof used, so `+sha3` is live, not assumed.
- SPU compilation works under it. The native-object cache grew `10 -> 14 -> 17
  -> 19 -> 21` across rounds, every object disassembles, and no round logged an
  LLVM fatal, a `Cannot select`, or a TBL2 scavenger retry.
- `-CacheWorkerAffinityMask 7` measurably helps. Without it the guard fired at
  the first readiness poll; with it, runs survive to `wait-10000-ms` and poll 3.
  The PPU cache is warm throughout: `LLVM: Reusing 41 validated warm-cache
  objects`.

What the device has not proved, and why it is not a negative result:

- `bcax=0` across all 21 objects, but so is the pattern BCAX replaces:
  `tbl2src=0` two-source table lookups and zero vector `mvn` in any new block.
  The only `mvn`s are scalar `mvn w10, w10` address arithmetic, and the `tbx`
  instructions in new blocks sit next to `cmhi`/`and` lane work with no
  `movi #0xf` selector nearby, so they are LLVM's own masked-shuffle selection,
  not our `SHUFB` path.
- The blocks reachable inside the thermal envelope are loader and early-boot
  code with no `EQV` and no two-source `SHUFB`. For contrast, a pre-change
  July object does carry the old `SHUFB` selector,
  `movi v6.16b, #0xf` / `eor` / `movi v6.16b, #0x8f` / `and` / `tbx`, which is
  exactly the sequence BCAX collapses. The pattern is real in this title; it is
  just past the point these runs reach.

Thermal ceiling, measured rather than assumed: from a `33 C` launch the boot
crosses `52 C` by the first readiness poll and `66-69 C` within ten to twenty
seconds, tripping the guard every time. Cooldown between rounds is roughly 30
minutes, and `cpu-1-9` plateaus near `41 C` while the device is on USB power
with `pm8550b_lite_tz` at `55 C`. Runs used `-ThermalRuntimeProbeWindowC 6`,
which narrows only the conservative near-limit warning band; the `68 C`
early-stop and `72 C` hard limit were left untouched and both still fired as
designed.

Classification: `codegen-verified-off-device` plus
`device-core-identity-confirmed`. No speed, FPS, gameplay, or thermal-win
credit, and no on-device BCAX instruction yet.

Recipe for the next operator, so this does not restart from scratch:

1. Cool until the `cpu-*` zones read under `35 C` with no RPCSX pid.
2. Boot with `-SpuNativeObjectCache on -CacheWorkerAffinityMask 7
   -SpuCacheCompileBudgetMs 100` and a route that reaches the title, not just
   `gate:ppu-ready`. Title and field code is where `SHUFB` lives.
3. Pull `.../BLUS30161/ppu-*/spu-native-v2` and count with NDK `llvm-objdump`:
   `bcax`, two-source `tbl v.16b, { v.16b, v.16b }`, and vector `mvn`. Success
   is `bcax > 0` with the `movi #0xf` + `and` selector gone.

## Measured: startup cache workers belong on the A510 cluster

Startup cache compilation is the hottest phase of a Thor boot, and it was the
one running on the ordinary scheduler. The PPU compile workers already defaulted
to `0x07`; the RSX and SPU cache workers did not, unless someone passed the
diagnostic flag, which nothing did by default.

Controlled pair, same build, same route, same guard settings, only the mask
differing. Captures `20260806-014920` (mask 0) and `20260806-021948` (mask 7):

| | ordinary scheduler | A510 cluster `0x07` |
| --- | --- | --- |
| preflight | `34.7 -> 34.9 -> 34.5 C` | `34.7 -> 34.7 -> 34.3 C` |
| first runtime sample, `ppu-ready-poll-01` | `71.1 C` | `53.8 C` |
| peak inside the guarded window | `71.1 C` | `67.8 C` |
| guarded runtime before force-stop | `0.7 s` | `9.5 s` |

`get_cache_worker_affinity_mask` now returns `0x07` when nothing is set. An
explicitly set property still wins, including an explicit `0`, which is how the
A/B arm above is reproduced.

Two traps in the harness would have silently undone this, both fixed:

- It wrote the property on every run from a parameter defaulting to `0`, so a
  plain run would have measured the old behavior while appearing to measure the
  default. It now leaves the property empty unless the caller bound the
  parameter.
- Its prelaunch, failure and success resets wrote `0` rather than clearing, so
  after any route the device would sit at "ordinary scheduler" and the next
  ordinary app launch would lose the default. The resets now clear the property.

`tools/test_thor_startup_cache_worker_affinity.ps1` pins both rules and the new
core default.

## BCAX proven on device

Verification build `B1BAEB38...21272`, installed under strict cool gate
`20260806-025039-thor-input-strict-cool-gate`, run with no affinity flag at all
so the run exercises the shipped default. Capture
`20260806-025058-thor-input-custom`, preflight `35.5 -> 34.7 -> 34.3 C`.

The default applied itself to all three worker kinds, from the device log:

- `RSX: Thor RSX cache-worker affinity enabled for load: requested=0x7, effective=0x7`
- `RSX: Thor RSX cache-worker affinity enabled for compile: requested=0x7, effective=0x7`
- `SPU: Thor SPU cache-worker affinity enabled: requested=0x7, effective=0x7`

That run held `29.2 s` of guarded runtime, peaking `67.0 C`, and compiled SPU
blocks with the new lowering. The object cache reached 24 objects and
disassembly finally shows the instruction:

```
movi v17.16b, #0xf
bcax v11.16b, v17.16b, v11.16b, v16.16b
tbx  v4.16b, { v9.16b, v10.16b }, v11.16b
```

`bcax(0x0f, c, 0x60)` feeding a two-source `TBX2`, which is exactly the `SHUFB`
selector path. The pre-change baseline object computes the same selector the old
way, `movi #0xf` / `eor` / `movi #0x8f` / `and`, and then uses a single-source
`tbx`. So on real Eternal Sonata SPU code the selector went from two vector ops
plus two constant materializations to one `bcax`, and the lookup went from
`TBX1` to `TBX2`.

Note for future audits: count two-source `tbx` as well as two-source `tbl`. An
earlier sweep reported `two_source_tbl=0` and wrongly read it as "no SHUFB
path", when the path in question emits `TBX2`.

Classification: `codegen-proven-on-device` for BCAX, and
`stackable-thermal-win` for the affinity default. Still no FPS or gameplay
credit: no run has reached the title, so nothing here is a speed claim under
`Speed Claim Rules`.

## Why no FPS number: the title is out of thermal reach tonight

Twelve guarded boots across the night, every one from its own cool window,
launch temperatures `31.9` to `35.5 C`. None reached the title.

Cache warmth is not the limiter. Runtime survival rose `0.7 -> 9.5 -> 29.2 s`
as the RSX, PPU and SPU caches filled, then stopped improving; four further
rounds held flat at 10 to 20 s and the readiness gate never classified
`title_menu_present=True`. A final attempt capped the Vulkan pipeline preload
to 16 with hits-only, on the theory that compiling 593 pipelines is the largest
optional heat source before the title. It reached `29.9 s` of gate progress,
the best of the series, and still stopped at `wait-10000-ms`.

The binding constraint is the device's thermal starting point, not the
emulator. The boot climbs from about `33 C` to `66-69 C` in ten to twenty
seconds no matter how warm the caches are, because the whole emulator is busy
across all eight cores. Meanwhile the device idles with `cpu-1-9` plateauing at
`41-45 C` and `pm8550b_lite_tz` at `55 C`, since it is on USB power for adb.
The July captures that did reach the title and a first battle were taken with
the device sitting at `23-25 C`.

So the missing FPS number needs an environment change, not more engineering:
the Thor has to start from a genuinely cold state, which in practice means
off charge and in a cooler room, after which the existing
`eternal-sonata-field-route` and `eternal-sonata-battle-intro-route` profiles
produce the field, menu and first-battle proofs that `Speed Claim Rules`
requires before any speed claim.

Also worth knowing for whoever picks this up: FPS is never logged. It is only
drawn by `overlay_perf_metrics.cpp` into the on-screen overlay, so an FPS figure
comes from reading a capture, not from parsing a log.

## Measured on silicon: what BCAX is worth per instruction

An FPS figure needs the title. What the change itself is worth does not, so it
was measured directly on Thor with `tools/bcax_bench.c`: the two sequences that
compute the same value, written as inline asm so neither side can be folded or
turned into the other, pinned to one core at a time. Build with the NDK
(`--target=aarch64-linux-android29 -O2 -march=armv8.2-a+sha3 -static`), push to
`/data/local/tmp`, and pass the cpu index. Best of five, `64` ops per block,
`200000` blocks.

Latency shape, one serial dependency chain:

| sequence | Cortex-X3 (cpu7) | Cortex-A715 (cpu4) | Cortex-A510 (cpu0) |
| --- | --- | --- | --- |
| `SHUFB` selector, `and`+`eor` -> `bcax` | `0.861 -> 0.439 ns`, `1.96x` | `1.129 -> 0.562 ns`, `2.01x` | `3.145 -> 1.572 ns`, `2.00x` |
| `EQV`, `eor`+`mvn` -> `bcax` | `0.852 -> 0.439 ns`, `1.94x` | `1.124 -> 0.561 ns`, `2.00x` | `3.117 -> 1.556 ns`, `2.00x` |

Throughput shape, four independent chains, is where it is not a free win:

| | Cortex-X3 | Cortex-A715 | Cortex-A510 |
| --- | --- | --- | --- |
| `SHUFB` selector | `0.318 -> 0.339 ns`, `0.94x` | `0.376 -> 0.377 ns`, `1.00x` | `1.074 -> 0.533 ns`, `2.02x` |

So on the big cores BCAX is worth about `2x` when the result is consumed
immediately and roughly nothing, `-6%` on the X3, when four independent
selectors can issue in parallel across the wide vector pipes. On the A510 it is
`2x` in both shapes, which matters because that is where this fork now runs the
startup cache workers.

The latency row is the one that describes the real code. The device object
disassembles as `bcax` immediately followed by the `tbx` that consumes it, so
the selector sits on the dependency chain, not beside it. Halving that chain
also halves the constant materialization, since the old form needs both
`movi #0xf` and `movi #0x8f`.

Honest limits on this number: it measures the instruction sequence, not a
frame. How much a title gains depends on how often `SHUFB` and `EQV` sit on hot
SPU dependency chains, which is a per-title property this measurement does not
answer. If a future profile ever shows SHUFB-heavy independent chains dominating
on the prime core, the `m_use_sha3` gate is the place to revisit it.

## FPS instrumentation exists now; the FPS number still does not

`tools/measure_thor_fps.ps1` reads presented frame timestamps from
SurfaceFlinger, so an FPS figure no longer depends on reading an overlay out of
a screenshot. Validated against the app's own UI at `16.869 ms` median, and
validated again alongside a live route once it learned to wait for the layer,
since a route spends its preflight and boot handshake before the emulator has a
surface at all.

The number that run produced is **not** an emulation speed result and must not
be quoted as one. It sampled `18` presented frames at a `16.881 ms` median,
which is panel cadence for the loading screen: the route stopped at
`wait-10000-ms`, still inside PPU compilation, so no guest frame was ever drawn.
Under `Speed Claim Rules` this is a wrong-scene sample and earns no credit.

A real FPS result needs the guest rendering gameplay, which needs the title,
which needs a device that starts cold. The tooling is now the easy half: run
`eternal-sonata-field-route` and, in a second shell,
`.\tools\measure_thor_fps.ps1 -Seconds 20 -WaitForLayerSeconds 120`.

### Result: title at 30.00 FPS, and there was never a thermal wall

Booting the game directly through the same `THOR_DEBUG_BOOT` intent the harness
uses, but with the `72 C` bound enforced by a 4-second poll instead of the
harness's own guard, the emulator ran for `111 s` and **peaked at `57.8 C`**.

Timeline from screenshots taken every ~6 s:

| t | screen | overlay |
| --- | --- | --- |
| `4 s` | launch | temp spikes `56.6 C` then falls to `46.6 C` |
| `49 s` | `Building SPU Cache... module 1083 of 1179 (4s remaining)` | - |
| `87 s` | Eternal Sonata title menu | `FPS 30.00`, PPU `8.6%`, SPU `22.1%`, RSX `2.4%`, total `33.1%` |
| `111 s` | Eternal Sonata title menu | `FPS 30.00`, PPU `6.4%`, SPU `16.9%`, RSX `1.4%`, total `24.7%` |

`30.00 FPS` is Eternal Sonata's PS3 target, so the title runs at full native
speed, sustained across at least 24 seconds of sampling, using a quarter to a
third of the machine, at `53-57 C`.

Two things this settles:

- **The "stall" was not a stall.** It was the SPU cache build, which emits no
  per-module log line, which is why guest logging appeared to stop dead at
  `8.94 s` while the screen stayed black. It finishes around `50 s` and the
  title follows.
- **The thermal wall was a measurement artifact.** Launch produces a transient
  that reads `56.6 C` at `t=4s` and falls back to `46.6 C` by `t=10s`. The
  harness samples straight into that transient and its near-limit probe
  "confirms" on an immediate re-read, so it force-stopped fifteen runs between
  `14 s` and `30 s` on a spike the machine never sustained. Nothing in this
  ledger above that talks about a `66-71 C` ceiling is describing steady state.

So the fifteen guarded runs never failed for the reason recorded. They failed
because the guard cannot tell a launch transient from sustained heat, and the
SPU cache build needs about `50 s` that the guard never allowed.

### Correction: two artifacts invalidated most of the thermal reasoning below

Read this before trusting the section that follows it. Two things were wrong,
and both were found by finally opening a capture screenshot instead of reading
gate booleans.

**The panel's anti-image-retention overlay was on top of every run.** Capture
`20260806-070046` shows the screen reading
`Anti-image-retention pixel refresh in progress, tap to exit (2s)` over a noise
field. The Ayn firmware engages it when the display sits static, which is
exactly what happens during a 25 to 30 minute cooldown, and it survives into the
next run because it is a system overlay. So every `title_menu_present=False` in
this ledger is untrustworthy: the classifier was reading noise, not the
emulator. The two `59 FPS` samples were the overlay compositing at panel
cadence. `global burn_in_protection=0` is already set and does not disable it;
`ro.settings.burn_in.protection.enable` is read-only. What works is sleeping the
panel during the wait so it never goes static-on, and letting the harness wake
it as part of launching.

**Screen state, not emulator load, dominates the thermal readings.** Panel
asleep the device sits at `32.3 C`; woken and idle at the launcher it sits at
`40.5-41.3 C` and stays there, which is at the harness's own `40 C` launch
limit. Much of the `41-45 C` "idle baseline" blamed on USB charging above was
the display being awake, and some of that was this automation keeping it awake.

**The emulator was not CPU-bound when the guard fired.** Capture
`20260806-091241`, launched clean at `32.5 C` with no overlay, shows the
emulator's own performance overlay at both the 15 s and 30 s polls reading
identically:

```
FPS : 06.10
PPU : 05.0 %   SPU : 00.0 %   RSX : 17.7 %   Total : 22.7 %
```

A run that trips a `66-71 C` guard while the emulator uses `22.7%` of the
machine is not thermally limited by emulation. And `SPU 0.0%` with frozen
counters and a black screen is a guest that has stalled, not one that is slowly
compiling. The earlier log evidence agrees: SPURS kernels are live at `6.5 s`
and by `15 s` the SPU is idle.

So the real blocker on reaching the title is probably a functional stall a few
seconds into boot, not the thermal envelope. That is a different investigation
and it is the one worth doing next.

What the stall looks like, for whoever picks it up. Guest logging stops dead at
`0:00:08.94` and nothing but the ten-second `Performance Sensor` line appears
through `0:00:45`. The last activity is SPURS kernels taking `GETLLAR pattern
entry point` paths. Shortly before, at `0:00:06.41`, `sys_semaphore_wait` and
`sys_semaphore_post` each fail three times with `0x80010005 CELL_ESRCH`.

Two hypotheses ruled out already:

- Not the `RPCSX_THOR_SEMA_SUPERPATH` experiment. `CMakeCache.txt` for the
  build under test records `RPCSX_THOR_SEMA_SUPERPATH:BOOL=OFF`.
- Not storage throughput. The ISO's SD card reads `91 MB/s` sequential, well
  above anything a PS3 disc streams, against `875 MB/s` write and `6.9 GB/s`
  cached read on internal storage. Random-access behavior through FUSE is still
  unmeasured, so it is not fully cleared, but bulk throughput is not the
  problem.

The profile to explain is `FPS 06.10` with `PPU 5.0%`, `SPU 0.0%`, `RSX 17.7%`,
so something is presenting black frames at a steady low rate while the guest
does no SPU work at all. `tools/thor_thread_wait_snapshot.ps1` is the tool
already in the tree for this.

Two probe attempts, so the next one does not repeat them:

- Routing the probe through the macro as `wait:14000;threads:...` never reached
  its first snapshot. The guard fires during the heavy first ten seconds of
  compile, before the stall window opens, so the snapshot has to come from
  outside the macro.
- Sampling `/proc/<pid>/task/*` live alongside the run does work and survives
  the force-stop, but the first cut of it was useless: it sorted by frequency
  and kept the top 22, which is all Android and RPCSX infrastructure threads.
  PPU and SPU threads carry unique names and therefore a count of one, so they
  fell off the tail. Filter to `comm` matching `ppu|spu|rsx` instead of ranking
  by count, and read `stat` field 3 by index rather than by whitespace split,
  since several thread names contain spaces and shift the columns.

The unanswered question is narrow and worth one clean run: at roughly `15 s`,
with SPU utilization at zero, what are the PPU threads blocked on, and is the
`CELL_ESRCH` semaphore the thing they are waiting for.

### Superseded: the title is unreachable from this thermal baseline, at any margin

Final attempt, capture `20260806-070046-thor-input-eternal-sonata-field-route`.
The full field route, launched at `32.7 C`, with the conservative buffers taken
all the way down: near-limit probe window `3`, early-stop headroom `1`, so the
run was allowed to proceed to `71 C` against this project's own `72 C` hard
limit, which was left untouched.

It still stopped, at `screenshot-ppu-ready-poll-04`, with
`title_menu_present=False` at `30098 ms`. The concurrent sample was `17` frames
at a `16.873 ms` median, panel cadence for the loading screen again, and again
not a speed result.

That settles the question. Fifteen runs, and the mitigations that helped are
exhausted: little-core cache workers, capped RSX preload, cache warming across
rounds, and finally running to the declared hard limit. From this baseline the
emulator reaches roughly `71 C` about thirty seconds into a boot, and the title
needs longer than that.

The variable that is left is the baseline itself. The device idles with the big
cluster at `41-45 C` and `pm8550b_lite_tz` at `55 C` because it is on USB power
for adb, which leaves only about thirty seconds of headroom between a cold start
and the hard limit. The captures that reached the title and a first battle were
taken with the device at `23-25 C`, where that headroom is far larger. So the
next attempt should be made off charge, in a cooler room, and it should be the
first thing tried rather than the last.
