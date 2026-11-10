# ADR 0008: Emit a textual debug-info bridge for G4

- Status: accepted
- Date: 2026-11-09
- Decision: interim `.debug_info` encoding for the KR32 debugger
- Supersedes: none

## Context

G4 requires a KR32 debugger session on `binarytree.c`. The target KDBG binary
format described in [docs/symbols.md](../../docs/symbols.md) is not implemented
yet, and the existing KR32 debugger already consumes a NUL-separated
`.debug_info` record stream. Withholding source lines, functions, variables, and
globals until KDBG lands would leave the debugger unable to show the program
under test.

## Decision

The compiler emits a `.debug_info` section using the existing KR32 debugger's
text record stream:

- `S <source> <path>` — source file.
- `L <label> <source> <line> <column>` — line start at a local `.Ldbg.*` label.
- `F <name> <start> <end> <frame> <type>` — function range and frame size.
- `V <function> <name> <offset> <type> <size>` — parameter or local at `R7 + offset`.
- `G <name> <label> <type> <size>` — global at a symbol.
- `T <name> <size>` and `M <type> <member> <offset> <type> <size>` — type and
  aggregate-member layout.

Records are NUL-separated. The backend emits a debug label before each
instruction that carries an IR debug location. `krld` merges `.debug_info`
sections, deduplicates source paths, renumbers source IDs, and preserves the
records in the `.k32` image. Debug sections are not guest-addressable.

This section is a **bridge**, not KDBG. KDBG's binary sections, location
expressions, and shared reader remain the v1 contract. The bridge is removed
when `libkrcdebug` and the KDBG writer land.

## Consequences

The KR32 GUI and DAP debugger can set source breakpoints, show lines, functions,
locals, globals, call stacks, memory, and disassembly for compiler output now.
The linker must understand one more section type, and the image carries a
debugger-specific record stream in addition to the eventual KDBG payload.

The bridge does not satisfy goal G-5 (one shared debug format); it is a
migration aid until KDBG is frozen. Tooling that reads `.debug_info` directly is
temporary.

## Alternatives considered

- **Implement KDBG before G4.** Rejected for this step: it would delay all
  debugger evidence until the complete type/location/reader model exists.
- **Emit no debug info until KDBG.** Rejected because G4 explicitly requires a
  working KR32 debugger session.
- **Teach the debugger a new dialect first.** Rejected because the existing
  debugger already provides the session surface G4 needs.
