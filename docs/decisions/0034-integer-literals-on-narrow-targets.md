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
- **A sixty-four-bit value still cannot be carried.** A `long` is sixteen bits on
  KR16, so the literal rule hides the larger defect rather than fixing it: the
  KR16 ABI as written in [docs/abi-kr16.md](../abi-kr16.md) describes arguments
  as words, while the backend assigns one register per *parameter*, and a
  wide immediate has no materialisation at all. Carrying a sixty-four-bit value
  needs word-based argument assignment and multi-word constant materialisation,
  which changes the ABI, so it needs its own ADR and a failing fixture before it.
- `krc_cxx_kr16_integer_too_large` pins the diagnostic. The KR16 run tests do not
  cover a value wider than sixteen bits, and cannot until the ABI work lands.
