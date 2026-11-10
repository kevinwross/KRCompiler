# ADR 0002: Freeze the target descriptor schema

- Status: accepted
- Date: 2026-11-09
- Decision: freezes target descriptor schema v1 in `docs/targets/schema.md`
- Supersedes: none

## Context

M2 adds typed IR, instruction selection, and the KR32 backend. Those pieces need a stable target-descriptor shape before they encode register, instruction, and ABI assumptions. The approved plan §4.2 names the descriptor sections but they were not yet written as one schema.

## Decision

`docs/targets/schema.md` is the frozen v1 schema. It fixes the section names, field names, field types, units, and invariants for identity, data model, registers, instructions, ABI, and capabilities. `schema_version` is `1`.

This ADR freezes the **shape**, not every target value. KR32/KR16/KR64 values continue in their target documents; the M2 backend fills the register and instruction tables and the M7 work fills the KR16 ABI values. Adding a field, changing a type, or changing a unit requires a new ADR and a schema-version bump.

## Consequences

M2 can implement `TargetDescriptor` and instruction-selection hooks without renegotiating their layout. The schema document becomes the review checklist for target changes. The cost is that a missing field now requires an explicit version bump instead of an informal edit.

## Alternatives considered

- Freeze the full KR32/KR16 register and instruction tables now: rejected because the KR32 encoding tables and KR16 ABI are M2/M7 deliverables; freezing guessed values would make later corrections look like ABI breaks.
- Keep the schema in `plan.md` only: rejected because implementers need field-level units and invariants that the plan intentionally leaves to the target docs.
