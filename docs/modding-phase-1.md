# Modding Library — Phase 1

## Goal

Extract the reusable MPN patching machinery from `VRally2TurboMod` into
`libmophunmod` without changing the generated Turbo game.

## Scope

- Parse and validate uncompressed VMGP/MPN sections with strict bounds checks.
- Serialize modified MPNs while preserving padding and unknown trailing data.
- Extract a typed PIP2 assembler with labels, short/long fixups, and diagnostics.
- Add section allocators for guest code, initialized data, BSS, pool entries, and
  strings.
- Add helpers to import syscalls and replace a pool entry while retaining a
  callable reference to its original target.
- Refactor `VRally2TurboMod` to use the library. Keep cinematic generation and
  Turbo gameplay definitions mod-specific.
- Add synthetic tests for parsing, serialization, assembler fixups, allocation,
  hook preservation, malformed inputs, and deterministic output.

## Proposed Layout

```text
lib/mophunmod/
  mpn_image.{h,cpp}
  pip_assembler.{h,cpp}
  patch_builder.{h,cpp}
  pool_table.{h,cpp}
tools/
  vrally2_turbo_mod.cpp
tests/
  modding_*.cpp
```

## Deliverables

- A reusable `mophunmod` CMake library target.
- A smaller Turbo patcher built on its public API.
- Unit tests that require no proprietary game files.
- An optional local golden test for a user-supplied V-Rally MPN.

## Completion Criteria

- Existing project tests and all new library tests pass.
- The refactored patcher produces a byte-for-byte identical Turbo MPN from the
  current local inputs.
- Reapplying a patch or passing a malformed/unsupported MPN fails safely without
  modifying the input.
- No Turbo-specific behavior is added to the emulator runtime.
