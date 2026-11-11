# KRCompiler documentation

The plan is approved (2026-11-09). Specifications marked frozen are stable contracts; the rest remain drafts until the freeze gate named in their header. For frozen documents, `plan.md` and the owning ADR remain the design context.

## Core

- [plan.md](../plan.md) — design contract, gates, decision log.
- [implementation.md](../implementation.md) — live milestone tracker.
- [tree.md](tree.md) — host/target repository boundary and build output contract.
- [planning/source-tree-reorganization.md](planning/source-tree-reorganization.md) — source/build reorganization plan.
- [planning/nemo-plan-review.md](planning/nemo-plan-review.md) — review findings and responses.

## Specifications

- [abi.md](abi.md) — C/C++ ABI, data models, calling conventions, mangling, EH/RTTI.
- [abi-kr16.md](abi-kr16.md) — KR16 IP16 scalar ABI, frame layout, and link rules.
- [object.md](object.md) — `KRO` object/executable container.
- [symbols.md](symbols.md) — `KDBG` symbol and debug format.
- [ir.md](ir.md) — typed SSA intermediate representation.
- [attributes.md](attributes.md) — target and standard attributes.

## Targets

- [targets/schema.md](targets/schema.md) — frozen target descriptor schema v1.
- [targets/kr32.md](targets/kr32.md)
- [targets/kr16.md](targets/kr16.md)
- [targets/kr64.md](targets/kr64.md)

## Decisions

- [decisions/README.md](decisions/README.md) — ADR process and index.
