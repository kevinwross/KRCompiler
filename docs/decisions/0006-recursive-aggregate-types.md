# ADR 0006: Recursive named aggregate construction

- Status: accepted
- Date: 2026-11-09
- Decision: extends the v1.0 IR type-construction API
- Supersedes: none

## Context

C records may contain pointers to themselves (`struct Node { struct Node *next; }`). The frozen IR core has named struct/union types, but `TypeContext::struct_type` requires the complete field list, so constructing a self-referential record would recurse forever. The type identity must exist before its fields can refer to it.

## Decision

`TypeContext` gains:

- `struct_forward(name)`, which creates or returns a named, incomplete struct type;
- `complete_struct(type, fields, field_names, packed)`, which fills in the fields of a previously forwarded named struct.

A named aggregate may be incomplete while its fields are being built. Any function, global, or instruction type that reaches code generation must be complete. No opcode, value, or textual type kind is added; the printer and `same_type` already identify named aggregates by name.

The backend treats an aggregate's fields in declaration order and applies natural alignment; the frontend remains authoritative for `sizeof`, `alignof`, and member offsets.

## Consequences

Self-referential C records can be lowered without target-specific side channels. The IR verifier/backend must reject an incomplete aggregate at a code-generation boundary. Unions and anonymous records can use the same forward/complete mechanism.

## Alternatives considered

- Represent records as byte arrays: rejected because it loses member types and prevents later aggregate/ABI work.
- Require pointers to be represented as opaque `ptr<i8>`: rejected because it breaks type checking and member GEPs.
- Add a new recursive type kind: unnecessary; a mutable completion step on named types is sufficient.
