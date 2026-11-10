# ADR 0005: Freeze the KRO v1 object layout

- Status: accepted
- Date: 2026-11-09
- Decision: D-013 (object format)
- Supersedes: none

## Context

M3 needs a relocatable object that the in-tree assembler can write and `krld` can read. `docs/object.md` described the required fields but deliberately left exact offsets and table order open. Code must not depend on a draft.

## Decision

KRO v1 uses the exact layout in [docs/object.md](../object.md):

- a 120-byte little-endian header with `KRO\0` magic, version 1.0, target/ABI tags, counts, table locations, entry/image base, file size, build ID, and CRC32;
- fixed 64-byte section records, 40-byte symbol records, and 32-byte relocation records, in that order immediately after the header;
- the string table after the relocation table, then 8-byte-aligned section payloads in section-index order;
- CRC32 covers the file with the CRC field zeroed;
- symbols are ordered globals-by-name then locals-by-section/offset, strings in first-use order, sections in writer order;
- the base relocation registry is absolute 8/16/32/64, PC-relative 8/16/32/64, and KR32 absolute-32.

## Consequences

The assembler and linker can be implemented against stable byte offsets. Adding a section type, symbol flag, or relocation type is a versioned format change with an ADR. The format is deliberately static-link-only in v1.

## Alternatives considered

- Freeze `docs/object.md` without exact offsets: rejected because two implementations could disagree on padding and table order.
- Use a target-native executable as the object: rejected because the linker must be target-tagged and shared.
- Adopt ELF: rejected because the plan requires a small, target-tagged, self-owned format.
