# Modding Library — Phase 2

## Goal

Replace V-Rally-specific constants with explicit, verifiable target profiles and
named symbol resolution.

## Scope

- Define a versioned target-profile format containing:
  - target ID and supported runtime ABI;
  - original-file SHA-256 fingerprints;
  - expected MPN section properties;
  - named syscall imports;
  - named game functions, structures, and field offsets;
  - hook signatures and validation rules.
- Resolve syscalls by their string-table names instead of fixed pool IDs.
- Resolve game functions through profile symbols and verified byte signatures.
- Require every signature to have exactly one valid match.
- Add an injection marker containing runtime version, target ID, and patch ID so
  already-patched or incompatible files are rejected clearly.
- Extend inspection tooling to report the detected target and resolved symbols.
- Move all V-Rally values such as car update, flip hook, and car fields into an
  `vrally2-rc14eu-m5` profile.

## Proposed Interface

```text
mophun-mod inspect game.mpn
mophun-mod verify game.mpn --target vrally2-rc14eu-m5
mophun-mod inject game.mpn output.mpn --target vrally2-rc14eu-m5
```

## Deliverables

- Target-profile model, parser, validator, and resolver in `libmophunmod`.
- The first V-Rally 2 RC14EU M5 profile.
- Inspection and verification commands with actionable errors.
- Tests for exact matches, wrong releases, ambiguous signatures, changed imports,
  corrupt profiles, and already-patched inputs.

## Completion Criteria

- The Turbo patcher contains no raw V-Rally pool IDs, code offsets, or structure
  offsets outside its selected profile.
- Unsupported releases are rejected before any output is written.
- Profile and symbol resolution is deterministic across builds.
- The supported V-Rally input still produces the Phase 1 golden output.