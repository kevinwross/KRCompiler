# ADR 0007: Add the varargs base opcode

- Status: accepted
- Date: 2026-11-09
- Decision: extends the v1.0 IR opcode set with `VaStart`
- Supersedes: none

## Context

C varargs functions need the address of the first stacked variadic argument. On KR32 that address is derived from the frame pointer and the function's final frame size, which is a backend layout fact. Expressing it as ordinary pointer arithmetic in the front end would bake a target ABI into IRGen.

## Decision

The IR adds `VaStart`, a zero-operand instruction returning `ptr<i8>`. The backend lowers it to the first variadic stack slot for the current function. `va_start(ap, last)` in the C front end becomes assignment of this value to `va_list`; `va_arg` is ordinary pointer arithmetic over 8-byte slots.

`VaStart` is well-formed only inside a function with a body and may be used more than once (each occurrence denotes the same base address). It has no textual operands and is accepted by the verifier and printer.

## Consequences

The front end does not need to know frame sizes. The backend must know its final frame layout before emitting the instruction, which it already computes. Targets with different varargs layouts lower the same opcode differently.

## Alternatives considered

- A front-end pointer expression based on `R7`: rejected as target-specific.
- A hidden parameter containing the varargs base: rejected because the C ABI does not reserve one.
- Special-case only `printf` in assembly: rejected because `varargs.c` needs general functions.
