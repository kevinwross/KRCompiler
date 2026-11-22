# 0056 — 64-bit argument and result passing on kr216

- Status: accepted (2026-11-22)
- Date: 2026-11-22
- Decision: a 64-bit integer argument consumes four consecutive integer argument slots; a 64-bit result is returned in R2–R5
- Supersedes: none

## Context

`docs/abi-kr216.md` fixes 16-bit and 32-bit argument slots and results but leaves
`long long` and `intmax_t` without a boundary convention. The backend now carries
64-bit values in locals and helpers, and the compiler cannot pass or return one
across a function call without naming the registers and stack bytes.

The data model already fixes `long long` at eight bytes and two-byte alignment
(`DataModel::Ip16Long32`). KR16's stack rules (ADR 0028) already order pushed
arguments so argument 4 sits at `FP + 4`; kr216 reuses that layout.

## Decision

1. Integer and pointer arguments occupy one two-byte slot each; 32-bit values
   occupy two slots and 64-bit values occupy four slots.
2. A 64-bit argument takes the next four integer slots in R2–R5 when all four are
   free. If fewer than four slots remain, the whole argument is pushed as an
   eight-byte little-endian area. The area is ordered so its low word is at the
   first free argument position and its high words follow at higher addresses.
3. A 64-bit result is returned in R2–R5, low word in R2 and high word in R5. A
   32-bit result remains in R2:R3 and a 16-bit result remains in R2.
4. The callee treats R2–R5 as caller-saved whether they carried one short
   argument or part of a 64-bit argument.

## Consequences

The backend copies all four words in the prologue, the call argument setup, the
result store and the return sequence. A 64-bit argument in a variadic call is
pushed as four words, so `va_arg` reads it with the same eight-byte layout.

The convention needs no new opcode and no object-format change; KRO symbols and
relocations are untouched.

## Alternatives considered

- **Return a 64-bit value through a hidden destination pointer.** Rejected: it
  consumes R6, which the aggregate-return contract already uses, and it makes a
  scalar return a memory operation.
- **Return 64-bit values in two independent register pairs.** Rejected: R2:R3 is
  already the 32-bit result, so a second pair would make the split depend on the
  argument count.
- **Leave 64-bit values out of calls until a later milestone.** Rejected: the
  widened backend and the ABI test surface both use `long long` across functions,
  and an undocumented gap becomes a silent high-word truncation.
