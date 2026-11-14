# 0034 — Integer literals and immediates on a sixteen-bit target

- Status: accepted
- Date: 2026-11-14
- Decision: how a literal is typed when the target's types are narrow, and what happens to a value no type can hold
- Scope: front end and the KR16 assembler; the ABI consequence is named as open work, not decided here

## Context

The KR16 profile has sixteen-bit `int` and `long`. Two silent narrowings were
shipping wrong code:

- `MOV R0, #5000000001` assembled as `MOV R0, #0x2001`, because the assembler
  cast the immediate to sixteen bits with no range check.
- `5000000001L` had the type `long` on a profile where `long` is sixteen bits,
  and nothing compared the value with the type's range.

`long a = 5000000001L; if ((a >> 16) != 76293) return 1;` exited 1 on the KR16
VM, and every existing KR16 long fixture passed because it compared a truncated
value against a truncated expectation.

The KR16 runtime had the same problem from the other side: `rand()` used a
thirty-two-bit multiplier, which only worked because the assembler truncated it.

## Decision

1. An integer literal is typed by C11 6.4.4.1. Without a suffix it takes the
   first of `int`, `long`, `long long` that holds it, and a hexadecimal or octal
   literal may also take the unsigned type of the same width. A profile type that
   does not exist (`long long` on KR16) is skipped rather than chosen.
2. A literal with a suffix must fit the type the suffix names. If it does not,
   the front end reports `integer constant is too large for its type` and the
   translation unit fails.
3. A literal that fits no type is diagnosed the same way.
4. The KR16 assembler rejects an immediate outside `-32768..65535` for the
   instructions that take a sixteen-bit field, naming the value and the
   mnemonic. Silent truncation is never an option.
5. The KR16 runtime uses constants that fit the profile: `rand()` is a
   sixteen-bit linear congruential generator (`* 25173 + 13849`) rather than the
   thirty-two-bit one.

## Consequences

- The diagnostic is per translation unit and per profile, so a program that is
  valid on KR32 can be rejected on KR16, which is the intent: the profile cannot
  represent the value.
- **KR16 has no 64-bit type, and that is by design.** Its profile declares
  `short`, `int`, and `long` as sixteen bits and `long_long_bits` as zero, so
  `long long` is rejected where it is written (`'long long' is not supported by
  the kr16 profile`) and `long` is one word. One register per parameter is
  therefore one word per parameter, which is what
  [docs/abi-kr16.md](../abi-kr16.md) describes, so no ABI change is needed and
  none is implied here. An earlier reading of these defects as a missing 64-bit
  ABI was wrong: they were the literal typing and the immediate range, and both
  are fixed.
- `krc_cxx_kr16_integer_too_large` pins the diagnostic. A KR16 run test cannot
  cover a wider value because the profile has no type for one.
