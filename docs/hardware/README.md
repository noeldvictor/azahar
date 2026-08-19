# Hardware Reference Index

The AYN Thor Base/Pro/Max target is Snapdragon 8 Gen 2 (`kalama`/SM8550-class), with one Cortex-X3, two Cortex-A715, two Cortex-A710, three Cortex-A510 cores, and Adreno 740. The host vendor documents reviewed below are held outside this Git repository in the existing sibling personal research libraries recorded here.

Manual PDFs are deliberately not committed to this public fork. Their redistribution terms are not always explicit, and binary manuals make source history needlessly heavy. This index records exact external inputs so they cannot be confused with an unverified download.

## Snapdragon 8 Gen 2 host references

| Document | Revision or scope | Size | SHA-256 | Azahar relevance |
| --- | --- | ---: | --- | --- |
| `qualcomm_adreno_game_developer_guide.pdf` | Qualcomm 80-78185-2, 200 pages | 20.17 MiB | `0872AA49B763ACB46AEB7427784E926D2BF3939F2E731B405DEF977A5BFAECAC` | GMEM, render passes, resolves, concurrent binning, LRZ, mobile bandwidth, and page 84 texture-fetch/filter cost |
| `qualcomm_snapdragon_opencl_optimization_guide.pdf` | Qualcomm 80-NB295-11 Rev. C, 116 pages | 1.71 MiB | `59CEEE4F9E33686CEB5F2970045C77858B4A395885778C5944C3130E590EEB3A` | Hardware cache/UMA guidance; OpenCL API advice is out of scope |
| [Arm Architecture Reference Manual for A-profile architecture](https://developer.arm.com/documentation/ddi0487/ha), `arm-architecture-reference-manual-a-profile.pdf` | DDI 0487 H.a, 11,530 pages; sibling `xenia-thor` research library | 65.86 MiB | `62AF4D2F908347C3018BD63572F0CE1D5EDF0D34ABAE2BEE3C18DE7C97D06E43` | Sections C7.2.289, C7.2.309, C7.2.339, C7.2.390, and C7.2.403 define `SQDMULH`, saturating `SQXTUN`, byte-table `TBL`, lane-variable `USHL`, and `ZIP1` semantics used by exact audio and ETC1 SIMD paths |
| [Arm Architecture Reference Manual for A-profile architecture](https://developer.arm.com/documentation/ddi0487/mc), `arm_architecture_reference_manual_DDI0487M_c.pdf` | DDI 0487 M.c, 17,145 pages | 119.93 MiB | `B5F9DAA7EC0446777C8F848AA6431C99E5AB6554E5AA171B28B9016771494F8C` | Authoritative AArch64 instruction and memory-ordering semantics; sections C6.2.180 and C6.2.192 distinguish relaxed `LDADD`/ordinary loads from release/acquire variants |
| `arm_cortex_x3_software_optimization_guide.pdf` | Cortex-X3 | 1.12 MiB | `3EC100F2BBCD4DE004E1730553A3276BB27ECA4B320471B177BBDB843B8761D9` | Prime-core pipelines; scalar `SMADDL`/`SMSUBL` and accumulator forwarding are on page 16; same-width `SABD`/`UABD` and accumulating `SABA`/`UABA`, plus `SABDL`/`UABDL`, are on page 25; long/wide `SADDL`/`UADDL`/`SSUBL`/`USUBL`/`SADDW`/`UADDW`/`SSUBW`/`USUBW`, halving `SHADD`/`SHSUB`/`UHADD`/`UHSUB`, saturating `SQADD`/`SQSUB`/`UQADD`/`UQSUB`, and SoundTouch FIR/WSOLA `ADDV` are on page 26; element `INS` and `REV32` are on pages 31-32, `SMLAL`/`SMULL` and late forwarding on pages 27-28, Q-form `LD2` on page 33, AdvSIMD floating compare on page 28, and element-to-GPR `UMOV`/`SMOV` timing on page 32; D24 conversion/divide is on pages 28-29, reciprocal estimate/refinement/divide/square-root timing is on pages 29-32, and `FADD`/`FADDP` plus D/Q `FCVTZS` comparisons are on pages 28-29 |
| `arm_cortex_a715_software_optimization_guide.pdf` | Cortex-A715 | 1.16 MiB | `D6D7A49F34528B79E1F8C8E0B02D59D3DA6011D719D3271D310A4D83EC8F6FA2` | Newer performance-core pair; scalar `SMADDL`/`SMSUBL` and accumulator forwarding are on page 18; same-width `SABD`/`UABD` and accumulating `SABA`/`UABA` are on pages 27-28, while `SABDL`/`UABDL`, long/wide `SADDL`/`UADDL`/`SSUBL`/`USUBL`/`SADDW`/`UADDW`/`SSUBW`/`USUBW`, halving `SHADD`/`SHSUB`/`UHADD`/`UHSUB`, saturating `SQADD`/`SQSUB`/`UQADD`/`UQSUB`, and SoundTouch FIR/WSOLA `ADDV` are on page 28; element `INS` and `REV32` are on page 34, `SMLAL`/`SMULL` is on page 29, Q-form `LD2` on page 36, AdvSIMD floating compare on page 30, and element-to-GPR `UMOV`/`SMOV` timing on page 35; D24 conversion/divide is on pages 30-32, reciprocal estimate/refinement/divide/square-root timing is on pages 31-34, and `FADD`/`FADDP` plus D/Q `FCVTZS` comparisons are on pages 30-31 |
| `arm_cortex_a710_software_optimization_guide.pdf` | Cortex-A710 | 1.39 MiB | `096B9C2924BBFA4C5045D2C2F1C711D6E544C28F2F04ADAAD4B66B2E8C48CD6A` | Older performance-core pair; scalar `SMADDL`/`SMSUBL` and accumulator forwarding are on page 21; same-width `SABD`/`UABD`, accumulating `SABA`/`UABA`, `SABDL`/`UABDL`, long/wide `SADDL`/`UADDL`/`SSUBL`/`USUBL`/`SADDW`/`UADDW`/`SSUBW`/`USUBW`, halving `SHADD`/`SHSUB`/`UHADD`/`UHSUB`, saturating `SQADD`/`SQSUB`/`UQADD`/`UQSUB`, and SoundTouch FIR/WSOLA `ADDV` are on page 42; element `INS` and `REV32` are on page 52, `SMLAL`/`SMULL` is on page 43, Q-form `LD2` on page 55, AdvSIMD floating compare on page 46, and element-to-GPR `UMOV`/`SMOV` timing on page 53; D24 conversion/divide is on pages 46-48, reciprocal estimate/refinement/divide/square-root timing is on pages 47-52, and `FADD`/`FADDP` plus D/Q `FCVTZS` comparisons are on pages 46-47 |
| `arm_cortex_a510_software_optimization_guide.pdf` | Cortex-A510 | 1.22 MiB | `E80E25EFFBEE27FB95740420469846FA0B3211C5716757A464E5C32E63281D44` | Efficiency cores; scalar `SMADDL`/`SMSUBL` and accumulator forwarding are on page 18; same-width `SABD`/`UABD`, accumulating `SABA`/`UABA`, `SABDL`/`UABDL`, long/wide `SADDL`/`UADDL`/`SSUBL`/`USUBL`/`SADDW`/`UADDW`/`SSUBW`/`USUBW`, halving `SHADD`/`SHSUB`/`UHADD`/`UHSUB`, saturating `SQADD`/`SQSUB`/`UQADD`/`UQSUB`, and SoundTouch FIR/WSOLA `ADDV` are on page 35; element `INS` and `REV32` are on page 43, `SMLAL`/`SMULL` is on page 36, Q-form `LD2` on page 46, AdvSIMD floating compare on page 39, and element-to-GPR `UMOV`/`SMOV` timing on page 44; D24 conversion/divide is on pages 39-40, reciprocal estimate/refinement/divide/square-root timing is on pages 40-43, and `FADD`/`FADDP` plus D/Q `FCVTZS` comparisons are on pages 39-40; D-form byte/halfword `ST3`, D-form `ST4`, Q-form `ST4`, and four-table `TBL` are especially slow at `1/17`, `1/25`, `1/50`, and `1/9` throughput |

## Guidance already applied

- Fold only measured small immediate left shifts into AArch64's shifted-register `ADD`. The
  accepted Dynarmic gate is a sole immediately adjacent flag-free A32 `LSL #1..#4` feeding ADD;
  shared, carry-producing, variable, immediate-source, and wider forms remain split. Exact all-core
  measurements overruled the tempting instruction-count generalization because base-dependent
  shifts 16/31 fell to about half speed on A715/A710/X3.
- Measure flag-setting shifted arithmetic separately from no-flags ADD/SUB. The basic arithmetic
  rows are on X3 page 15, A715 and A710 page 17, and A510 page 14, and AArch64 can encode shifted
  `ADDS`/`SUBS`; those facts prove availability, not a universal speedup. Exact Thor runs accepted
  only a sole adjacent no-carry `LSL #1..#4` feeding normal-carry-in ADD/SUB with exactly one NZCV
  pseudo-result. Every accepted dependency shape was neutral or faster on A510/A715/A710/X3.
  Flag-setting LSR/ASR and LSL 5..31 stay split because base-dependent forms fell to roughly
  0.51x-0.53x on the big cores. Preserve this narrower gate for ADDS/SUBS/CMN/CMP.
- Treat right shifts as a separately measured instruction family. AArch64 shifted-register `ADD`
  encodes `LSR` and `ASR` immediates through 31, and representative 1/2/3/4/8/16/31 measurements
  retained or improved throughput on every Thor core class. The accepted Dynarmic path therefore
  folds sole immediately adjacent flag-free A32 LSR/ASR producers across 1..31, while encoded-zero
  LSR/ASR (guest shift 32), carry-producing, shared, variable, and non-adjacent forms stay split.
  This does not relax the LSL 1..4 gate: encoding availability and lower instruction count are not
  substitutes for per-family heterogeneous-core evidence.
- Treat shifted `SUB` as another independently measured family. The A-profile architecture manual
  defines 32-bit shifted-register `SUB` with `LSL`, `LSR`, or `ASR` immediates through 31, but that
  proves semantics and encodability rather than performance. Disassembly-checked Thor runs found
  no repeatable regression across the 1..31 range, including corrected long X3 confirmations, so
  Dynarmic may fold a sole adjacent flag-free A32 shift into normal subtraction throughout that
  range. Preserve normal subtraction carry-in and keep borrow/reverse-subtract, carry-producing,
  shared, variable, zero/32, and non-adjacent forms split. This result remains separate from ADD's
  narrower measured LSL gate.
- Treat shifted logical operations as their own measured family. The A-profile architecture manual
  defines 32-bit shifted-register `AND`, `EOR`, and `ORR` with `LSL`, `LSR`, `ASR`, or `ROR`
  immediates through 31. The standalone logical-operation timing rows are on X3 page 15, A715 and
  A710 page 17, and A510 page 14; removing that materialized shift reduces a real instruction and
  front-end/integer issue work. Exact all-core runs accepted 1..31 because no corrected row
  regressed: independent work improved on every core, shifted-index chains improved on the big
  cores, and neutral dependency shapes stayed within about 0.02% after long confirmation. Fold
  only a sole adjacent no-flags/no-carry A32 shift feeding operand 1 of AND/EOR/ORR; preserve
  shared, non-adjacent, immediate-source, variable, zero/32/RRX, flag/carry, and unrelated paths.
  Encoding availability and the manuals support the shape, but only the measured Thor gate supports
  the performance decision, which remains separate from ADD's narrower LSL rule.
- Treat shifted `MVN` as a unary dependency shape, not as proof for every member of the logical
  family. The no-flags shifted logical rows list latency/throughput 1/3 ALU on A510 page 14, 1/4 I
  on A710 and A715 page 17, and 1/6 I on X3 page 15. Exact Thor runs accepted folding a sole
  adjacent no-flags/no-carry LSL/LSR/ASR/ROR #1..31 into one `MVN`: independent loops improved
  on every core, long A510 input-dependency checks were neutral, and big-core dependency loops
  improved. Preserve zero/32/RRX, flag/carry, shared, non-adjacent, immediate, and variable forms.
  Do not generalize this result to shifted `BIC`: its additional base dependency produced repeated
  A510 regressions around 1.4% and 0.7%, so the split `shift; BIC` path remains the safe default.
- Do not globally substitute same-width `SABA/UABA` for `SABD/UABD` plus `ADD`. The checked rows
  list `SABD/UABD` latency/throughput as 2/4 on X3 page 25, 2/2 on A715 pages 27-28 and A710 page
  42, and latency 3 with split `2,1` throughput on A510 page 35. Accumulating `SABA/UABA` is 4(1)/2
  on X3, 4(1)/1 on A715/A710, and latency 6 with `1/2,1/4` throughput on A510. The physical A510
  accumulator-chain regression controls the decision despite wins on the other core classes.
- Collapse exact sole-use `SXTB`/`SXTH`-to-word plus `SXTW` chains to direct AArch64
  `SXTB`/`SXTH`-to-X forms. The baseline `SBFM` timing tables used for this gate are on Cortex-X3
  page 18, Cortex-A715 page 20, Cortex-A710 page 27, and Cortex-A510 page 22.
- Keep compatible render work inside render passes/GMEM and avoid unnecessary resolves or mid-pass dependencies.
- Treat skipped presentation as an asynchronous queue boundary, not a GPU-idle boundary. Submit
  pending Vulkan work so emulated rendering continues in order, but rely on timeline-tagged pools,
  stream-buffer wrap waits, and conservative deferred destruction instead of blocking the CPU on
  every duplicate/Eco-Turbo-skipped frame. Retain explicit waits for CPU readback and resource
  destruction. This preserves CPU/GPU overlap and avoids needless Adreno completion wakeups.
- Treat normal native presentation as one FIFO worker command, not a CPU-worker join or two
  scheduler dispatches. Submit the render-ready semaphore first, release the submit lock, enqueue
  the frame, release its predicate mutex, and only then notify presentation. Completion-tick
  resource retirement must remain strict (`completed > sentenced`), with explicit drains retained
  for synchronous presentation, LibRetro, CPU readback, resize, and destruction. This removes a
  per-frame dispatch/queue/wakeup cycle without changing Vulkan queue order or inventing another
  background thread.
- Do not resolve a stereo presentation surface that no active layout can sample. For mono-left or
  bottom-only output, preserve a valid descriptor by aliasing the current left image and skip the
  right-eye surface lookup/upload. This applies the Adreno guide's avoid-unnecessary-resolve rule
  directly and removes CPU/cache/driver work without assuming a specific Arm instruction. Retain
  the real right-eye resolve for stereo and mono-right output, and use actual frame-skip state rather
  than a settings checkbox when a compatibility hack can be disabled per title.
- Use Snapdragon Profiler to verify concurrent binning; do not infer it from source structure alone.
- Treat memory writes, texture uploads, and CPU wakeups as power costs, not only frame-time costs.
- Preserve PICA sampler semantics instead of forcing device-maximum anisotropy. PICA exposes
  nearest/linear and mip filtering but no anisotropy control, and OpenGL already leaves anisotropy
  off. Vulkan guest and final-screen samplers therefore use isotropic filtering and
  `maxAnisotropy = 1.0f`. Qualcomm guide page 84 warns that texture fetches/cache misses and stronger
  filters consume texture-pipe capacity; a 16x anisotropic lookup can require up to sixteen samples
  for an affected fragment, although adaptive real workloads are commonly much lower. Khronos also
  defines nearest-plus-anisotropy behavior as implementation-dependent. Keep the Vulkan device
  feature available, but do not opt samplers into it without an explicit setting and Thor A/B.
- Prefer vector permutation plus ordinary contiguous stores over byte/halfword `ST3` or `ST4`
  when the exact layout permits it; the A510 structured-store path is exceptionally slow.
- For converted RGB5A1/RGB565/RGBA4 encode, prepare both eight-pixel halves in Q registers so masks
  and byte-field assembly are shared through `SHLL`/`SHLL2`. Linear input should use one Q-form
  byte `LD4` for all sixteen pixels, not two D-form `LD4` operations. That halves the structured
  load count and the documented load-issue budget on A715 and A510 while keeping the same issue
  budget on X3 and A710. The relevant entries are X3 page 34, A715 page 37, A710 page 56, and A510
  page 47. Morton rows are non-contiguous, so retain their two D-form loads but share subsequent
  channel preparation.
- For packed D24 Morton to D32-float conversion, use D-form `LD3`, one-table shuffles, and
  ZIP/narrow operations across a two-row band. Do not collapse the mapping into a four-table `TBL`:
  Cortex-A510 documents that form at latency 16 and throughput `1/9`. Preserve true vector `FDIV`
  and `FCVTZU`; reciprocal approximations or changed rounding are not equivalent depth math.
- Approximate reciprocal-square-root only where the emulated operation already has that contract.
  X3, A715, and A710 list F32 `FDIV`/`FSQRT` at 7-10-cycle latency, with reciprocal estimates at
  three cycles and refinement steps at four; A510 lists divide at 13, square root at 12, and the
  estimate/refinement/multiply operations at four. Thor measurements nevertheless rejected the
  analogous `RCP` sequence as slower on every core. PICA `RSQ` uses one estimate plus one Newton
  step and measured 16.2-43.2% faster in isolation; exact D24 depth conversion remains unchanged.
- Match SIMD width to live lanes. PICA `MOVA` consumes only X/Y, so D-form `.2S` `FCVTZS` avoids
  converting dead Z/W lanes and measured essentially twice Q-form throughput on every Thor core
  class. For `DP3`, remove the GPR-to-vector W-zero insertion: reduce X/Y while independently
  broadcasting Z, then perform the required scalar add. This keeps `(X + Y) + Z`, ignores W, and
  measured 16.7-26.0% faster. X3/A710 list `FADD` and `FADDP` at two-cycle latency, A510 lists both
  at four, and A715 lists normal `FADD` at two versus pairwise `FADDP` at three.
- For converted linear RGB8, deinterleave each 48-byte BGR block with one Q-form `LD3` and assemble
  opaque RGBA with ZIPs. In the reverse direction, split each 48-byte BGR output into three exact
  adjacent-input `TBL2` maps rather than exposing all four RGBA input vectors to `TBL4`. X3,
  A715, and A710 list `TBL2` at latency/throughput 2/2 versus 4/`2/3` for `TBL4`; A510 lists
  8/`2/5` versus 16/`1/9`. The relevant Q-form `LD3` entries are on X3 page 34, A715 page 37,
  A710 page 56, and A510 page 46.
- Apply the same store-side rule to Y2R's final output packing. Numeric `0xRRGGBB00` words can become
  RGBA8 with ordinary Q loads, alpha ORs, and ordinary Q stores; RGB8 can drop each zero byte with
  three adjacent-input `TBL2` maps and contiguous stores. RGB5A1/RGB565 can use one Q-form `LD4`,
  byte masks, `SHLL`/`SHLL2`, and paired Q stores. Keep table constants outside the repeated RGB8
  loop and verify final ThinLTO contains no `ST3`/`ST4`; the A510 structured-store throughput in the
  reference table above makes source-level auto-vectorization an unsafe performance assumption.
- Remove identity staging passes before trying to accelerate them. For unrotated linear Y2R output,
  the tile remap is exactly the identity, so stream each 32-byte tile row into its final horizontal
  position and make the destination row the inner traversal. The checked ordinary pair load/store
  tables on X3 page 23, A715 page 26, A710 page 39, and A510 page 32 support the final post-indexed
  Q-form `LDP`/`STP` loop. This halves arrangement load/store bytes and improves destination
  locality without changing any color, rotation, swizzle, stride, or CDMA semantics.
- Apply the same rule before Y2R conversion. An 8-bit CDMA source with `gap == 0` is already the
  compact byte stream consumed by the converter, so borrow that read-only guest pointer and update
  only the visible address/remaining-size state. Keep the staging buffer for gapped transfers and
  for 16-bit low-byte extraction. This removes a complete read-plus-write pass rather than spending
  NEON instructions on a copy that has no semantic work.
- When unrotated linear Y2R also has zero output gap, fuse the tile-row gather with final packing
  instead of materializing a contiguous RGB32 strip. Pair adjacent tile rows for RGB8 so three
  `TBL2` operations produce 48 packed bytes; retain a separate exact Q/D-store tail for an odd tile.
  RGBA8 can load, OR alpha, and store each eight-pixel row directly. RGB5A1/RGB565 legitimately use
  one D-form byte `LD4` because the next horizontal tile row is 256 bytes away rather than adjacent,
  then pack into an ordinary Q store. The relevant ordinary pair tables are X3 page 23, A715 page
  26, A710 page 39, and A510 page 32; `TBL2` and byte `LD4` are covered on X3 page 34, A715 page 37,
  A710 page 56, and A510 pages 46-47. This removes eight logical staging bytes per pixel while
  preserving the gapped, rotated, and tiled fallbacks.
- Complete the lifetime analysis after removing staging traffic: if every active 8-bit input is
  borrowed and final output is direct, do not allocate the now-dead strip buffer. Keep it for any
  active input/output gap, 16-bit low-byte extraction, rotation, or tiling, and ignore gaps on
  inactive planes. Preserve the fallback's uninitialized allocation; array `make_unique` would
  value-initialize it and add a full clear. Hoist its fixed Y/U/V partition addresses out of the
  strip loop and verify linked control flow skips only the matching allocation and deallocation.
- For packed S8D24 staging, prefer ordinary paired loads plus narrowing/`UZP` and contiguous plane
  stores over `LD4`; A510 lists one-register `LD1` at `2/cycle` but Q-form byte `LD4` at `1/3`.
- For interleaved stereo HLE audio that feeds planar mix buses, unroll eight samples and use
  ordinary paired Q loads plus `UZP` before sharing widening/conversion work across outputs. The
  A710, A715, and X3 guides recommend loop unrolling and non-writeback `LDP`/`STP` for throughput;
  final ThinLTO must confirm this lowering instead of assuming an intrinsic avoids `LD2`.
- For large linear index-bound scans, use four independent `UMIN` and `UMAX` chains over 64-byte
  bands and let Clang combine adjacent vector loads into Q-form `LDP`. The X3, A715, and A710 list
  AdvSIMD integer min/max at two-cycle latency, while the A510 lists three cycles; one accumulator
  therefore serializes the next vector even when load bandwidth is available. Across all four
  cores, two Q loads and one Q `LDP` carry the same useful bytes per documented issue interval.
  Keep a single-chain path below two bands so accumulator setup and the final reduction do not
  inflate small-draw work, and inspect final ThinLTO rather than assuming source unrolling survives.
- For final HLE audio downmix that must read and write interleaved stereo, widen D-form `LD2`/`ST2`
  to Q-form and handle eight samples per loop. The exact tables above show equal or better useful
  bytes per cycle for Q form on all four Thor core classes. Do not replace the structured pair with
  ordinary loads plus `UZP`/`ZIP`: that adds permutation work without removing an expensive
  multiway transpose. Confirm Q-form instructions and spill-free loops in final ThinLTO.
- For recurrent GC-ADPCM, load each packed byte once and use direct signed bitfield extraction for
  its high and low four-bit samples instead of indexed integer-table reads. X3 page 18, A715 page
  20, A710 pages 27-28, and A510 pages 22-23 document basic `SBFM`/`SBFX` and load characteristics:
  the bitfield operation is a short ALU instruction and avoids both address/index work and cache
  traffic. Preserve sequential filter feedback; this optimization removes representation work,
  not the recurrence itself, and final ThinLTO must prove the lookup table disappeared.
- For sequential PCM8/PCM16 output into libc++'s deque, keep one output iterator and a counted loop
  instead of calling indexed `operator[]` for every sample. The indexed AArch64 code makes the
  destination store depend on another block-map load; X3 pages 18-19, A715 pages 20-21, and A710
  pages 28-29 list ordinary L1-hit integer loads at four-cycle latency and throughput three, while
  A510 pages 23-24 list two-cycle latency and throughput two. Advancing the current element pointer
  removes that dependent load/address chain until the rare deque-block transition without assuming
  any optional ISA extension. Preserve the portable deque abstraction and verify the final linked
  loop because source-level iterators alone do not prove the compiler retained the pointer.
- Eliminate provable write-before-write traffic before reaching for a wider instruction. HLE source
  resampling defines and overwrites its complete produced prefix, so a full-frame silence store is
  needed only when no output will be produced; an underrun needs silence only after the produced
  prefix. This saves store-pipeline, cache, and dirty-line work across every Thor core class without
  assuming an optional ISA feature. Preserve full clears on early returns and tail silence before
  recurrent filters, then verify the control-flow shape in the linked AArch64 binary.
- Apply the same rule to multi-producer accumulators, but count recurring bookkeeping as well as
  vector work. Let the first audible producer direct-write its complete routed buses without
  loading or adding known zeros, clear the rest of the accumulator set immediately, and return
  later producers to the original fast path. A front-only specialization must explicitly define
  its omitted rear planes. Preserve arithmetic order and state transitions, and inspect final
  direct and accumulated loops separately; source templates alone do not prove loads disappeared.
- For SoundTouch's integer 64-tap stereo FIR, use an exact 32-bit accumulator on Android LP64 and
  feed both channels from one coefficient vector. Two Q-form `LD2` sample loads plus one paired
  coefficient load per sixteen taps avoid the duplicated coefficient structured loads while eight
  independent `SMLAL` chains cover both channels and hide dependency latency. Preserve the scalar
  remainder, arithmetic shift, saturation, and full-range overflow proof, then inspect final
  ThinLTO for `ADDV` rather than assuming source-level autovectorization survived.
- For SoundTouch WSOLA correlation, keep the algorithm's 32-bit accumulator/normalizer widths and
  use one Clang vector-interleave group on Android AArch64. This leaves four independent 4S
  accumulators in caller-saved NEON registers, then reduces with `ADDV`; higher automatic
  interleaving spills a callee-saved vector register without improving work per frame. Preserve
  the rolling normalizer's per-sample shift semantics and confirm final ThinLTO remains spill-free.
- Prefer eliminating an inactive DSP stage over optimizing its instructions. Azahar changes
  SoundTouch tempo only, so exact unity pitch/rate requires no anti-alias FIR or interpolation.
  The opt-in path must enter TDStretch directly before stream processing; keep generic crossover
  behavior unchanged and confirm final linked code does not call RateTransposer on that branch.
- For PICA source swizzles that run in the AArch64 CPU shader JIT, prefer one or two register-only
  lane permutations over loading a 16-byte index literal and executing `TBL`. The X3 guide lists
  element `DUP`, `EXT`, `INS`, `REV64`, `TRN`, `ZIP`, and `UZP` at latency 2 and throughput 4,
  versus throughput 2 for one-table `TBL`; A715/A710 list the simple permutations and one-table
  `TBL` at latency 2, while A510 lists simple permutations at latency 3 and one-table `TBL` at
  latency 4. Preserve an exact `TBL` fallback and prove the synthesized maps exhaustively.
- When PICA `CMP` uses one operation for both X and Y, issue one four-lane AdvSIMD floating compare
  and extract the two lane sign bits instead of serial scalar compares and lane moves. The checked
  tables list `FCMEQ`/`FCMGE`/`FCMGT` at latency/throughput 2/4 on X3 page 28, 2/2 on A715 page 30,
  2/2 on A710 page 46, and 3/`2,1` on A510 page 39. Preserve ordered NaN behavior by implementing
  `NotEqual` as inverted equality, and leave mixed operations on the scalar path.
- For canonical zero/one PICA condition flags, select one arithmetic/logical flag-setting operation
  whose returned AArch64 condition code directly represents the guest truth table; do not first
  invert booleans into scratch registers. The basic integer arithmetic/logical tables cover `CMP`,
  `CMN`, and `TST` on Cortex-X3 page 15, Cortex-A715 page 17, Cortex-A710 page 17, and Cortex-A510
  page 14. These are baseline operations across every Thor core class. Prove all sixteen reference/
  input combinations and every branch consumer because the true condition is not always EQ/NE.
- Treat instruction tables as candidate guidance rather than proof across the heterogeneous SoC.
  X3 page 15, A715/A710 page 16, and A510 pages 12-13 make `CBZ`/`CBNZ` look attractive, but a
  disassembly-checked Thor benchmark rejected replacing the PICA uniform `CMP` plus conditional
  branch. Taken branches regressed 22.7%-24.2% on A510 and 38.9%-46.0% on A710; A715/X3 mostly tied,
  and only the A510 fallthrough case won. Preserve the current sequence until a real shader route
  provides a different measured branch distribution.
- Eliminate a fixed-register relocation when the destination is already reserved architectural
  state. The A710 page 86, A715 page 63, and X3 page 60 special-register tables say NZCV reads are
  fully renamed and have no non-speculative, in-order, or flush constraint. Dynarmic can therefore
  read arithmetic flags directly with `MRS X23, NZCV` instead of `MRS Xtemp, NZCV` plus `MOV`.
  The A510 guide has no comparable special-register table, so its measured 16.2% sequence win is
  the controlling evidence there; the A710/A715/X3 wins were 20.1%, 19.3%, and 2.3% respectively.
- Treat a full SIMD register copy in generated code as real front-end/vector-pipeline work even
  when the Arm manuals make the following arithmetic operation look cheap. If an IR read/write
  operand is at its final use, has one owner, and stays in the same host-register class, transfer
  the physical register to the output instead of emitting the copy. Keep a conservative fallback
  for shared, locked, immediate, spilled, or already-realized values, then prove the result with
  final disassembly, independent dependency chains, and guest-instruction correctness tests on
  every Thor core class.
- Apply the same rule to bitfield packing and byte selection. The X3 page 18, A715 page 20, A710
  page 27, and A510 page 22 tables list `BFM`/`BFI` as real integer-pipeline work; the corresponding
  `BSL` rows are X3 page 31, A715 page 34, A710 page 52, and A510 page 43. Removing a preceding
  `MOV`/`FMOV` therefore reduces dependency and issue work on every Thor core class without relying
  on an optional ISA feature. Preserve the original low word before `BFI`, exact per-byte GE
  selection through `BSL`, and the copy fallback whenever the mask or low operand is still shared.
- Prefer a first-class semantic operation over a synthesized mask network when the host ISA has an
  exact scalar match. The AArch64 `RBIT` rows are A510 page 22, A710 page 27, A715 page 20, and X3
  page 18. Keep A32 ARM/Thumb-2 bit reversal as IR until ARM64 can emit one `RBIT`, retain portable
  polyfills elsewhere, and require disassembly plus exact-path measurements on every Thor core
  class before acceptance.
- The same pages list AArch64 `REV16` at latency/throughput 1/3 on A510, 1/4 on A710, 1/4 on A715,
  and 1/6 on X3. Keep A32 ARM/Thumb-16/Thumb-2 halfword-byte reversal as
  `ByteReverseHalfwords32` until ARM64 emits one `REV16`; preserve portable polyfills and require
  all-core disassembly-checked measurement rather than accepting the shorter sequence by inspection.
- Those pages also show why signed halfword reversal needs a measured compound choice rather than
  an instruction-count guess. `REV`/`REV16` has latency/throughput 1/3 on A510, 1/4 on A710 and
  A715, and 1/6 on X3. The `SBFM` family containing `SXTH` is 2/3 on A510 and 1/4 on the other
  cores, while the A510 guide's immediate-`ASR` alias has latency 1. Keep A32 ARM/Thumb-16/Thumb-2
  `REVSH` as `ByteReverseSignedHalf32`, emit the all-core winner `REV; ASR #16` on ARM64, and
  retain the exact `UXTH; REV16; SXTH` semantic polyfill on other hosts. Do not replace it with
  `REV16; SXTH`; that candidate lost materially on the A510 dependency chain.
- The same scalar bitfield rows cover the native `SBFM`/`UBFM` aliases used by AArch64 `SBFX` and
  `UBFX`. A510 page 22 lists the basic bitfield group at latency/throughput 2/3 while noting the
  simple immediate-shift aliases at latency 1; A710 page 27 and A715 page 20 list 1/4, and X3 page
  18 lists 1/6. Keep A32 ARM/Thumb-2 bitfield extraction semantic until ARM64 emits one native
  instruction, but expect dependency chains to tie the old two latency-1 operations on A510 while
  improving on the larger cores. Require actual-JIT disassembly, independent and dependent
  all-core measurements, full-width identity coverage, portable host polyfills, and exact guest
  signedness/flags/FPSCR tests before accepting the lowering.
- The insert row is distinct from the basic `SBFM`/`UBFM` row. AArch64 `BFM`, which provides the
  `BFI` alias, is latency/throughput 2/3 on A510 page 22, 2/2 on the M pipeline on A710 page 27,
  1/4 on the I pipeline on A715 page 20, and 2/2 on the M pipeline on X3 page 18. This predicts
  that replacing A32 `BFI`'s four-operation mask/shift/OR graph removes issue work on every core,
  while a distinct dependency chain can still tie its old destination-AND-to-OR critical path on
  A510, A710, and X3. Preserve semantic bitfield-insert IR, a copy-free self-alias lowering,
  portable polyfills, actual-JIT disassembly, and repeated all-core measurements. Leave A32 `BFC`
  on its one-instruction ARM64 logical-immediate clear path.
- Those same scalar data-processing tables list the AArch64 move-wide family containing `MOVK` at
  latency/throughput 1/3 on A510 page 22, 1/4 on A710 page 27, 1/4 on A715 page 20, and 1/6 on X3
  page 18. A32 ARM/Thumb-2 `MOVT` is an exact semantic match for `MOVK Wd,#imm,LSL#16`: both retain
  the low halfword and replace the high halfword without touching flags. Keep the semantic
  operation first-class until ARM64 emits one MOVK, retain the mask/OR polyfill for other hosts,
  and preserve the one-instruction `AND #0xffff` path for immediate zero because physical A510
  measurements rejected MOVK there. Require actual-JIT disassembly, dirty-input and flags/FPSCR
  coverage, and repeated measurements on every Thor core class.
- Fuse redundant unsigned-then-signed narrowing only when IR use data proves the signed extension
  immediately consumes the sole byte/halfword result. `UXTB`/`UXTH` and `SXTB`/`SXTH` are aliases
  of the baseline `UBFM`/`SBFM` family documented on X3 page 18, A715 page 20, A710 pages 27-28,
  and A510 pages 22-23. One `SXTB`/`SXTH` has the exact semantics of the consecutive pair, while
  removing a dependency and an integer-pipeline operation. Do not apply this to shift counts or
  other U8/U16 consumers that require canonical zero extension; preserve a dirty-upper-bits guest
  regression alongside the fused signed cases.
- Apply the same use-data discipline to ordinary A32 byte and halfword stores. AArch64 `STRB` and
  `STRH` architecturally consume only the low 8 or 16 source bits, so a sole matching store
  consumer can use the raw word without first issuing the `UBFM` alias `UXTB`/`UXTH`. The same X3
  page 18, A715 page 20, A710 pages 27-28, and A510 pages 22-23 rows prove that this removes a real
  integer operation, but they do not imply that a store-throughput-limited loop gets faster. Keep
  canonical narrowing for shared/non-store consumers, exclusive writes, mismatched widths, and
  endian-reversal paths, and verify both callback and fastmem stores with dirty upper bits.
- Fold ordinary A32 signed byte/halfword reads into native `LDRSB`/`LDRSH` only when a matching
  sign extension is the load's sole immediately following consumer. The checked basic register-
  offset rows list the same latency/throughput for unsigned and signed narrow loads: 4/3 on X3
  page 19, 4/3 on A715 page 21, 4/3 on A710 page 29, and 2/2 on A510 page 24. This makes the native
  signed load the manual-supported way to remove the separate `SBFM` alias `SXTB`/`SXTH`, although
  the heterogeneous cores can expose very different loop-level gains. Preserve explicit sign
  extension in callback/fault fallbacks and reject shared, ordered, exclusive, endian-reversed,
  non-adjacent, or non-A32 shapes. Require actual JIT opcodes, matched checksums, and guest-state
  coverage rather than inferring a whole-game or battery-watt result from the table.
- Reuse that canonical `UXTB` result in a following no-carry variable shift instead of masking it
  a second time. The logical-instruction tables list the removed `AND` at latency/throughput 1/6
  on X3 page 15, 1/4 on A715 and A710 page 17, and 1/3 on A510 page 14. The variable-shift rows list
  the surviving `LSLV`/`LSRV`/`ASRV` at 1/6 on X3 page 18, 1/4 on A715 page 20 and A710 page 27,
  and 1/3 on A510 page 22. Both consume the same integer/ALU resources on each core, so removing
  the proven duplicate reduces issue work and the shift-count dependency. Do not generalize this
  to callback-returned or otherwise unknown U8 values; retain their mask and all carry paths.
- For a byte-sized A32 register count whose sole consumer is LSL, LSR, or ROR, avoid materializing
  `UXTB` as well. AArch64 `LSLV`/`LSRV`/`RORV` consume only bits 4:0. For LSL/LSR, `TST #0xe0`
  ignores dirty source bits above the low byte while separating valid 0..31 counts from A32's
  saturating 32..255 cases; ROR needs no range operation. The same X3/A715/A710/A510 tables above
  show that removing `UBFM` reduces real integer issue work. Keep ASR on the canonical path: the
  analogous raw-count clamp repeatedly regressed on A710 even though it helped A510. Preserve
  low-byte zero/range handling in carry-producing shifts and reject any extension that lacks
  dirty-upper-bit result-and-carry coverage.
- Preserve signedness when moving x86 integer-to-float lowering to AArch64. PICA `LG2` subtracts the
  IEEE-754 exponent bias, so values below one produce a negative GPR exponent and require `SCVTF`,
  not `UCVTF`. Convert directly from the GPR instead of first moving the bits into a SIMD lane. The
  checked conversion tables list both signed and unsigned forms across X3 pages 28-29, A715 pages
  30-31, A710 pages 46-47, and A510 pages 39-40; the choice is semantic, not interchangeable.
- For the PICA CPU-fallback vertex cache's tiny fully associative `u16` scan, compare sixteen IDs
  per band with two ordinary Q loads, narrow the two halfword equality masks with `XTN`/`XTN2`,
  select lane indices, and use one byte `UMINV` to recover the first match. The checked manuals put
  ordinary vector loads on X3 page 23, A715 page 26, A710 page 39, and A510 page 32; integer
  reductions on pages 26, 29, 43, and 36 respectively; and the relevant select/narrow operations
  on X3 pages 31-32, A715 pages 34-35, A710 pages 52-53, and A510 pages 43-44. Keep one reduction
  per sixteen entries—especially on the A510, where the reduction has the highest listed latency—
  and retain scalar handling for the short tail. Prove first-match and every valid-prefix length
  against a scalar reference before relying on the manual-backed instruction shape.
- When four float routes may all be silent, compare one loaded Q vector against zero with `FCMEQ`
  and reduce the equality mask with 4S `UMINV`. This treats both signs of zero as silent while any
  nonzero value or NaN remains audible. Use the shortcut only when state transitions remain exact,
  and verify the linked binary because a scalarized predicate erases much of the front-end win.
- When an exact frame-wide predicate proves a group of planar outputs unused, specialize that group
  out instead of multiplying by zero and still loading/storing its buffers. The A510, A710, A715,
  and X3 load/store tables all show vector memory operations consuming load/store and vector-side
  resources; eliminating the operation is portable across Thor's heterogeneous cores. Preserve a
  full path for every nonzero or NaN value and verify the linked loop rather than assuming template
  specialization removed the memory traffic.
- When the destination is known to be zero and the first contribution is already clamped to its
  final lane width, store that contribution directly instead of clearing, reloading, and performing
  a saturating add against zero. This equivalence applies only to the first contribution: retain
  the original clamp-and-saturating-add order for every later contribution, preserve NaN routing,
  and explicitly clear the destination when no contribution is active. Keep the branch outside the
  sample loop and inspect final ThinLTO to confirm the direct variant has no destination load/add.
- Do not stage a complete planar frame into persistent state merely to consume it once in the same
  tick. When the producer's lifetime covers the consumer, route its const view directly and retain
  staging only for data that truly crosses an ownership or endian boundary. Preserve serialized
  field layout when compatibility requires it, and prove that loaded/stale fields are overwritten
  or bypassed before use. Count both the load and store sides of every removed copy when estimating
  memory-system work, then confirm the linked call sites actually disappeared.
- A native-endian shared-memory view should use one pointer per channel rather than flattening
  nested arrays across subobject boundaries. Keep an explicit endian-converting fallback for other
  hosts, and inspect linked AArch64 to prove those pointers load once before the vector loop.
- For the fixed-point Y2R video/camera block, widen eight unsigned byte samples to signed halfwords,
  then use `SMULL`/`SMLAL`/`SMLSL` into signed 32-bit lanes before the original arithmetic shifts
  and saturating narrows. The widening multiply tables are on X3 pages 27-28, A715 page 29, A710
  page 43, and A510 page 36; ZIP and narrowing tables are on X3 pages 31-32, A715 pages 34-35,
  A710 pages 52-53, and A510 pages 43-44. Duplicate subsampled chroma in registers and pack exact
  `0xRRGGBB00` words with ZIPs plus contiguous stores, avoiding the A510's `1/25` D-form byte `ST4`.
  Validate all coefficient extremes and inspect final ThinLTO because this equivalence depends on
  the two signed shift stages and saturating to `[0,255]` only at the output.
- Schedule latency-critical work on fast cores only when measurement proves a benefit. Unnecessary affinity and waking idle cores can increase power.
- Do not compile the entire Android binary for Cortex-X3. The SoC is heterogeneous and the shipping device does not expose every optional Arm feature, including SVE/SVE2.
- Optional ARM crypto instructions follow the same heterogeneous-core rule. Crypto++ compiles CRC32
  and PMULL only in dedicated translation units and selects them through Android CPU-feature checks;
  keep the generic callers baseline AArch64. Its CMake `try_compile` programs use installed-style
  `<cryptopp/...>` includes, so every probe must receive the vendored public-header directory or a
  missing header will silently look like an unsupported instruction set.

## Nintendo 3DS guest CPU references

These describe processors emulated by Azahar, not the Snapdragon host. They are most useful for guest ISA correctness, cache/control behavior, WFE/WFI semantics, and deciding whether cycle-sensitive timing assumptions are justified. They do not imply that reproducing the guest pipeline on the host will improve performance.

| Document | Public source | Scope | Local research copy | SHA-256 |
| --- | --- | --- | --- | --- |
| Arm `ARM11 MPCore Processor Technical Reference Manual`, DDI 0360E | [Arm documentation service](https://documentation-service.arm.com/static/5e8e1cd9fd977155116a4a7a) | 730 pages; ARMv6K MPCore programmer model, memory system, coherency, WFE/WFI, pipeline, and cycle timings | `reference/manuals/arm11-mpcore-trm-ddi0360e.pdf` (3.82 MiB) | `15EA1BA2AEF0F6F756ABD70D0C67F3E26103CB28EF3061660864E7F0D0E419B4` |
| Arm `ARM946E-S Technical Reference Manual`, DDI 0201D | [Arm documentation service](https://documentation-service.arm.com/static/5e8e3ee588295d1e18d3aa82) | 218 pages; ARMv5TE programmer model, caches, MPU, TCM, bus behavior, and control registers | `reference/manuals/arm946e-s-trm-ddi0201d.pdf` (1.48 MiB) | `FB45E13849688DCB8165CDF1C5CE0849492A64712AC5F094EECC23955A6D043F` |

The ARM11 timing tables show a three-pipeline design with instruction-dependent interlocks and forwarding. Use those facts to validate timing-sensitive titles or instrumentation. Azahar's ARM64 JIT should still be optimized from measured host hot paths rather than by imitating the guest microarchitecture.

## Nintendo 3DS PICA200 GPU references

No complete, public PICA200 technical reference manual was found. Do not obtain or redistribute Nintendo SDK documentation. The closest lawful public vendor material is the DMP-authored PICA200 slide in the SIGGRAPH 2007 mobile 3D course notes; it is a high-level, pre-3DS overview rather than a register or shader manual.

| Document | Public source | Scope | Local research copy | SHA-256 |
| --- | --- | --- | --- | --- |
| `Mobile 3D Graphics API, Architecture and Roadmap`, SIGGRAPH 2007 course notes | [MIT-hosted course PDF](https://people.csail.mit.edu/kapu/siggraph_2007/mob3D_SG07_notes.pdf) | 495 pages; DMP page 67 identifies PICA200 features, MAESTRO extensions, nominal throughput, clock range, and power range | `reference/manuals/pica200-mobile-3d-siggraph-2007-notes.pdf` (9.91 MiB) | `90289889966392C2BD799CDC29053CC84A75AE50369E41137273D1C881CCE951` |

For command-level behavior, use the project's PICA implementation and hardware-tested community references, while treating unknowns as experimental rather than official: [internal registers](https://www.3dbrew.org/wiki/GPU/Internal_Registers), [external registers](https://www.3dbrew.org/wiki/GPU/External_Registers), and [shader instruction set](https://www.3dbrew.org/wiki/GPU/Shader_Instruction_Set). The hardware-tested floating-point notes are particularly important because PICA200 behavior is not generally interchangeable with host IEEE floating point.

## AYN Thor device reference

| Document | Public source | Scope | Local research copy | SHA-256 |
| --- | --- | --- | --- | --- |
| `AYN THOR Handheld Game Console User Manual` | [FCC exhibit record](https://apps.fcc.gov/eas/GetApplicationAttachment.html?id=8915262) ([mirror metadata](https://fccid.io/2BDXNBASE/User-Manual/UserManual-8915262)) | 11 pages; controls, Thor settings, display modes, calibration, device specifications, and regulatory information | `reference/manuals/ayn-thor-user-manual.pdf` (859 KiB) | `8714F2F6A70646AE79C6D39BA70EF8FE83DBD9109B2C32FFAED0C129F7CB92ED` |

The filing confirms the Base/Pro/Max Snapdragon 8 Gen 2 and Adreno 740 target, active cooling, 6000 mAh battery, 120 Hz primary display, and 60 Hz secondary display. It also documents controller calibration, video-output selection, and Thor settings that may matter during repeatable tests. Its hash matches the identifier used by the public manual mirror, so it is the same 11-page document reviewed from the user-provided context.

The manual reports UFS 4.0, but [AYN's current product page](https://www.ayntec.com/products/ayn-thor) lists UFS 3.1 for Lite, Base, Pro, and Max. Treat UFS generation as unverified until the connected device reports enough evidence to resolve the conflict.
