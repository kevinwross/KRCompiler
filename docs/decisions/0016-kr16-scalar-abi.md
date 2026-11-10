# 0016 — KR16 scalar calling convention (M7 subset)

**Status:** Accepted

## Context

M7 introduces the first KR16 code path. The ISA defines `R1`–`R3` as argument
registers and `R0` as the return value, but the compiler also needs a frame
layout and a stable rule for how callers and callees use the stack. The full
KR16 ABI appendix still needs aggregates, varargs, the runtime, and the image
format.

## Decision

- The first three integer or pointer arguments are passed in `R1`, `R2`, and
  `R3`; additional arguments are not implemented yet and are rejected.
- Integer and pointer results are returned in `R0`.
- `R4` is the frame pointer and `R5` is the stack pointer. The callee saves the
  incoming `R4` with `PUSH R4`, sets `R4 = SP`, then allocates its frame with
  `LEA SP, [R4 - frame_size]`.
- Frame slots are addressed as `[SP + offset]`, with every slot two bytes.
  Parameters are stored to their slots in the prologue.
- The epilogue restores `SP = R4`, pops `R4`, and executes `RET`.
- All registers are caller-saved except `R4`. The compiler emits no
  callee-saved register allocation yet.
- Aggregate parameters/returns, floating point, varargs, globals, indirect
  calls, and more than three arguments are rejected by the backend.
- This ADR covers the scalar compiler subset only. Object/image emission and
  the runtime ABI remain M7 work.

## Consequences

- `sum.c` compiles to KR16 assembly, assembles, and runs to exit 55.
- The frame layout is simple and deterministic, at the cost of memory traffic
  for every SSA value.
- The full KR16 ABI appendix must extend, not silently replace, these rules.
