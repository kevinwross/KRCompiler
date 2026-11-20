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
