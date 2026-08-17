# Hardware Reference Index

The AYN Thor Base/Pro/Max target is Snapdragon 8 Gen 2 (`kalama`/SM8550-class), with one Cortex-X3, two Cortex-A715, two Cortex-A710, three Cortex-A510 cores, and Adreno 740. The host vendor documents reviewed below are held outside this Git repository in the existing sibling personal research libraries recorded here.

Manual PDFs are deliberately not committed to this public fork. Their redistribution terms are not always explicit, and binary manuals make source history needlessly heavy. This index records exact external inputs so they cannot be confused with an unverified download.

## Snapdragon 8 Gen 2 host references

| Document | Revision or scope | Size | SHA-256 | Azahar relevance |
| --- | --- | ---: | --- | --- |
| `qualcomm_adreno_game_developer_guide.pdf` | Qualcomm 80-78185-2, 200 pages | 20.17 MiB | `0872AA49B763ACB46AEB7427784E926D2BF3939F2E731B405DEF977A5BFAECAC` | GMEM, render passes, resolves, concurrent binning, LRZ, and mobile bandwidth |
| `qualcomm_snapdragon_opencl_optimization_guide.pdf` | Qualcomm 80-NB295-11 Rev. C, 116 pages | 1.71 MiB | `59CEEE4F9E33686CEB5F2970045C77858B4A395885778C5944C3130E590EEB3A` | Hardware cache/UMA guidance; OpenCL API advice is out of scope |
| [Arm Architecture Reference Manual for A-profile architecture](https://developer.arm.com/documentation/ddi0487/ha), `arm-architecture-reference-manual-a-profile.pdf` | DDI 0487 H.a, 11,530 pages; sibling `xenia-thor` research library | 65.86 MiB | `62AF4D2F908347C3018BD63572F0CE1D5EDF0D34ABAE2BEE3C18DE7C97D06E43` | Sections C7.2.289, C7.2.309, C7.2.339, C7.2.390, and C7.2.403 define `SQDMULH`, saturating `SQXTUN`, byte-table `TBL`, lane-variable `USHL`, and `ZIP1` semantics used by exact audio and ETC1 SIMD paths |
| [Arm Architecture Reference Manual for A-profile architecture](https://developer.arm.com/documentation/ddi0487/mc), `arm_architecture_reference_manual_DDI0487M_c.pdf` | DDI 0487 M.c, 17,145 pages | 119.93 MiB | `B5F9DAA7EC0446777C8F848AA6431C99E5AB6554E5AA171B28B9016771494F8C` | Authoritative AArch64 instruction and memory-ordering semantics; sections C6.2.180 and C6.2.192 distinguish relaxed `LDADD`/ordinary loads from release/acquire variants |
| `arm_cortex_x3_software_optimization_guide.pdf` | Cortex-X3 | 1.12 MiB | `3EC100F2BBCD4DE004E1730553A3276BB27ECA4B320471B177BBDB843B8761D9` | Prime-core pipelines; SoundTouch FIR/WSOLA `ADDV` is on page 26, `SMLAL`/`SMULL` and late forwarding on pages 27-28, and Q-form `LD2` on page 33; D24 conversion/divide is on pages 28-29 and table/narrow/ZIP operations on pages 31-32 |
| `arm_cortex_a715_software_optimization_guide.pdf` | Cortex-A715 | 1.16 MiB | `D6D7A49F34528B79E1F8C8E0B02D59D3DA6011D719D3271D310A4D83EC8F6FA2` | Newer performance-core pair; SoundTouch FIR/WSOLA `ADDV` is on page 28, `SMLAL`/`SMULL` on page 29, and Q-form `LD2` on page 36; D24 conversion/divide is on pages 30-32 and table/narrow/ZIP operations on pages 34-35 |
| `arm_cortex_a710_software_optimization_guide.pdf` | Cortex-A710 | 1.39 MiB | `096B9C2924BBFA4C5045D2C2F1C711D6E544C28F2F04ADAAD4B66B2E8C48CD6A` | Older performance-core pair; SoundTouch FIR/WSOLA `ADDV` is on page 42, `SMLAL`/`SMULL` on page 43, and Q-form `LD2` on page 55; D24 conversion/divide is on pages 46-48 and table/narrow/ZIP operations on pages 52-53 |
| `arm_cortex_a510_software_optimization_guide.pdf` | Cortex-A510 | 1.22 MiB | `E80E25EFFBEE27FB95740420469846FA0B3211C5716757A464E5C32E63281D44` | Efficiency cores; SoundTouch FIR/WSOLA `ADDV` is on page 35, `SMLAL`/`SMULL` on page 36, and Q-form `LD2` on page 46; D24 conversion/divide is on pages 39-40 and table/narrow/ZIP operations on pages 43-44; D-form byte/halfword `ST3`, D-form `ST4`, Q-form `ST4`, and four-table `TBL` are especially slow at `1/17`, `1/25`, `1/50`, and `1/9` throughput |

## Guidance already applied

- Keep compatible render work inside render passes/GMEM and avoid unnecessary resolves or mid-pass dependencies.
- Use Snapdragon Profiler to verify concurrent binning; do not infer it from source structure alone.
- Treat memory writes, texture uploads, and CPU wakeups as power costs, not only frame-time costs.
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
- For converted linear RGB8, deinterleave each 48-byte BGR block with one Q-form `LD3` and assemble
  opaque RGBA with ZIPs. In the reverse direction, split each 48-byte BGR output into three exact
  adjacent-input `TBL2` maps rather than exposing all four RGBA input vectors to `TBL4`. X3,
  A715, and A710 list `TBL2` at latency/throughput 2/2 versus 4/`2/3` for `TBL4`; A510 lists
  8/`2/5` versus 16/`1/9`. The relevant Q-form `LD3` entries are on X3 page 34, A715 page 37,
  A710 page 56, and A510 page 46.
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
