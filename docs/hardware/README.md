# Snapdragon 8 Gen 2 Reference Index

The AYN Thor Base/Pro/Max target is Snapdragon 8 Gen 2 (`kalama`/SM8550-class), with one Cortex-X3, two Cortex-A715, two Cortex-A710, three Cortex-A510 cores, and Adreno 740. The vendor documents below are held in the sibling `ps3-thor/rpcsx-ui-android/docs/hardware` research library.

The PDFs are not copied into this public fork yet. Their redistribution terms are not explicit in the files, and the set is 26.8 MiB. This index records exact inputs so they cannot be confused with an unverified download; vendor them only after an explicit source-policy decision.

| Document | Revision or scope | Size | SHA-256 | Azahar relevance |
| --- | --- | ---: | --- | --- |
| `qualcomm_adreno_game_developer_guide.pdf` | Qualcomm 80-78185-2, 200 pages | 20.17 MiB | `0872AA49B763ACB46AEB7427784E926D2BF3939F2E731B405DEF977A5BFAECAC` | GMEM, render passes, resolves, concurrent binning, LRZ, and mobile bandwidth |
| `qualcomm_snapdragon_opencl_optimization_guide.pdf` | Qualcomm 80-NB295-11 Rev. C, 116 pages | 1.71 MiB | `59CEEE4F9E33686CEB5F2970045C77858B4A395885778C5944C3130E590EEB3A` | Hardware cache/UMA guidance; OpenCL API advice is out of scope |
| `arm_cortex_x3_software_optimization_guide.pdf` | Cortex-X3 | 1.12 MiB | `3EC100F2BBCD4DE004E1730553A3276BB27ECA4B320471B177BBDB843B8761D9` | Prime-core instruction throughput and pipelines |
| `arm_cortex_a715_software_optimization_guide.pdf` | Cortex-A715 | 1.16 MiB | `D6D7A49F34528B79E1F8C8E0B02D59D3DA6011D719D3271D310A4D83EC8F6FA2` | Newer performance-core pair |
| `arm_cortex_a710_software_optimization_guide.pdf` | Cortex-A710 | 1.39 MiB | `096B9C2924BBFA4C5045D2C2F1C711D6E544C28F2F04ADAAD4B66B2E8C48CD6A` | Older performance-core pair |
| `arm_cortex_a510_software_optimization_guide.pdf` | Cortex-A510 | 1.22 MiB | `E80E25EFFBEE27FB95740420469846FA0B3211C5716757A464E5C32E63281D44` | Efficiency cores, shared vector resources, and background-work placement |

## Guidance already applied

- Keep compatible render work inside render passes/GMEM and avoid unnecessary resolves or mid-pass dependencies.
- Use Snapdragon Profiler to verify concurrent binning; do not infer it from source structure alone.
- Treat memory writes, texture uploads, and CPU wakeups as power costs, not only frame-time costs.
- Schedule latency-critical work on fast cores only when measurement proves a benefit. Unnecessary affinity and waking idle cores can increase power.
- Do not compile the entire Android binary for Cortex-X3. The SoC is heterogeneous and the shipping device does not expose every optional Arm feature, including SVE/SVE2.
