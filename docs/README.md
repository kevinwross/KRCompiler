# KRCompiler documentation

The plan is approved (2026-11-09). This index lists the documents under `docs/` and the point-in-time documents kept at the repository root. A document marked frozen is a stable contract; the rest are drafts, guides, plans, or records. `plan.md` and the owning ADR remain the design context for every frozen document.

## Core

- [plan.md](../plan.md) — design contract: scope, targets, data models, architecture, gates, and the Appendix A decision log.
- [implementation.md](../implementation.md) — live tracker: current milestone status, milestone evidence, gate records, and the running log.
- [release-notes.md](release-notes.md) — what v1.0 contains, how to build it, and what it does not contain.
- [tree.md](tree.md) — the host/target repository boundary and the build output contract.

## Guides

- [krc-user-manual.md](krc-user-manual.md) — building and linking C and C++ programs for the KR VMs with `krc` and `krld`.
- [kr-build-instructions.md](kr-build-instructions.md) — the family build and gate guide: presets, gate flags, install prefixes, generated images, and the clean-build procedure.
- [new-target-guide.md](new-target-guide.md) — the checklist for bringing up architecture number five.
- [arm64-tester.md](arm64-tester.md) — the tester loop for the Raspberry Pi host.
- [benchmarking.md](benchmarking.md) — the dynamic instruction benchmarks and their recorded rows.
- [cxx-front-end-gaps.md](cxx-front-end-gaps.md) — C++ constructs the front end still rejects, each with a reproducer.

## Specifications

- [toolchain-embedding.md](toolchain-embedding.md) — `krc::toolchain` and `krc::debug`: package, call sequence, ownership, errors, and listing.
- [abi.md](abi.md) — C/C++ ABI, data models, calling conventions, mangling, EH and RTTI. The KR32 C ABI is frozen by [ADR 0001](decisions/0001-kr32-c-abi-freeze.md); the C++/EH sections are draft.
- [abi-kr16.md](abi-kr16.md) — KR16 IP16 scalar ABI, frame layout, and link rules. Draft.
- [abi-kr216.md](abi-kr216.md) — KR216 `Ip16Long32` ABI: registers, frames, aggregates, object rules. Accepted 2026-11-22; [ADR 0056](decisions/0056-kr216-64-bit-calling-convention.md) adds the 64-bit convention.
- [abi-kr232.md](abi-kr232.md) — KR232 `Ilp32` without a 64-bit type, the R6 aggregate destination, frames, vtables, and `.kr232` image rules. Accepted with [ADR 0057](decisions/0057-kr232-target-profile.md).
- [host-services.md](host-services.md) — KR32 host-service wrappers, errno, flags, and the sandbox.
- [display-abi.md](display-abi.md) — the common display command set, canonical block and 16-bit marshalling, the `krc::display` package and the `<krc/display.h>` guest API. Proposed, version 1.
- [object.md](object.md) — the `KRO` object and executable container. Frozen v1.0 by [ADR 0005](decisions/0005-kro-object-format-freeze.md).
- [symbols.md](symbols.md) — the `KDBG` symbol and debug format. v1 is frozen by [ADR 0009](decisions/0009-kdbg-v1-freeze.md); v1.1 adds records additively.
- [ir.md](ir.md) — the typed SSA intermediate representation. The v1.0 core is frozen by [ADR 0003](decisions/0003-ir-core-freeze.md); EH, RTTI, and v1.1 metadata remain draft.
- [attributes.md](attributes.md) — target and standard attributes. Draft.

## Targets

- [targets/schema.md](targets/schema.md) — target descriptor schema v1, frozen by [ADR 0002](decisions/0002-target-descriptor-schema-freeze.md); v2 adds the `this_register` and `sret_register` fields ([ADR 0053](decisions/0053-kr216-target-profile.md)).
- [targets/kr32.md](targets/kr32.md) — KR32 identity and data-model values, fixed by ADR 0001 and ADR 0002.
- [targets/kr16.md](targets/kr16.md) — KR16 profile. Draft.
- [targets/kr64.md](targets/kr64.md) — KR64 placeholder: no backend before the 64-bit VM and ISA are approved.

## Plans and reviews

- [optimize.md](optimize.md) — optimisation strategy, measurements, and the pass list. A draft for discussion, not an approved contract.
- [optimize-review.md](optimize-review.md) — review findings against `optimize.md`; it decides nothing.
- [plan-o1-debugger.md](plan-o1-debugger.md) — the plan for making `-O1` output usable with the debugger. W1–W3 are implemented; W4–W6 are not.
- [build-cleanup-plan.md](build-cleanup-plan.md) — the approved build-cleanup plan, closed 2026-11-27 with P7 blocked on a missing ISA.
- [planning/source-tree-reorganization.md](planning/source-tree-reorganization.md) — the source and build reorganisation; phases 1–2 are implemented, phase 3 onward is not started.
- [planning/nemo-plan-review.md](planning/nemo-plan-review.md) — the review record for `plan.md` and its responses.
- [planning/0062-kr232-integer-offset-forms-review.md](planning/0062-kr232-integer-offset-forms-review.md) — review findings against ADR 0062.
- [planning/tuple-and-layout-priorities.md](planning/tuple-and-layout-priorities.md) — the 2026-11-16 priority list for finishing `tuple` and the `ir_size_of` defect.
- [planning/untracked-fixtures-build.md](planning/untracked-fixtures-build.md) — the 2026-11-26 working note on building checked-in images instead of tracking them.
- [planning/documentation-audit-2026-11-27.md](planning/documentation-audit-2026-11-27.md) — the 2026-11-27 audit: the frozen-text divergences and code defects waiting on an owner decision, each with options and a recommendation.

## Records

- [adopt-krc-log.md](adopt-krc-log.md) — the log of stage A of the toolchain adoption.
- [kdbg-v1-1-log.md](kdbg-v1-1-log.md) — the log of the KDBG v1.1 work.
- [arm64-test-log.md](arm64-test-log.md) — one entry per ARM64 revision the tester host built and tested.

## Point-in-time documents at the repository root

These are kept for their record of a moment; they are not current guidance.

- [../Arm64-readme.md](../Arm64-readme.md) — the ARM64 verification checklist, superseded by [arm64-tester.md](arm64-tester.md).
- [../handoff-11-19-26.md](../handoff-11-19-26.md) — handoff written at revision `085a8f5`.
- [../Optimizer-handoff.md](../Optimizer-handoff.md) — optimisation handoff written at revision `7832371`.
- [../new-target-guide.md](../new-target-guide.md) — the 2026-11-21 target-backend guide at revision `73611af`, superseded by [new-target-guide.md](new-target-guide.md).

## Decisions

- [decisions/README.md](decisions/README.md) — the ADR process and the index of all decision records.
