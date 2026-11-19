# 0050 — Canonicalise constructed types

**Status:** accepted 2026-11-18.
**Date:** 2026-11-18.
**Relates to:** `D-049` (selection quality is lowering), [ADR 0049](0049-register-allocation-wiring.md),
`src/ir/ir.cpp` (`TypeContext`).

## Context

`TypeContext` caches the primitive types — `void`, `bool`, `i8`…`i64`, floats — but
`pointer_type`, `array_type` and `function_type` each **construct a fresh `Type` on
every call**. Two `ptr<i32>` therefore have different addresses, and the IR's
consumers compare type *pointers*: the verifier rejects an operation whose result and
operands are structurally identical but not the same instance, and IRGen inserts a
`bitcast` to restore the instance a consumer expects.

Measured consequences in the current tree:

- **263 of the 570 `bitcast`s in the two runtime sources have source and target types
  that are structurally identical** — `bitcast i32 0 to i32` and its kind. Each costs
  an instruction, a frame slot, a store, and a load at every later use.
- Folding those casts was attempted and **could not be landed**: replacing an identity
  cast with its operand made the module fail verification with

  ```
  integer binary operation has mismatched types: add result i32 operands i32, i32
  ```

  Every type in that message is `i32`. Narrowing the fold to pointer-identical types
  removed nothing at all, which is what established the mechanism: the casts exist
  because the pointers differ.

`D-049` makes selection quality a property of the lowering rather than of an
optimisation level, so removing these casts belongs at `-O0` and should pay on every
build. It cannot be done while equal types have distinct addresses.

## Decision

**Canonicalise constructed types in `TypeContext`.** `pointer_type`, `array_type` and
`function_type` return a previously built instance when one with the same shape already
exists, so structural equality and pointer equality agree. The verifier's pointer
comparison then means what it reads as saying, and a later change may fold identity
casts at the lowering.

The cache is keyed on the constructed shape — element type plus count, element type,
or return type plus parameters and variadic flag — and is per `TypeContext`, which is
per `Module`, so no state crosses a compilation.

## Alternatives rejected

**Make the verifier compare types structurally instead of by pointer.** It would let an
identity cast be folded without canonicalisation, but every comparison in the compiler
would become a recursive walk — the backend, the linker and the object writer all
compare `TypePtr`s today — and the same defect would keep producing redundant casts
between values that are already the right type.

**Fold identity casts by rewriting the *producer* to the wanted instance.** That is what
IRGen does today, and it is the source of the casts.

**Leave it and accept the casts.** They are 0.5 instructions per call of type-returning
work in the runtime alone, they obscure every IR dump read during this work, and the
verifier message above cost a round to diagnose.

## Consequences

- Type identity becomes reliable. `-O0` output changes — the casts disappear once the
  lowering folds them — which is intended and is measured rather than assumed; the
  `-O0` byte-identity guarantee that ADR 0049 asserts is about the `-O1` flag not
  changing output, and it is unaffected.
- A cache lookup on every constructed type. Types are built during IR generation, not
  in any inner loop, and the maps are small.
- Determinism: lookups only, no iteration over the caches, so output order is unchanged.
- No change to the object format, the symbol format, the ABI or the instruction set.
