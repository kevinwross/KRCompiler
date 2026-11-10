# IR

The v1.0 typed-SSA core lives in `ir.h`/`ir.cpp`: structural types, named records, constants, globals, functions, blocks, instructions, `IRBuilder`, the dominance/type verifier, and the deterministic textual printer.

The frozen contract is [docs/ir.md](../docs/ir.md) and [ADR 0003](../docs/decisions/0003-ir-core-freeze.md); `plan.md` §8 remains the design context. C IRGen and the KR32 backend consume this core in M2.
