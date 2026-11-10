# ADR 0004: Add constant data arrays and function declarations to the IR core

- Status: accepted
- Date: 2026-11-09
- Decision: extends the v1.0 IR core with byte-array constants and declaration-only functions
- Supersedes: none

## Context

C string literals need a global initializer that carries bytes, and external C functions such as `putchar` need module-level function values before their definitions are available. The frozen v1.0 IR core in ADR 0003 only described scalar constants and functions with bodies, so C IRGen cannot represent either without inventing a target-specific side channel.

## Decision

The IR core adds:

- `ConstantDataArray`, a typed byte-array constant printed as `c"..."`; its type is an array of `i8`.
- `Function::declaration`, a flag for a declaration-only function with no basic blocks. The printer emits `declare`; the verifier accepts an empty block list only when the flag is set.

No other opcode or type changes are made. Both additions are part of IR schema version 1 and are covered by the verifier and textual dump.

## Consequences

C globals can carry string bytes and external declarations can be referenced by calls. The verifier must distinguish an intentional declaration from an empty definition. A future aggregate-constant representation can reuse `ConstantDataArray` or add a typed aggregate constant under a new ADR.

## Alternatives considered

- Encode strings as separate target sections: rejected because it leaks object layout into the front end/IR.
- Synthesize a body for external declarations: rejected because it would misrepresent linkage and allow invalid definition checks.
- Defer strings to M3: rejected because `hello.c` and `mainentry.c` need their string literals at G2a.
