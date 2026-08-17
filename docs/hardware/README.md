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
| `arm_cortex_x3_software_optimization_guide.pdf` | Cortex-X3 | 1.12 MiB | `3EC100F2BBCD4DE004E1730553A3276BB27ECA4B320471B177BBDB843B8761D9` | Prime-core pipelines; `UMINV`/`FCMEQ` tables on pages 26 and 28, D24 conversion/divide on pages 28-29, table/narrow/ZIP operations on pages 31-32, and exact structured loads/stores on pages 33-36 |
| `arm_cortex_a715_software_optimization_guide.pdf` | Cortex-A715 | 1.16 MiB | `D6D7A49F34528B79E1F8C8E0B02D59D3DA6011D719D3271D310A4D83EC8F6FA2` | Newer performance-core pair; `UMINV`/`FCMEQ` tables on pages 29-30, D24 conversion/divide on pages 30-32, table/narrow/ZIP operations on pages 34-35, and exact structured loads/stores on pages 36-39 |
| `arm_cortex_a710_software_optimization_guide.pdf` | Cortex-A710 | 1.39 MiB | `096B9C2924BBFA4C5045D2C2F1C711D6E544C28F2F04ADAAD4B66B2E8C48CD6A` | Older performance-core pair; `UMINV`/`FCMEQ` tables on pages 43 and 46, D24 conversion/divide on pages 46-48, table/narrow/ZIP operations on pages 52-53, and exact structured loads/stores on pages 55-60 |
| `arm_cortex_a510_software_optimization_guide.pdf` | Cortex-A510 | 1.22 MiB | `E80E25EFFBEE27FB95740420469846FA0B3211C5716757A464E5C32E63281D44` | Efficiency cores and shared vector resources; `UMINV`/`FCMEQ` tables on pages 36 and 39, D24 conversion/divide on pages 39-40, table/narrow/ZIP operations on pages 43-44, and exact structured loads/stores on pages 45-49; D-form byte/halfword `ST3`, D-form `ST4`, Q-form `ST4`, and four-table `TBL` are especially slow at `1/17`, `1/25`, `1/50`, and `1/9` throughput |

## Guidance already applied

- Keep compatible render work inside render passes/GMEM and avoid unnecessary resolves or mid-pass dependencies.
- Use Snapdragon Profiler to verify concurrent binning; do not infer it from source structure alone.
- Treat memory writes, texture uploads, and CPU wakeups as power costs, not only frame-time costs.
- Prefer vector permutation plus ordinary contiguous stores over byte/halfword `ST3` or `ST4`
  when the exact layout permits it; the A510 structured-store path is exceptionally slow.
- For packed D24 Morton to D32-float conversion, use D-form `LD3`, one-table shuffles, and
  ZIP/narrow operations across a two-row band. Do not collapse the mapping into a four-table `TBL`:
  Cortex-A510 documents that form at latency 16 and throughput `1/9`. Preserve true vector `FDIV`
  and `FCVTZU`; reciprocal approximations or changed rounding are not equivalent depth math.
- For packed S8D24 staging, prefer ordinary paired loads plus narrowing/`UZP` and contiguous plane
  stores over `LD4`; A510 lists one-register `LD1` at `2/cycle` but Q-form byte `LD4` at `1/3`.
- For interleaved stereo HLE audio that feeds planar mix buses, unroll eight samples and use
  ordinary paired Q loads plus `UZP` before sharing widening/conversion work across outputs. The
  A710, A715, and X3 guides recommend loop unrolling and non-writeback `LDP`/`STP` for throughput;
  final ThinLTO must confirm this lowering instead of assuming an intrinsic avoids `LD2`.
- For final HLE audio downmix that must read and write interleaved stereo, widen D-form `LD2`/`ST2`
  to Q-form and handle eight samples per loop. The exact tables above show equal or better useful
  bytes per cycle for Q form on all four Thor core classes. Do not replace the structured pair with
  ordinary loads plus `UZP`/`ZIP`: that adds permutation work without removing an expensive
  multiway transpose. Confirm Q-form instructions and spill-free loops in final ThinLTO.
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
- Schedule latency-critical work on fast cores only when measurement proves a benefit. Unnecessary affinity and waking idle cores can increase power.
- Do not compile the entire Android binary for Cortex-X3. The SoC is heterogeneous and the shipping device does not expose every optional Arm feature, including SVE/SVE2.

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
