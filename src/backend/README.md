# Backend

M2 has a KR32 assembly emitter in `kr32_backend.*`: it lays out every SSA value and alloca object in a frame, uses R0–R2 as scratch, stages call arguments into R0–R3 and the varargs area, and emits `.text`/`.data` assembly for `krc -S`.

Still pending: the descriptor register/instruction tables, real instruction selection, graph-colouring register allocation, peepholes, and the shared assembler/encoder used by the object writer. KR16 follows at M7.

Design: [docs/ir.md](../docs/ir.md), [docs/targets/kr32.md](../docs/targets/kr32.md), `plan.md` §9.
