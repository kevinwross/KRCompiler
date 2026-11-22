# 0054 — The kr216 executable image container

- Status: accepted (2026-11-22, at the M3 close)
- Date: 2026-11-22
- Decision: fixes the byte layout of the kr216 loadable image, the fields the loader validates, and the reset-vector convention, so the compiler's image writer and the emulator's loader are written against one specification
- Scope: the loadable image only. KRO relocatable objects are unchanged (ADR 0005); the 16-bit exception descriptor remains ADR 0055.

## Context

`../kr216/kr216-plan.md` D33 says the kr216 image is "a KRCompiler container with magic,
target/ABI version, sections, entry, image base and the embedded KDBG payload, like KR32's
`.k32`", but the plan fixes the shape, not the bytes. Two repositories need the bytes at
once: the compiler's image writer (kr216-plan M2) and this repository's loader (M1.4). The
loader cannot be tested against a real image until the layout exists, so the layout is
written here before either implementation.

KR16's `.kex` container (`src/linker/kr16_image.cpp`) already establishes the house style: a
64-byte little-endian header, a section table of 32-byte records, a string table, and a
magic that names the target. kr216 follows it field for field, with a target ID and an ABI
version because the plan's KRO container carries both.

## Decision

### Header (64 bytes, little-endian)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | magic | `K216` |
| 4 | 2 | container version | `1` |
| 6 | 2 | kind | `2` = executable |
| 8 | 4 | flags | bit 0: a debug payload is present |
| 12 | 4 | header size | `64` |
| 16 | 2 | entry | absolute address of the first instruction |
| 18 | 2 | ABI version | `1` |
| 20 | 2 | section count | |
| 22 | 2 | target ID | `4` (kr216) |
| 24 | 4 | image base | the linker's load base, for diagnostics only |
| 28 | 4 | section table offset | |
| 32 | 4 | reserved | zero |
| 36 | 4 | string table offset | |
| 40 | 4 | string table size | bytes |
| 44 | 4 | file size | bytes |
| 48 | 4 | CRC32 | of the whole file with this field zeroed; `0` means absent |
| 52 | 12 | reserved | zero |

### Section record (32 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | name: offset into the string table |
| 4 | 2 | type: `1` progbits, `8` nobits |
| 6 | 2 | flags: `1` alloc, `2` write, `4` execute, `8` debug |
| 8 | 2 | address: absolute in the 64 KiB space; `0` for an unallocated section |
| 10 | 2 | reserved, zero |
| 12 | 4 | file offset of the payload |
| 16 | 4 | size in bytes |
| 20 | 2 | alignment, a power of two |
| 22 | 10 | reserved, zero |

### Loader rules

- Validate the magic, container version `1`, kind `2`, header size `64`, target ID `4`, ABI
  version `1`, and `file size` against the buffer. A mismatch is an error naming the field,
  never a partial load.
- Validate the section table and string table lie inside the file, and that every section's
  payload lies inside the file.
- An **allocated** section must lie in `0x0100`–`0xFEFF` and must not overlap another
  allocated section. `nobits` sections have no payload and are zeroed.
- The **entry** must be non-zero and inside an executable allocated section.
- The loader writes the entry into the word at `0x0100` (the reset vector) before resetting
  the machine, so the ISA's reset rule and the container's `entry` cannot disagree. An image
  whose bytes at `0x0100` differ is not an error: the container is the authority.
- A **debug** section is unallocated and is not loaded into memory. The loader reports its
  file offset and size for the debugger to read.
- CRC32, when present, is verified before any section is copied.
- Sections are copied in file order; a later section never silently overwrites an earlier
  one because overlap is rejected first.

### Writer duty

The compiler's kr216 image writer emits exactly this layout: `crt0` and the program in one
executable `.text`, read-only data in `.rodata`, initialised data in `.data`, zeroed data in
`.bss` (nobits), and the KDBG payload in `.kdbg` (debug, unallocated), with the KRO target
ID 4 in the object records (ADR 0053). The writer also writes the entry word at `0x0100`
for the emulator's benefit, even though the loader does not depend on it.

## Consequences

- The loader and its tests can be written now, against a container that any implementation
  can produce; the M2 writer has a specification instead of a convention.
- The layout is deliberately a sibling of `.kex`, so one reader shape covers both and the
  differences are the magic, the target ID and the ABI version.
- The image base is diagnostic: sections carry absolute addresses, so a relinked image whose
  base moved changes its section addresses rather than needing a relocation pass. kr216 has
  a 64 KiB flat space and no position independence (plan section 7.1), so this is not a
  limitation in practice.
- The container is frozen once the writer and loader agree. A change is a container version
  bump with a compatibility note.

## Alternatives considered

- **Reuse `.kex` unchanged.** Rejected: its magic says KR16, it has no target ID or ABI
  version, and the loader must reject an image built for another machine. The plan requires a
  target-tagged container.
- **ELF.** Rejected by the plan (section 7.1): the image is 64 KiB, statically linked, and
  needs no dynamic loader, symbol versions or program headers.
- **A raw binary with an entry on the command line.** Rejected: it loses the section table,
  the debug payload and the target check, exactly the metadata the debugger and the loader
  need.
- **Put the debug payload in a sidecar only.** Rejected: the plan requires the payload in the
  image by default (D31), with a sidecar as an option.
