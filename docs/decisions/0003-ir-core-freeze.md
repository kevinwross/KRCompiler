# ADR 0003: Freeze the v1.0 IR core

- Status: accepted
- Date: 2026-11-09
- Decision: freezes the v1.0 typed-SSA IR core in `docs/ir.md`
- Supersedes: none

## Context

M2 lowers C to a target-independent IR and then to KR32. The IR must have a stable type grammar, opcode set, operand rules, verifier contract, and dump shape before the C IRGen and instruction selection depend on it. The approved plan §8 states the goals but leaves the exact v1.0 core to the IR specification.

## Decision

`docs/ir.md` is frozen for the v1.0 core:

- the type grammar (`void`, explicit integer/FP widths, `ptr<T>`, arrays, structs, unions, functions, named records);
- the arithmetic, conversion, comparison, memory, control-flow, SSA, aggregate, and call opcodes;
- memory flags and the `volatile`/atomic contract;
- the debug-location representation;
- the verifier rules;
- the human-facing dump grammar.

The v1.1 exception, RTTI, and metadata surfaces are named but not frozen. Their opcodes are reserved and the v1.0 verifier rejects them. No serialised IR format is promised.

## Consequences

C IRGen and the KR32 backend can be written and tested against one contract. A type/opcode/verifier change now needs an ADR and a contract update. The IR text dump is explicitly not an interchange format, so formatting changes are not compatibility events.

## Alternatives considered

- Freeze only the type system and defer opcodes: rejected because the verifier and backend need the opcode operand rules together.
- Adopt LLVM IR: rejected because the project needs its own compact contract and must not depend on an external toolchain.
- Freeze serialised IR now: rejected because v1.0 has no tooling requirement for a stable artifact.
