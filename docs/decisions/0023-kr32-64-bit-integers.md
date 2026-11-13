# 0023 — 64-bit integers on KR32

**Status:** Accepted

## Context

The KR32 word is 32 bits, but the ILP32 data model gives `long long` 8 bytes and
`intmax_t` a 64-bit value, and plan.md §10.2 already assigns the 64-bit integer
return to `R0`/`R1`. The backend allocated an 8-byte frame slot for such a value
and then moved only its low word: `long long` arithmetic truncated silently, a
64-bit argument reached the callee as one word, and `long long` division
returned its dividend's low word. `../kr32/examples/cxx_tls.cpp` exposed this as
a `long long` division returning 0 where 42 was expected.

The ISA divides only 32-bit values (plan.md §10.1 records "64-bit division is a
runtime libcall"), so a division cannot be lowered inline.

## Decision

- A 64-bit integer value occupies two consecutive 32-bit frame words, low word
  first, and is held in the register pair `R0` (low) / `R1` (high).
- Add, subtract, multiply, the bitwise operators, and the shifts are lowered
  inline: `SLTU` produces the carry and borrow, `MULHU` supplies the high word
  of the low-word product, and a shift count of 1–31 funnels one word into the
  other. A count of zero leaves the value unchanged; 32–63 keeps one word and
  zero- or sign-fills the other. The ISA masks shift counts to five bits, so
  those two ranges need their own paths.
- A 64-bit comparison tests the high words first and defers to the low words
  only when the high words are equal, which preserves the predicate's
  signedness on both halves.
- A 64-bit argument consumes two consecutive integer argument words. A pair
  that does not fit in the remaining argument registers continues on the stack,
  one word per outgoing word, following the existing outgoing-area convention.
  The callee prologue stores both words into the parameter's frame slot.
- A 64-bit return travels in `R0`/`R1`, as plan.md §10.2 states.
- Division and remainder are libcalls to `__krc_udivdi3`, `__krc_sdivdi3`,
  `__krc_umoddi3`, and `__krc_smoddi3` in `targets/kr32/runtime/libc.c`. Each
  takes the two operands as eight-byte aggregate pairs in `R0`–`R3` and returns
  the eight-byte result in `R0`/`R1`, so the helper follows the same convention
  as a compiled function. A zero divisor yields zero rather than trapping
  inside a runtime routine. The helpers publish their two results through
  file-scope variables and are therefore not reentrant; the compiler never
  calls one helper from inside another.
- `MULH` and `MULHU` are added to the compiler's KR32 assembler and encoder.
  Both are KR32 v1 instructions (`../../../kr32/docs/isa-v1.md` §5.3, opcodes 0x23
  and 0x24); the compiler previously encoded neither.
- The IR, object format, symbol format, and target descriptor are unchanged.
  This is a KR32 calling-convention and lowering decision within ADR 0001.

## Consequences

- `long long` values compare, divide, and cross call boundaries correctly, and
  `printf("%lld")` receives both words.
- The `R0/R1` return and the two-word argument rule are ABI commitments: a
  hand-written assembly routine that passes or returns a 64-bit integer must
  follow them.
- Converting a 64-bit integer to or from `float`/`double` still uses the
  ISA's 32-bit conversion, so values outside the 32-bit range convert wrongly.
  That path needs its own helper and remains open.
- KR16 is unaffected: its IP16 profile has no 64-bit integer type
  (`long_long_bits` is zero), so no helper is built for it.
- The helper costs a call and up to 64 shift-subtract iterations per 64-bit
  division; a constant-divisor fast path is future work.
