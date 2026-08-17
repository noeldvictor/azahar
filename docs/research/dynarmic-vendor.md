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

The FastDispatch table is 65,536 16-byte entries (1 MiB per A32 address space). Its
hash mixes the upper location descriptor into the guest PC and discards the always-zero
ARM alignment bit. Table size and hash changes require matched Thor measurements; they
must not be treated as wins from static inspection alone.

The focused Thor microbenchmark measured 1.89x dispatch throughput in its stable
CPU7-pinned reverse-order sample (4.015 ms to 2.128 ms per million indirect dispatches),
with 1.69x to 1.95x observed across all sample groups. This number applies only to the
dispatcher-saturated loop; it is not an emulator-wide FPS or power result.

The imported upstream `master` was checked again on 2026-08-16 and still resolved to
`e77b1ba0b7da7cbe93021b01a663acfe7c4dd516`, so no later upstream Dynarmic change was
available to replace these local A32 ARM64 changes.

## Updating

Use command-line Git to clone the desired upstream revision recursively into a temporary
directory, compare it with this vendored tree, and import only an intentionally reviewed
update. Preserve the Thor changes or replace them with equivalent upstream code, run the
ARM64 Dynarmic tests, build the release-style Android APK, and test it on the Thor before
committing directly to `master`. Delete the temporary clone and generated build trees
after verification.
