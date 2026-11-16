# 0043 — Branch promotion in the KR16 assembler

- Status: proposed
- Date: 2026-11-15
- Decision: the assembler promotes an out-of-range `BRA` to `JMP`, and the
  backend may then select `BRA` wherever a short branch fits
- Scope: the KR16 assembler and backend, not the ISA

## Context

KR16 has two branch widths. The **B-type** is one word — `0111 cond disp8`, a
signed *word* displacement, `target = PC + 2 + disp8 * 2`, reach -256..+254
bytes — and `BRA` is its always condition. `JMP` and `CALL` are the two-word
C-type with a 16-bit signed displacement. This compiler's encoder has both
(`encode_b`, `encode_c_relative`) and its assembler accepts `BRA`.

What it does **not** do is promote. A `BRA` whose target is 400 bytes away fails
with `KR16 branch displacement does not fit in 8 signed bits`, so the backend
must emit `JMP` everywhere and a near unconditional branch costs two words
where one would do. The sibling assembler promotes instead, and its ADR 0003
describes the behaviour.

## Decision

- After parsing, the assembler runs a fixpoint: for each `BRA` whose target is
  out of the 8-bit word range, rewrite the line's mnemonic to `JMP` and repeat,
  because every promotion moves the labels after it.
- Promotion is by **rewriting the mnemonic**, not by threading a flag: the
  existing `instruction_size` then reports two words and the existing encoder
  emits the C-type form, so no new code path is introduced.
- **Conditional** branches are not promoted by this decision. kr16 promotes an
  out-of-range `Bcc` to the inverse condition plus `JMP` over a synthetic label;
  that needs label synthesis this assembler does not have, so an out-of-range
  `Bcc` stays an error and is recorded as a limitation.
- Once promotion exists, the backend may emit `BRA` for an unconditional branch
  wherever it already emits `JMP`, and let the assembler widen what does not fit.

## What blocks it today

Labels are recorded **during the parse pass** and are not stored in `lines`, so
after a promotion there is nothing to recompute them from. The first step is
therefore to record each label's position in the line sequence — a small change
— and only then to run the fixpoint. Attempting the fixpoint first would produce
exactly the failure mode this project has already paid for three times: a change
that cannot see the state it needs to maintain.

## Consequences

- Near unconditional branches save one word each, and no `JMP` ever fails for
  range.
- Output changes for any function where a `BRA` is promoted, so the determinism
  check must be re-run, and the emitted size becomes dependent on the assembler's
  choice rather than the backend's. That is the point of relaxation, and it is
  also why the fixpoint must be deterministic: it is, because it walks lines in
  source order.
- An out-of-range conditional branch remains an error. That is a real gap against
  the sibling assembler and should be a separate decision, not folded into this
  one.
