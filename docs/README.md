# KRCompiler documentation

The plan is approved (2026-11-09). Specifications marked frozen are stable contracts; the rest remain drafts until the freeze gate named in their header. For frozen documents, `plan.md` and the owning ADR remain the design context.

## Core

- [plan.md](../plan.md) — design contract, gates, decision log.
- [implementation.md](../implementation.md) — live milestone tracker.
- [release-notes.md](release-notes.md) — what v1.0 contains, how to build it, and what it does not contain.
- [tree.md](tree.md) — host/target repository boundary and build output contract.
- [planning/source-tree-reorganization.md](planning/source-tree-reorganization.md) — source/build reorganization plan.
- [planning/nemo-plan-review.md](planning/nemo-plan-review.md) — review findings and responses.

## Guides

- [krc-user-manual.md](krc-user-manual.md) — building and linking C and C++ programs for the KR VMs with `krc` and `krld`.

## Specifications

- [toolchain-embedding.md](toolchain-embedding.md) — `krc::toolchain` and `krc::debug`: package, call sequence, ownership, errors, listing.
- [abi.md](abi.md) — C/C++ ABI, data models, calling conventions, mangling, EH/RTTI.
- [abi-kr16.md](abi-kr16.md) — KR16 IP16 scalar ABI, frame layout, and link rules.
- [abi-kr216.md](abi-kr216.md) — KR216 ABI: `Ip16Long32` data model, registers, frames, aggregates, object rules.
- [abi-kr232.md](abi-kr232.md) — KR232 ABI: `Ilp32` without a 64-bit type, the R6 aggregate destination, frames, vtables, `.kr232` image rules.
- [host-services.md](host-services.md) — KR32 host-service wrappers, errno, flags, and sandbox.
- [object.md](object.md) — `KRO` object/executable container.
- [symbols.md](symbols.md) — `KDBG` symbol and debug format.
- [ir.md](ir.md) — typed SSA intermediate representation.
- [attributes.md](attributes.md) — target and standard attributes.

## Targets

- [targets/schema.md](targets/schema.md) — frozen target descriptor schema v2.
- [targets/kr32.md](targets/kr32.md)
- [targets/kr16.md](targets/kr16.md)
- [targets/kr64.md](targets/kr64.md)

## Decisions

- [decisions/README.md](decisions/README.md) — ADR process and index.
