# 0015 — Small aggregate returns on KR32

**Status:** Accepted

## Context

ADR 0014 passes small aggregates by value but left aggregate returns
unsupported. C++ methods and factory functions commonly return a small record,
so a caller could not initialize a local from a function call.

## Decision

- A by-value aggregate return of up to 8 bytes is split into 4-byte words and
  returned in `R0` and `R1`.
- The callee's `ret` loads those words from the returned value's frame slot.
- The caller stores `R0`/`R1` into the call result's frame slot before any use.
- A local record initialized directly from a function call copies the returned
  aggregate into the local.
- Aggregate returns larger than 8 bytes remain unsupported; the `sret` path is
  still the intended mechanism for them.
- The IR signature and object format are unchanged; this is a KR32 calling
  convention clarification within ADR 0001.

## Consequences

- Factory functions and small record returns work end to end.
- The backend keeps its frame-slot model: return values and results occupy
  ordinary frame slots, with registers only at the call boundary.
- Large returns still need the future `sret` work.
