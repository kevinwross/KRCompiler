# 0022 — Internal global linkage

**Status:** Accepted

**Date:** 2026-11-11

## Context

C `static` file-scope objects were emitted with the same `.global` directive as
external-linkage objects. Two translation units that each defined `static int
counter` produced duplicate symbols when linked, even though each object should
have private storage.

`GlobalVariable` carried `external` for declarations but no internal-linkage
flag.

## Decision

- `GlobalVariable` gains `internal`. The C front end sets it for `static`
  file-scope and function-scope objects.
- The KR32 and KR16 backends emit a label without `.global` for an internal
  global, so the object symbol remains local.
- External declarations keep `external`; a static declaration is never external.
- The IR printer writes `internal global` for the local form.
- Debug records still include internal globals because they have storage in the
  current object.

## Consequences

- Two objects may each define `static int counter = N` and link cleanly.
- A static object is not visible to another object at link time.
- The object format is unchanged; local KRO symbols already express the
  contract.
- No D-xxx decision changes.

## Alternatives considered

- **Rename static globals with a per-object prefix.** Rejected: it changes source
  names in debug output and does not express the linkage directly.
- **Mark static globals weak.** Rejected: weak symbols merge, which is the wrong
  semantics for private storage.
- **Keep `.global` and rely on the linker.** Rejected: it rejects valid C.
