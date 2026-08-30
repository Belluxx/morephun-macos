# Modding Library — Phase 2

## Goal

Make the modding library less dependent on V-Rally-specific constants by
introducing target descriptions and more meaningful symbol resolution.

## Scope

- Introduce a practical way to describe supported game targets and the symbols
  or metadata needed to modify them.
- Move V-Rally-specific IDs, offsets, and related knowledge out of the generic
  patching code and into a target-specific definition.
- Prefer named or discoverable symbols over fixed IDs and offsets where that
  improves clarity and portability.
- Provide enough target detection and compatibility handling to avoid applying
  a mod to an obviously unsuitable input.
- Improve inspection tooling so developers can understand the selected target
  and the symbols used during patching.
- Consider a lightweight way to recognize previously modified files if it fits
  naturally with the chosen design.

The exact profile format, matching strategy, command-line interface, and amount
of validation are implementation choices. They should fit the existing library
and remain easy to extend for future games or releases.

## Example Workflow

```text
mophun-mod inspect game.mpn
mophun-mod inject game.mpn output.mpn --target vrally2-rc14eu-m5
```

These commands are illustrative rather than a required interface.

## Deliverables

- Target-aware symbol or metadata support in `libmophunmod`.
- A target definition for the currently supported V-Rally 2 release.
- Useful inspection support and understandable failure messages.
- Tests covering the main behavior of the chosen design.

## Completion Criteria

- Game-specific values are reasonably isolated from the reusable patching code.
- The existing V-Rally mod continues to work with the new target-aware approach.
- Clearly incompatible inputs fail safely.
- The resulting design provides a useful foundation for adding another target
  without prescribing how every future target must be represented.
