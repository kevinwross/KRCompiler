# ADR 0009: Freeze KDBG v1

- Status: accepted
- Date: 2026-11-09
- Decision: freezes the `.kdbg` payload, header, section table, record streams, and CRC
- Supersedes: none

## Context

M4 needs one debug format with a shared reader/writer. The textual
`.debug_info` bridge from ADR 0008 proves the debugger session but is a
KR32-specific record stream and cannot be the shipping contract. The symbol
format must be versioned, checksummed, target-tagged, and deterministic.

## Decision

`KDBG` v1 is one debug payload carried as the `.kdbg` section of a `.k32` or
`.kex` image. The exact 64-byte header, 32-byte section directory, NUL string
table, record encoding (`tag u16`, `flags u16`, `length u32`, payload), section
names, record payloads, CRC32 rules, and deterministic ordering are specified in
[docs/symbols.md](../symbols.md).

`krld` builds the payload from `.debug_info` after linking; source labels are
resolved against the linked symbol table. `krc --dump-symbols` decodes it to
text or the stable JSON form. `include/krc/debug/kdbg.h` is the shared
reader/writer, shipped as `krc::debug`.

The `.debug_info` bridge remains until the KR32 debugger consumes KDBG
directly; it is not part of the KDBG contract.

## Amendment: image base in the reserved bytes (2026-11-20)

A `.kex` is mapped at the addresses in its section table rather than relocated at
load, so a payload's absolute addresses are correct only for the base it was
built for. The eight reserved header bytes at offset 56 now hold that base as a
little-endian `u64`, and a reader compares it with the container's base — the
`.k32` header's base field or the `.kex`'s first section address — rejecting a
mismatch. A payload recording zero predates the field and is accepted, so no
older image is invalidated. The version stays 1.0 and no record type changes;
readers that skip reserved bytes are unaffected. [docs/symbols.md](../symbols.md)
records the field.

## Amendment: KDBG v1.1 (2026-11-20)

KDBG gains a minor version, `1.1`, without changing a v1 record. The new facts
are new `.debug_info` letters (`P` for a register location, `K` for a lexical
scope, `W` for variable flags and scope, `A` for an array's element type and
count, `R` for a type reference) and the existing `C` record's base field now
written, plus new KDBG tags and two sections: `Location` 9 in `.kdbg_loc`,
`Scope` 10 in `.kdbg_scope`, `VarInfo` 11, `Base` 12, `ArrayType` 13,
`TypeRef` 14, and 64-bit variants `Line64` 15, `Function64` 16, `Symbol64` 17 and
`Location64` 18. `target_id` is written from the KRO target id (`1` KR32, `2`
KR16, `3` KR64).

The rules: a v1 reader skips unknown tags and sections and reports the v1
records; a v1.1 reader accepts a v1 payload with the new fields empty; a caller
that requires a v1.1 fact reports "newer KDBG" rather than guessing. No v1 tag
changes number or payload, so no v2 is implied by this amendment. The 64-bit
variants are written instead of the 32-bit records when the header's pointer
width is 8.

The format can now describe a register-held variable with a live range; the
compiler still does not create one. Enabling promotion or slot reuse remains
ADR 0052's separate decision, so no krc path emits `P` yet.

## Consequences

- KDBG images are self-describing, versioned, and checksummed.
- `krld` owns the bridge-to-KDBG conversion, so objects keep the frozen KRO
  format and the linker owns final addresses.
- The v1 subset covers sources, lines, functions, symbols, variables, types,
  members, and build metadata; location expressions, sidecars, strip, and
  scope/template records are post-v1.
- Writers and readers must reject unsupported majors, out-of-bounds records,
  and checksum failures rather than guessing.

## Alternatives considered

- **Keep the textual `.debug_info` stream as the contract.** Rejected: it is
  KR32-specific, unversioned, and parsed directly by the debugger.
- **Put KDBG sections in KRO objects.** Rejected: final addresses and source
  deduplication are linker responsibilities, and it would disturb the frozen
  object format.
- **Wait for the full location/scope model.** Rejected: it would leave M4
  without a real debug-format contract.
