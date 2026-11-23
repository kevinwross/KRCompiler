# 0058 — The kr232 executable image container

- Status: accepted (2026-11-22, at M0)
- Date: 2026-11-22
- Decision: fixes the byte layout of the `.k232` loadable image, the fields the loader validates and the reset-vector convention, so the kr232 plan's writer (krcompiler, M2) and the emulator's loader (kr232, M2) are written against one specification
- Scope: the loadable image only. KRO relocatable objects are unchanged (ADR 0005); the target profile and ABI appendix are ADR 0057.

## Context

`kr232-plan.md` §5 requires a second ADR beside the target profile, because the
image crosses two repositories: the writer lands in krcompiler and the loader in
kr232-emu, and neither can be tested against the other's reading. ADR 0054 did
the same for `K216`, and the kr232 drafts fix what the bytes must carry: the
reset word at `0x0100` (ISA §2.4, §12), the sixteen vectors that follow it, and
a `.kdbg` payload whose `image_base` krdbg validates against the container.

The house style is KR16's `.kex` and KR216's `K216`: a 64-byte little-endian
header, 32-byte section records, a string table, a magic that names the target,
and a CRC32. `.k232` keeps all of it and widens the address fields to 32 bits,
because kr232's space is 4 GiB rather than 64 KiB.

## Decision

### Header (64 bytes, little-endian)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | magic | `K232` |
| 4 | 2 | container version | `1` |
| 6 | 2 | kind | `2` = executable |
| 8 | 4 | flags | bit 0: a debug payload is present |
| 12 | 4 | header size | `64` |
| 16 | 4 | entry | absolute 32-bit address of the first instruction |
| 20 | 2 | ABI version | `1` |
| 22 | 2 | section count | |
| 24 | 2 | target ID | `5` (kr232) |
| 26 | 2 | reserved | zero |
| 28 | 4 | image base | `0` for a kernel image, `0x08000000` for a user image |
| 32 | 4 | section table offset | |
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
| 8 | 4 | address: absolute in the 4 GiB space; `0` for an unallocated section |
| 12 | 4 | file offset of the payload |
| 16 | 4 | size in bytes |
| 20 | 2 | alignment, a power of two |
| 22 | 10 | reserved, zero |

### Loader rules

- Validate the magic, container version `1`, kind `2`, header size `64`, target ID `5`, ABI
  version `1`, and `file size` against the buffer. A mismatch is an error naming the field,
  never a partial load.
- Validate that the section table and the string table lie inside the file, and that every
  section's payload lies inside the file.
- An **allocated** section must lie wholly inside the region its `image base` names — kernel
  RAM `0x00000100`–`0x07FFFFFF` for a kernel image, user RAM `0x08000000`–`0x0FFFFFFF` for a
  user image — and must not overlap another allocated section. `nobits` sections have no
  payload and are zeroed.
- The **entry** must be non-zero and inside an executable allocated section.
- A **kernel image** (`image base` 0): the writer emits a 4-byte `.reset` section at `0x0100`
  holding the entry, and the loader writes the header's entry into the word at `0x0100`
  before reset, so the header and the ISA's reset rule cannot disagree. An image whose bytes
  at `0x0100` differ from the header is not an error; the header is the authority.
- A **user image** (`image base` `0x08000000`): the loader maps the sections and rewrites
  nothing. The kernel builds the trap frame that enters at the header's entry, because the
  ISA leaves how a user image is loaded to the kernel (§12).
- A **debug** section is unallocated and is not loaded into memory; the loader reports its
  file offset and size for the debugger to read.
- CRC32, when present, is verified before any section is copied.
- Sections are copied in file order; overlap is rejected before anything is written, so a
  later section never silently overwrites an earlier one.

### Writer duty

The kr232 image writer emits exactly this layout: `crt0` and the program in one executable
`.text`, read-only data in `.rodata`, initialised data in `.data`, zeroed data in `.bss`
(nobits), the KDBG payload in `.kdbg` (debug, unallocated) with `image_base` set to the
header's base, and the 4-byte `.reset` section for a kernel image. KRO objects and the
`.kdbg` header carry target ID 5 (ADR 0057).

### Relationship to the ISA

The container never contradicts the ISA: the reset word is at `0x0100` because ISA §2.4 says
so, vector 0 occupies it and the fifteen further vectors follow, and a kernel is linked at
`0x0200` because that is the linker's default the ISA records (§12). An image that places
code inside the null page is rejected like any other out-of-region section.

## Consequences

- The writer and the loader can be written and tested in different repositories against one
  byte layout, which is the reason this ADR exists before either exists.
- A user image and a kernel image share one reader and differ only in `image base` and the
  reset-word rule, so `--user` costs the loader one branch rather than a second format.
- The layout is a sibling of `.kex` and `K216`: one reader shape, three magics. The address
  fields are 32-bit here and 16-bit there, which is the only structural difference.
- The container is frozen when the writer and loader agree; a change is a container version
  bump with a compatibility note.
- `image_base` is diagnostic for a kernel image and load-bearing for a user image, so a
  reviewer sees one field with two behaviours; the loader rules above are the contract.

## Alternatives considered

- **Reuse `K216` unchanged.** Rejected: its magic names another machine, its entry and
  addresses are 16-bit, and its loader rule confines sections to a 64 KiB space.
- **Reuse `.kex`.** Rejected for ADR 0054's reasons: no target ID, no ABI version, and a
  magic that names KR16.
- **A sidecar-free format with no CRC.** Rejected: the CRC costs one pass and turns a
  truncated download into a load-time error instead of a wild jump.
- **Two formats, one for the kernel and one for user programs.** Rejected: they differ in a
  base and one rule, and two formats would drift.
- **Let the kernel parse `.k232` itself from a raw blob.** Rejected: it moves a byte layout
  into guest code, where it is neither testable from the host nor visible to the debugger.
