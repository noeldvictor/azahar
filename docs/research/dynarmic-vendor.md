# Vendored Dynarmic

Azahar Thor Experiment vendors Dynarmic and its recursive build dependencies under
`externals/dynarmic`. This keeps the fork's ARM64 backend changes reproducible from the
single `azahar-thor-experiment` repository and avoids depending on a separate custom
Dynarmic remote.

## Provenance

- Upstream repository: <https://github.com/azahar-emu/dynarmic.git>
- Imported upstream commit: `e77b1ba0b7da7cbe93021b01a663acfe7c4dd516`
- Import date: 2026-08-16
- Nested dependency contents match the gitlinks recorded by that upstream commit.
- Upstream and nested license files remain alongside their source.

## Thor changes after import

- Implement the A32 ARM64 FastDispatch terminal and direct-mapped dispatch cache.
- Send return-stack-buffer misses through FastDispatch when enabled.
- Clear matching entries during range invalidation and clear the whole table with the
  emitted-code cache.
- Clear the A32 dispatch table and block-range bookkeeping at the same explicit
  cache-clear boundary.
- Add focused A32 invalidation and full-cache correctness tests.
- Store adjusted absolute-offset page-table entries on AArch64 so ordinary mapped guest
  loads and stores no longer need a separate page-offset mask instruction.
- Keep A32 guest NZCV in callee-saved `W23` throughout a JIT run. Generated callback
  boundaries synchronize the cached value with `A32JitState`, while A64 retains its
  upstream state-memory path and full register-allocation set.
- Load `A32SetCpsrNZCV` inputs directly into reserved `X23`. A flags-backed value now emits
  `MRS X23, NZCV` instead of materializing an allocator temporary and moving it to `W23`.
- Coalesce eligible final-use ARM64 read/write operands into their existing physical register.
  Vector FMA, VTBX defaults, saturating accumulation, vector-element insertion, FP16 absolute,
  and SHA-256 helpers avoid a full-register copy when the source has one remaining use and one
  lock; all other lifetimes keep the original allocate-and-copy path.
- Reuse that final-use path for `Pack2x32To1x64` and `PackedSelect`, removing their preceding
  `MOV`/`FMOV`, and represent `LeastSignificantWord` as an alias of the source's low 32 bits. Real
  A32 `UMLAL` and all 16 A32 `SEL` GE masks provide permanent guest-level regression coverage.
- Fuse an immediately adjacent, single-use `LeastSignificantByte`/`LeastSignificantHalf` followed
  by its matching signed extension. The narrow IR result aliases its source and the surviving
  `SXTB`/`SXTH` supplies the required truncation and sign extension in one instruction. Other
  consumers retain `UXTB`/`UXTH` except for the separately gated shift-count case below.
- Reuse a proven `LeastSignificantByte` result directly in no-carry 32-bit logical-left,
  logical-right, and arithmetic-right variable shifts. The producer has already emitted `UXTB`, so
  the following `AND #0xff` was a duplicate. Other U8 producers and carry-producing shift paths
  keep their original masks rather than relying on a backend-wide physical-canonicalization rule.
- When `LeastSignificantByte` has exactly one eventual consumer and that consumer uses it as the
  count for 32-bit LSL, LSR, or ROR, alias the raw source instead of emitting `UXTB`. No-carry
  LSL/LSR use `TST #0xe0` to preserve A32's byte-sized saturation rule; ROR and the variable-shift
  portions of the existing carry paths already use the architectural low five bits. ASR retains
  its materialized byte because its otherwise shorter raw-count clamp regressed on A710.

The FastDispatch table is 65,536 16-byte entries (1 MiB per A32 address space). Its
hash mixes the upper location descriptor into the guest PC and discards the always-zero
ARM alignment bit. Table size and hash changes require matched Thor measurements; they
must not be treated as wins from static inspection alone.

The focused Thor microbenchmark measured 1.89x dispatch throughput in its stable
CPU7-pinned reverse-order sample (4.015 ms to 2.128 ms per million indirect dispatches),
with 1.69x to 1.95x observed across all sample groups. This number applies only to the
dispatcher-saturated loop; it is not an emulator-wide FPS or power result.

The direct NZCV benchmark ran 16,777,216 flag evaluations per case, alternated order for nine
rounds, and measured 16.22% faster on A510, 20.13% on A710, 19.28% on A715, and 2.30% on X3.
This is the exact generated transfer followed by a flag consumer, not a whole-game result.

The corrected read/write microbenchmark used four independent dependency chains and confirmed in
disassembly that the old loop copied a full vector before each FMLA or BIC while the coalesced loop
did not. Best-of-nine Thor results measured 1.86x to 3.50x throughput, equivalent to 46.1% to 71.5%
less time in those synthetic recurring sequences. This does not predict whole-game FPS or watts;
the affected opcode mix and register lifetimes vary by title.

