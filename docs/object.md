# KRO object and executable container

**Status:** Frozen v1.0 (2026-11-09, [ADR 0005](decisions/0005-kro-object-format-freeze.md)).
**Magic:** `KRO\0`. **Kind:** relocatable object, executable, or archive member.
All integer fields are little-endian.

## Header (120 bytes)

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | Magic | `KRO\0` |
| 4 | 1 | Version major | 1 |
| 5 | 1 | Version minor | 0 |
| 6 | 2 | Kind | 0 object, 1 executable, 2 archive member |
| 8 | 2 | Header size | 120 |
| 10 | 2 | Flags | bit 0 symbols, 1 debug, 2 stripped, 3 relocatable |
| 12 | 4 | Target ID | target descriptor ID |
| 16 | 4 | ABI version | 1 |
| 20 | 1 | Pointer width | 16, 32, or 64 |
| 21 | 1 | Endianness | 0 little, 1 big |
| 22 | 2 | Reserved | zero |
| 24 | 4 | Section count | |
| 28 | 4 | Symbol count | |
| 32 | 4 | Relocation count | |
| 36 | 8 | String table offset | absolute file offset |
| 44 | 8 | String table size | bytes |
| 52 | 8 | Debug table offset | zero when absent |
| 60 | 8 | Debug table size | zero when absent |
| 68 | 8 | Entry address | zero for objects |
| 76 | 8 | Image base | zero for objects |
| 84 | 8 | File size | bytes |
| 92 | 16 | Build ID | deterministic 128-bit value |
| 108 | 4 | CRC32 | file with this field zeroed |
| 112 | 8 | Reserved | zero |

## Table order

`header`, section records, symbol records, relocation records, string table,
then section payloads in section-index order. Tables begin immediately after
the previous table; payloads are aligned to 8 bytes. A section with no payload
(`bss`) has offset zero.

## Section record (64 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Name string offset |
| 4 | 2 | Type |
| 6 | 2 | Flags |
| 8 | 8 | Address |
| 16 | 8 | Payload offset |
| 24 | 8 | Size |
| 32 | 4 | Alignment |
| 36 | 4 | Link |
| 40 | 4 | Info |
| 44 | 4 | Entry size |
| 48 | 16 | Reserved |

Types: `none=0`, `text=1`, `rodata=2`, `data=3`, `bss=4`, `init_array=5`,
`fini_array=6`, `tls=7`, `debug=8`, `note=9`, `other=255`.
Flags: alloc `0x1`, write `0x2`, execute `0x4`, TLS `0x8`, merge `0x10`,
strings `0x20`, discardable `0x40`.

## Symbol record (40 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Name string offset |
| 4 | 1 | Binding |
| 5 | 1 | Type |
| 6 | 1 | Visibility |
| 7 | 1 | Flags |
| 8 | 4 | Section index |
| 12 | 4 | Reserved |
| 16 | 8 | Value |
| 24 | 8 | Size |
| 32 | 8 | Reserved |

Binding: local `0`, global `1`, weak `2`.
Type: notype `0`, object `1`, function `2`, section `3`, file `4`, TLS `5`.
Visibility: default `0`, hidden `1`, protected `2`.

## Relocation record (32 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Section index |
| 4 | 2 | Type |
| 6 | 1 | Width in bytes |
| 7 | 1 | Flags |
| 8 | 8 | Offset |
| 16 | 4 | Symbol index |
| 20 | 4 | Reserved |
| 24 | 8 | Addend |

Relocation types: absolute 8/16/32/64 = `0..3`, PC-relative 8/16/32/64 =
`4..7`, KR32 absolute-32 = `8`. Unsupported type/width pairs are errors.
Overflow is an error, never a truncation.

## String table

A leading NUL byte, then NUL-terminated names in first-use order. Name offsets
are byte offsets into this table.

## Build ID and CRC

The build ID is two FNV-1a 64-bit hashes written little-endian: the first over
the concatenated section names/types/flags/sizes and symbol names/bindings/
values, the second over relocations and section payload bytes. The CRC32 uses
the standard IEEE polynomial over the complete file with offset 108 zeroed.

## Validation and determinism

- Validate magic, version, header size, target ID, ABI version, pointer width,
  endianness, table bounds, string offsets, and counts.
- Symbols are written deterministically: globals by name, locals by
  section/offset, strings in first-use order.
- No timestamps or absolute host paths in the object.

## Executable images

The linker builds a common linked model, then a per-target image writer emits
the VM's loadable image (KR32, KR16). The writer receives sections, entry point,
symbols/debug, and layout, and rejects unsupported placements or address ranges.
Debug sections are not part of the load image: the linker gives them address
zero and excludes them from the image end, so `__heap_start` names the byte after
the loaded image rather than after debug data. That matters on KR16, whose whole
address space is 64 KiB and whose merged `.debug_info` exceeds it.
A `.bss` section is NOBITS: the section table carries its size, it has no file
payload, and the loader zero-fills the range. `__heap_start` is aligned to 8 on
KR32 and to the 2-byte word on KR16, matching `kr16`'s linker.

A KR16 object can carry named executable sections (`.text.<name>`, from the
assembler's `.section` directive) alongside the base `.text`. The linker keeps
each name as its own output section, lays text sections out together in name
order after `.text`, and with `--gc-sections` drops a section that no reachable
section relocates to. Survivors keep their assigned addresses; an image built
without GC and without named sections is byte-identical to one from before the
directive existed.

A `.kex` carries the `.kdbg` payload beside its text and string table as an
unallocated debug section; the payload records the image base at KDBG header
offset 56 (docs/symbols.md).

## kr216 executable images

The kr216 loadable image is the `K216` container specified by
[ADR 0054](decisions/0054-kr216-image-container.md): a 64-byte little-endian header carrying
the magic, container version, kind, flags, entry, **target ID 4**, **ABI version 1**, image
base, section-table and string-table offsets, file size and an optional CRC32; then 32-byte
section records and a string table, in the same shape as `.kex`. Sections are `progbits` or
`nobits`, flagged `alloc`/`write`/`exec`/`debug`, and carry absolute 16-bit addresses in the
64 KiB space. `.text` is executable, `.rodata` and `.data` are allocated, `.bss` is nobits,
and the KDBG payload travels in an unallocated `.kdbg` section selected by flag bit 0 of the
header.

The loader validates the header fields, the table and payload bounds, the target and ABI
version, and rejects overlapping allocated sections before copying anything. It writes the
entry into the word at `0x0100` — the ISA's reset vector — so the container and the reset
rule agree, and it does not load the debug section into memory.