# 0048 — KR16 aggregate return destination

**Status:** accepted.
**Date:** 2026-11-17.
**Supersedes:** [ADR 0047](0047-kr16-aggregate-returns.md), withdrawn because it chose `R6`,
which is the status register on this ISA.
**Extends:** [ADR 0016](0016-kr16-scalar-abi.md), whose scalar subset this removes one limit
from. Reuses the stack-argument convention of [ADR 0028](0028-kr16-stack-arguments.md).

## Context

`src/backend/kr16_backend.cpp` rejects every `Ret` whose operand is a struct or union, so
`make_unique` compiles on KR32 and fails on KR16.

`docs/abi-kr16.md` settles the register question: `R0` returns, `R1`–`R3` are arguments, `R4`
is the frame pointer, `R5` the stack pointer, `R6` the status register and `R7` the program
counter. **No general-purpose register is free**, so the destination cannot travel in one.

## Decision

The destination address is passed as a **hidden argument on the stack**, after every
declared argument.

- The **caller** pushes the destination address first, before the other argument pushes.
  Pushes run highest-index argument first, so the first push lands at the highest address —
  which makes the destination the last argument, as ADR 0028 orders stack arguments.
- The **callee** reads it at `[R4 + 4 + 2 * parameter_count]` during its prologue and keeps
  it in a frame slot, because nothing survives a call in a register on this target.
- `Ret` copies the aggregate to that address in two-byte words.
- The caller's result slot holds the returned aggregate afterwards, exactly as it does on
  KR32, so nothing downstream distinguishes the two targets.

## Why not a hidden first pointer in R1

It shifts every declared argument by one register, so every caller and callee must agree on a
changed convention for aggregate-returning functions, and the three-argument window shrinks to
two for those calls. The stack form reuses a convention the codebase already implements and
documents, and costs one push and one pop per aggregate-returning call — a path that exists to
support `make_unique`, not a hot loop.

## Consequences

- `docs/abi-kr16.md` gains a returns row naming the hidden stack argument.
- `plan.md` gains the matching Appendix A entry, `D-0048`, with the implementation.
- The `KR16 aggregate returns are not implemented` rejection is removed.
- Aggregate-returning functions have one more stack argument than their parameter list says;
  a caller that pushes the wrong count corrupts the callee's frame, so the count is asserted
  by a test rather than left to review.
- Variadic functions that return an aggregate are not covered: the destination position among
  the pushed arguments is fixed, and a variadic callee reads its arguments by position. They
  keep the rejection.
