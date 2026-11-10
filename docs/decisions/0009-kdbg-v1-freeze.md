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

`KDBG` v1 is one debug payload carried as the `.kdbg` section of a `.k32`
image. The exact 64-byte header, 32-byte section directory, NUL string table,
record encoding (`tag u16`, `flags u16`, `length u32`, payload), section names,
record payloads, CRC32 rules, and deterministic ordering are specified in
[docs/symbols.md](../symbols.md).

`krld` builds the payload from `.debug_info` after linking; source labels are
resolved against the linked symbol table. `krc --dump-symbols` decodes it to
text or the stable JSON form. `src/debug/kdbg.*` is the shared reader/writer.

The `.debug_info` bridge remains until the KR32 debugger consumes KDBG
directly; it is not part of the KDBG contract.

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
