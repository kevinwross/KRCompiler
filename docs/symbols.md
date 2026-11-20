# KDBG symbol and debug format

**Status:** KDBG v1 frozen for the M4 subset. Images carry one `.kdbg` debug
section built from the textual `.debug_info` bridge. The bridge remains only to
feed the existing KR32 GUI/DAP debugger until it consumes KDBG directly. See
[ADR 0009](decisions/0009-kdbg-v1-freeze.md).

**Magic:** `KDBG`. **One format for every target and every debugger.**

## Image encoding

`.kdbg` is a single debug payload carried as a section of a `.k32` or `.kex`
image. Its 64-byte header is:

| Offset | Field |
|---|---|
| 0 | `KDBG` magic |
| 4 | major version (`1`) |
| 6 | minor version (`0`) |
| 8 | header size (`64`) |
| 12 | flags |
| 16 | target ID |
| 20 | ABI version |
| 24 | pointer width |
| 25 | endianness (`1` little) |
| 28 | build ID (8 bytes) |
| 36 | section count |
| 40 | section-table offset |
| 44 | string-table offset |
| 48 | string-table size |
| 52 | header CRC32 |
| 56 | image base (u64, little-endian; 0 = not recorded) |

Addresses in the payload are absolute in the image's load space. `attach_kdbg`
runs after layout, and a `.kex` is mapped at the addresses in its section table
rather than relocated at load. The image base the payload was built for is the
little-endian `u64` at offset 56; a reader compares it with the container's own
base — the `.k32` header's base field, or the address of the `.kex`'s first
section — and rejects a payload that records a different value. A payload that
records zero predates the field and is accepted unchanged, so the write of the
field does not invalidate an older image. No version bump and no record type is
involved.

The section table has 32-byte entries: name string offset, section type, flags,
payload offset, payload size, payload CRC32, and reserved words. A section is a
record stream of `tag u16`, `flags u16`, `length u32`, payload. Readers skip
unknown tags and reject out-of-bounds or checksum failures.

## Sections and records

| Section | Records |
|---|---|
| `.kdbg_src` | `Source`: id, path offset |
| `.kdbg_line` | `Line`: address, source id, line, column |
| `.kdbg_func` | `Function`: name, start, end, frame size, result type |
| `.kdbg_sym` | `Symbol`: name, address, size, type |
| `.kdbg_var` | `Variable`: function, name, frame offset, size, type |
| `.kdbg_type` | `Type` and `Member`: name, size, kind, member layout |
| `.kdbg_build` | `Build`: compiler, build ID, source count |

Strings are NUL-terminated offsets into the payload string table. The
Function record's name comes from the .debug_info F record's display
name, which is Class::method for a member function and the symbol for
everything else ([ADR 0030](decisions/0030-debug-display-names.md)); a display
name is not unique, so anything that resolves keeps using the symbol. Records are
emitted deterministically: sources, lines, functions, symbols, variables, types,
then build metadata.

A `Line` record is written for each instruction that carries a source location, so a
line has a record exactly while the image still has an instruction at it. A line
whose instructions a pass removed has no record: the format does not map it onto a
neighbouring line, because a breakpoint that cannot be placed is better than one
that stops in the wrong place. A debugger showing a stopped frame therefore reports
the next line that still has code.

`-O1` shrinks the set of placeable lines by the code it removes. The case worth
naming is E5 (`empty_block_elimination`, `src/ir/passes.cpp`): a statement whose
whole body is one jump — `break;`, `continue;` — lowers to a block holding a single
`br`, and eliminating that block deletes the only instruction that carried the line.

A `Variable` record exists for each **source variable**, which the IR names
`<variable>.addr`; a compiler temporary — `call.temp`, `operator.temp`,
`subscript.this` — is not recorded, so a pass may promote one without leaving a
record pointing at a dead slot (`docs/optimize.md` E4). A promoted source variable
would invalidate its record, which is why the pass refuses those.

Source paths are copied from the `.debug_info` bridge. `krld
--source-root=DIR` rewrites paths under `DIR` to `/`-separated paths relative to
`DIR` before writing KDBG; paths outside `DIR` are left unchanged. Passing the
project root keeps repository sources portable. Without the option the recorded
paths are unchanged.

## Tooling

- `krc --dump-symbols image.k32` prints canonical text for a `.k32` or `.kex`.
- `krc --dump-symbols --json image.kex` prints the stable JSON form.
- `krld` builds `.kdbg` from `.debug_info` after linking.
- `include/krc/debug/kdbg.h` is the shared reader/writer; `krc::debug` packages it.

## Freeze checklist

- [x] Exact record encoding and tag registry.
- [x] Type-table model and member layout.
- [x] Line/frame records at `-O0`.
- [x] Header and section CRC, bounds checks.
- [x] Text and JSON dump schema.
- [x] Shared reader/writer API.
- [ ] Location-expression opcodes (post-v1).
- [ ] Sidecar naming, strip behavior, and build-ID matching (post-v1).
- [ ] Full C++ scope/template records (post-v1).