The follow-on packing/select benchmark also used four independent chains, 16,777,216 useful
operations, alternating order over nine rounds, best samples, equal checksums, and final
disassembly. Removing one move measured 1.05x-2.51x across the three exact sequences and four Thor
core classes. The A510 results were 2.51x for 32-bit packing, 2.12x for low-word extraction, and
1.38x for packed select; A710/A715/X3 results ranged from 1.05x to 1.50x. These are generated-code
microbenchmarks rather than emulator-wide speed or power measurements.

The signed-narrow benchmark compared four independent `UXTB; SXTB` or `UXTH; SXTH` chains with
the fused `SXTB`/`SXTH` form over 16,777,216 iterations, alternating order for nine rounds and
checking equal results. Best samples were 2.50x/4.50x faster for byte/halfword on A510, 1.67x/1.67x
on A710, and 1.75x/1.81x on A715 CPU 5; A715 CPU 6 independently measured 1.86x/1.78x. CPU 7
rejected even a harmless single-bit affinity probe with `EINVAL` during this run, so no X3 number
is claimed. These are exact generated-sequence measurements, not whole-emulator FPS or watts.

The register-shift benchmark retained the frontend `UXTB` and compared the old duplicate-mask
sequence with direct use of that canonical result. Four independent chains, 16,777,216 iterations,
nine alternating-order rounds, disassembly inspection, and equal nonzero checksums measured LSL at
1.415x on A510, 1.283x on A710, and 1.188x on A715; clamped ASR measured 1.236x, 1.286x, and 1.244x.
CPU 6 and CPU 7 rejected harmless single-bit affinity probes for the final run, so no result is
claimed for those cores. These 15.8%-29.3% exact-sequence time reductions do not predict whole-game
FPS or battery watts.

The follow-on sole-consumer benchmark compared the post-mask-elision sequences against raw-count
LSL, LSR, and ROR over the same four chains, 16,777,216 iterations, nine alternating-order rounds,
disassembly inspection, and equal nonzero checksums. LSL/LSR measured 2.006x/2.399x on A510,
1.248x/1.248x on A710, and 1.214x/1.213x on A715; ROR measured 4.529x, 1.304x, and 1.360x. CPU 6
and CPU 7 rejected harmless single-bit affinity probes, so no second-A715 or X3 result is claimed.
An ASR candidate helped A510 by about 23% but repeatedly ranged from 0.9% to 4.9% slower on A710;
it was rejected. These figures apply only to the isolated emitted sequences, not whole-game FPS or
battery watts.

The mixed-halving benchmark reproduced Dynarmic's exact old nine-instruction A32
`SHASX`/`SHSAX` lowering and the new four-instruction `REV32`, native halving add/subtract, and
element insert path. Eight recurring operations, 2,000,000 iterations, seven alternating-order
rounds, disassembly inspection, and checksum `0040003f` measured 2.506x on A510 CPU 0, 2.336x and
2.316x on A715 CPUs 3 and 4, and 2.334x on A710 CPU 6. CPUs 5 and 7 rejected the harmless affinity
request, so no second-A710 or X3 result is claimed. These are exact generated-sequence timings, not
whole-game FPS or battery watts.

The mixed-saturation follow-up keeps ARM11 `QASX`/`QSAX`/`UQASX`/`UQSAX` packed in IR. Its ARM64
backend exchanges halfwords with `REV32`, computes both lane candidates with signed or unsigned
saturating AdvSIMD arithmetic, and inserts the alternate low lane. A disassembly-checked,
checksum-locked dependency-chain benchmark compared the old 21-instruction scalar clamp/repack
sequence with the four-instruction native operation over four alternating rounds of 8,000,000
operations. Best samples measured 1.110x on A510 CPU 0, 2.121x and 2.141x on A715 CPUs 3 and 4,
and 1.808x on A710 CPU 5. CPU 6 and X3 CPU 7 rejected the affinity request, so no result is claimed
for them. These are exact-path operation timings, not whole-game FPS or battery watts.

Command-line Git checked upstream `master` again on 2026-08-18. It resolved to
`fb1c1a7104fae94c670e2ea1e2a6bf09e99379c2`, and `git merge-base master upstream/master`
returned that same revision. The fork therefore already contains the latest upstream source; no
merge or second repository was needed.

## Updating

Use command-line Git to fetch the desired upstream revision into the current repository's object
store and compare it with this vendored tree; do not create another repository. Import only an
intentionally reviewed update. Preserve the Thor changes or replace them with equivalent upstream
code, run the ARM64 Dynarmic tests, build the release-style Android APK, and test it on the Thor
before committing directly to `master`. Delete temporary refs and generated build trees after
verification.
