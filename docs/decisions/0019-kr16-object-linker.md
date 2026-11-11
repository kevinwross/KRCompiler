# 0019 — KR16 relocatable objects and linker path (M7)

**Status:** Accepted

**Date:** 2026-11-11

**Decision:** D-041

## Context

ADR 0017 established a minimal single-unit KR16 `.kex` image and deferred
relocatable objects, relocations, and the linker. `krc -c --target=kr16
--emit-object` now writes KRO objects, and `krld` links them into a runnable
KR16 image with the startup object.

The frozen KRO relocation registry has no KR16 PC-relative 16-bit type, and the
KR16 CALL displacement is expressed in words rather than bytes.

## Decision

- The default `krc -c --target=kr16` path remains the single-unit `.kex` from
  ADR 0017.
- `krc -c --target=kr16 --emit-object` accepts `.c` and `.s` inputs and writes a
  KRO object with one `.text` section and function symbols.
- Every symbol-bearing `MOV Rd, #label` and `LEA Rd, [label]` emits an `Abs16`
  relocation at the immediate word. This includes internal labels, because
  linking shifts object text.
- Unresolved labels listed by `.extern` become undefined KRO symbols plus
  `Abs16` relocations.
- KR16 calls lower to `MOV R0, #callee` followed by `CALL R0`. This avoids a
  PC-relative relocation type that KRO v1 does not define for KR16.
- `krld` detects target ID 2, defaults the base to `0x0100`, links objects in
  input order, and writes a KR16 EXEC image. The image's text is loaded at the
  link base while the entry field points at `_start`, which may lie after the
  load base.
- The startup object supplies `_start` and the console `puts` shim.
- Data emitted by the KR16 assembler remains folded into `.text`; archives,
  debug records, and KEX symbols are not yet emitted.

## Consequences

- `hello.c` links against the startup object, prints `Hello, KR16!`, and exits 0.
- `sum.c` links and exits 55.
- Multiple translation units link and run: a separate `add` object and `main`
  object return 42.
- KRO object files and relocations are exercised by unit and CTest coverage.
- A future KR16 object format extension can add PC-relative relocations and
  split `.data`/`.rodata` without changing the default `.kex` path.

## Alternatives considered

- **Change `-c` to always emit KRO objects.** Rejected: it would break the
  D-041 single-unit path and its existing tests before the linker is complete.
- **Add a KR16-specific PC-relative relocation.** Rejected for this step: the
  KRO relocation registry is frozen, and the register-call sequence needs no new
  format entry.
- **Place startup after every object and patch the entry.** Rejected: the KEX
  writer previously assumed the text load address equalled the entry, which is
  false for a startup object placed after user text.
