# 0047 — KR16 aggregate returns

**Status:** accepted (decision only; implementation pending).
**Date:** 2026-11-17.
**Extends:** [ADR 0016](0016-kr16-scalar-abi.md), whose scalar subset this removes one limit from.

## Context

`src/backend/kr16_backend.cpp` rejects every `Ret` whose operand is a struct or union with
`KR16 aggregate returns are not implemented`. The rejection is deliberate — storing a
truncated value would be worse — but it is the only thing that stops `make_unique` from
working on KR16 while it works on KR32:

```
$ krc -c --target=kr32 --emit-object p_unique.cpp   # ok
$ krc -c --target=kr16 --emit-object p_unique.cpp
error: KR16 backend: KR16 aggregate returns are not implemented
```

`make_tuple` is blocked for a different reason and is **not** part of this decision:
`sysroot/include/tuple` documents "no by-value factory" as a limit of the header itself, so
`make_tuple` is undeclared on KR32 as well.

## Decision

An aggregate return on KR16 passes its destination address in **`R6`**.

The caller allocates the destination, loads its address into `R6`, and calls. The callee
stores the aggregate there instead of in `R0`, and `RET` carries nothing. `R6` is
caller-saved scratch per [abi-kr16.md](../abi-kr16.md) line 35, so it needs no callee save
and conflicts with no other role.

## Why not R4, mirroring KR32

KR32 passes the memory-class return destination in `R4` (`docs/abi.md` line 50). The same
register number means something different on KR16: **`R4` is the frame pointer**, and `R5`
is the stack pointer (ADR 0016). Reusing `R4` would overwrite the frame the epilogue
restores `SP` from. A register number is not an ABI; the role is.

## Why not a hidden first pointer

The conventional C form passes the destination as a hidden argument 0. KR16 has exactly
three argument registers, `R1`–`R3`, and ADR 0028 already spills beyond them to the stack.
Consuming one of three for the destination would change the calling convention for every
aggregate-returning function and shrink the register-argument window for all its callers.

## Consequences

- The scalar subset in ADR 0016 gains one case; `docs/abi-kr16.md` gains a returns row.
- `kr16_backend.cpp` replaces the rejection with a store through `R6`, and the call site in
  IR generation allocates the destination and sets `R6` for KR16 as it already does for
  KR32's `R4`.
- Aggregate returns become testable on KR16 the way they already are on KR32, and
  `make_unique` works on both targets.
- Register pressure: `R6` is claimed across a call, so IR generation must not hold a live
  value in it. That is the cost of this choice and the thing to watch when implementing.

## Appendix A

This extends a frozen decision; the matching Appendix A entry is `D-0047`, to be added with
the implementation rather than with this record.
