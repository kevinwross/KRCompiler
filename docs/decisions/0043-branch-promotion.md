# 0043 — Branch promotion in the KR16 assembler

- Status: accepted, implemented at `185aa0e`
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

Before this decision the assembler did not promote: a `BRA` whose target was 400
bytes away failed with `KR16 branch displacement does not fit in 8 signed bits`,
so the backend had to reach a far target with a two-word `JMP` and a near
unconditional branch cost two words where one would do. The sibling assembler
promotes instead, and its ADR 0003 describes the behaviour.

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

## Implementation

Both parts landed at `185aa0e`. `KR16Assembler` records each label's position in
the line sequence during parsing, runs the promotion fixpoint over `lines`
(`kr16_assembler.cpp`, "Promote an out-of-range BRA to JMP"), and rewrites the
label offsets from the final layout before encoding — the step without which a
promoted instruction encodes a displacement to where the label used to be. The
backend emits `BRA` for every unconditional branch and no longer carries a range
check.

`tests/fixtures/c/branch_promotion.s` is the regression: a `BRA` over 200 `NOP`s,
which the assembler must widen, and `krc_run_kr16_branch_promotion` executes the
result under the VM expecting exit 42.

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
