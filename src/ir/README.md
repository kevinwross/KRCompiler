# IR

`ir.h`/`ir.cpp` hold the typed-SSA core: structural types, named records, constants, globals, functions, blocks, instructions, `IRBuilder`, the dominance and type verifier, and the deterministic textual printer. `passes.cpp`, `mem2reg.cpp`, and `inline.cpp` hold the target-independent passes.

The frozen contract is [docs/ir.md](../../docs/ir.md) and [ADR 0003](../../docs/decisions/0003-ir-core-freeze.md), extended by [ADR 0004](../../docs/decisions/0004-ir-data-array-and-declarations.md). The C and C++ front ends reach this core through `src/irgen/`.
