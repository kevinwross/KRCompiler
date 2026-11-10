# ADR 0001: Freeze the KR32 C ABI

- Status: accepted
- Date: 2026-11-09
- Decision: freezes the C data model and calling convention in `plan.md` §10 and `docs/abi.md` for KR32
- Supersedes: none

## Context

M2 lowers C to KR32 machine code, so the C data model, layout rules, calling convention, and varargs
representation must stop moving before instruction selection and frame layout depend on them. The approved
plan already specifies the KR32 ILP32 model and inherits the KR32 ABI revision 1 calling convention. This ADR
records that these existing decisions are now frozen for C.

## Decision

The following are frozen for KR32 C:

- ILP32: `char` 1/1 signed, `short` 2/2, `int` 4/4, `long` 4/4, `long long` 8/8, pointer 4/4,
  `float` 4/4, `double` 8/8, `long double` 8/8 as an alias for `double`, `size_t`/`ptrdiff_t` 4/4,
  `intmax_t` 8/8, `wchar_t` 4/4.
- Struct layout: members in declaration order, each member aligned to its type, trailing padding to the
  record alignment. Union size is the maximum member size rounded to the union alignment.
- Bit-fields: the allocation unit is the declared type's storage unit, bit order is little-endian, and a
  bit-field does not straddle its allocation unit. A zero-width bit-field starts a new allocation unit.
- Calling convention: integer arguments R0–R3, FP arguments F0–F7, further arguments on the stack;
  integer return in R0 (R0/R1 for 64-bit), FP return in F0/F1; R7 frame pointer; R11 thread pointer;
  16-byte public call alignment; aggregates larger than 8 bytes use `sret`.
- Varargs: stack-passed in 8-byte slots; `float` promotes to `double`; `va_list` addresses the first
  variadic slot.
- C has no mangling; external C names are the source identifiers.

The KR16 C ABI, C++ ABI, EH/RTTI, dynamic TLS, and target-descriptor code-generation fields are not frozen by
this ADR. They keep their own milestones: KR16 before M7, C++/EH before M5/M10.

## Consequences

M2 can implement instruction selection, frames, and aggregate passing against a fixed contract. Changing any
item above now requires a new ADR, an ABI version bump, and a runtime rebuild. The Sema data-model tests and
`sysroot/<target>/include/bits/` generator become the conformance check for these values.

## Alternatives considered

- Defer the freeze until M3 or M5: rejected because M2 codegen would encode provisional layout and calling
  decisions twice.
- Freeze KR16 and C++ at the same time: rejected because the KR16 calling convention and C++ ABI are not yet
  specified; freezing them now would turn open questions into accidental contracts.
