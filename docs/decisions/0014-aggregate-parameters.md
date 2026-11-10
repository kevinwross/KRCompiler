# 0014 — By-value aggregate parameters on KR32

**Status:** Accepted

## Context

The KR32 backend treated an aggregate argument as a single 32-bit word. Passing
an 8-byte `Point` by value moved only its first field, so the callee observed a
partly uninitialized object. The C++ class work needs ordinary by-value
parameters for small records, and the C ABI already assigns aggregates above
8 bytes to memory.

## Decision

- A by-value aggregate parameter of up to 16 bytes is split into 4-byte words.
  Its words occupy consecutive integer argument registers, beginning at the
  next free integer register.
- The caller copies the argument's words from its frame slot into those
  registers. The callee prologue stores the registers into the parameter's
  frame slot at 4-byte intervals.
- `load` and `store` of an aggregate value copy every 4-byte word between the
  source and destination.
- A parameter that does not fit in the remaining integer registers is
  unsupported and diagnosed in the assembly; passing such an aggregate on the
  stack is deferred.
- Aggregate return values remain unsupported; the `sret` path is still the
  intended future mechanism.
- The IR signature and object format are unchanged. This is a KR32 calling
  convention clarification within ADR 0001, not a new ABI version.

## Consequences

- Small records can be passed by value and read normally in the callee.
- The backend keeps its existing frame-slot model; no register allocator change
  is required.
- Very large aggregates and aggregate returns stay out of scope until the
  stack-parameter and `sret` work lands.
