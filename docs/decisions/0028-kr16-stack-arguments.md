# 0028 — KR16 stack arguments

- Status: accepted
- Date: 2026-11-13
- Decision: freezes how KR16 passes more than three scalar arguments
- Extends: [ADR 0016](0016-kr16-scalar-abi.md), which froze the register subset

## Context

ADR 0016 froze the KR16 scalar convention: the first three integer arguments
travel in `R1`–`R3`, the result returns in `R0`, and `R4` is the frame pointer.
Everything beyond three arguments was refused by the backend — "KR16 supports at
most three call arguments" — because no rule existed for where the rest go. That
rejection blocks every function with four or more parameters and is the first of
two blockers for `printf` on KR16 (the other is variadic calls).

The frame is already fixed: `PUSH R4; MOV R4, SP; LEA SP, [R4 - frame_size]`,
slots relative to `SP`, epilogue `MOV SP, R4; POP R4; RET`, two-byte slots, stack
top `0xF000`, full-descending.

## Decision

- The first three scalar arguments stay in `R1`–`R3`.
- Further arguments are pushed on the stack by the caller, **last argument
  first**, so the fourth argument ends at the lowest address and the arguments
  run upward in declaration order. Each occupies one two-byte slot.
- The caller releases the pushed area immediately after `CALL` with
  `LEA SP, [SP + 2 * count]`, so the callee inherits a clean stack and there is
  no callee-side cleanup.
- The callee reaches the fourth argument at `[R4 + 4]`, and argument `n >= 4` at
  `[R4 + 4 + 2 * (n - 4)]`. Prologue: `PUSH R4` puts the saved frame at `[SP-2]`,
  `MOV R4, SP` names that word, `CALL` left the return address at `[R4 + 2]`, and
  the caller's pushed area starts at `[R4 + 4]`.
- The prologue copies each stack argument into its frame slot, exactly as it
  copies the register arguments, so the rest of the function sees one uniform
  slot per parameter.
- Argument evaluation order is unchanged: the caller stages every argument in a
  frame slot first, then loads `R1`–`R3`, then pushes the extras, then loads the
  callee address. Pushing after the register loads is what keeps the staged
  slots addressable by `SP + offset` for the whole sequence.

## Consequences

- Functions with four or more scalar parameters compile and run on KR16; a
  five-parameter fixture returns the value its arithmetic produces.
- The stack area is transient: nothing in the frame layout changes, and a caller
  that recurses pays only for its own pushed extras.
- `printf` still cannot exist on KR16: variadic functions remain rejected. With
  stack arguments in place, the remaining work is a varargs rule — where the
  first variable argument lives, how `va_start`/`va_arg` find it, and how the
  promoted width is defined — which is a separate decision.
- A callee that declares fewer parameters than the caller passed is still a
  caller error; the compiler already rejects a mismatched call.

## Alternatives considered

- Push extras first, then load `R1`–`R3`: rejected because the staged argument
  slots are addressed relative to `SP`, which pushing moves.
- A caller-reserved outgoing argument area at the low end of the frame (the KR32
  shape): rejected for now because the KR16 frame is slot-addressed from `SP`
  and reserving an area would have to be part of `frame_size` for every
  function. Pushing is smaller and needs no layout change.
- Callee-cleans: rejected — it would have to restore `SP` before every `RET`,
  and the callee does not know how many extras the caller pushed.
