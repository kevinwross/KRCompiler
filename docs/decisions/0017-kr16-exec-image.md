# 0017 — Minimal KR16 EXEC image (M7)

**Status:** Accepted

## Context

The KR16 compiler and assembler can now produce text, but `krc -c` had no image
path. The KR16 design specifies a shared container for relocatable objects and
executables; the full linker, symbols, relocations, and debug sections are not
implemented yet.

## Decision

- `krc -c --target=kr16` emits a KR16 `.kex` EXEC container for one
  translation unit.
- The header is the frozen 64-byte KR16 header: magic `KR16`, major version 1,
  kind EXEC, entry `0x0100`, two sections, and a CRC32 over the complete file
  with the checksum field zeroed.
- Section 0 is `.text`, allocated and executable, loaded at `0x0100`.
- Section 1 is `.debug_str`, containing `.text\0.debug_str\0`; it is the shared
  string table referenced by the section headers.
- A generated `_start` calls `main` and executes `HLT`, so the VM exits with
  `main`'s result in `R0`.
- Symbols, relocations, debug line/type records, and multiple translation units
  are omitted until the KR16 linker lands. A `.kex` produced here is runnable
  but not debuggable.
- The reader and writer are bounds-checked; malformed images must diagnose
  rather than crash.

## Consequences

- `sum.c` compiles, assembles, links as one image, and runs to exit 55 without
  a separate linker.
- The image is deterministic and self-contained for the supported subset.
- The full KR16 relocatable object and linker path must extend this container
  without changing the frozen header fields.
