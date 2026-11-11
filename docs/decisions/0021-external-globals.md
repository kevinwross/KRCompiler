# 0021 — External global declarations

**Status:** Accepted

**Date:** 2026-11-11

## Context

C `extern` object declarations were lowered as definitions. Two translation units
that shared an `extern int` produced duplicate symbols at link time, and a
declaration followed by a definition in one translation unit could not be
distinguished from a pure declaration.

The IR had no way to represent a global that is resolved by the linker rather
than emitted by the current module.

## Decision

- `GlobalVariable` gains `external`. An external global has a name, type,
  alignment, and optional constant flag, but no initializer or emitted storage.
- The C front end marks a global external when its declaration has `extern` and
  no initializer. A later non-extern definition in the same translation unit
  clears the flag on the existing global.
- The KR32 backend emits `.extern <name>` for an external global instead of a
  `.data` label and storage. Debug records skip external globals because they
  have no address in this object.
- The IR printer writes `external global` for the declaration form.
- References keep using the ordinary `LEA Rd, [name]` form; the assembler records
  the resulting relocation against the external name.

## Consequences

- `extern int shared;` in one object and `int shared = 7;` in another link and run.
- A declaration followed by a definition in one translation unit links and runs.
- Multiple declarations without a definition fail at link time with an undefined
  symbol rather than being silently zero-filled.
- The KRO object format is unchanged; the undefined symbol is already part of the
  v1 symbol model.
- No D-xxx decision changes.

## Alternatives considered

- **Keep emitting a definition for extern declarations.** Rejected: it creates
  duplicate symbols and hides missing definitions.
- **Use a weak zero-initialized definition.** Rejected: a missing definition must
  be a link error, and weak symbols have different merge semantics.
- **Add a new object section for declarations.** Rejected: undefined KRO symbols
  already express the contract.
