# Architecture decision records

ADRs record why a decision was made, what it costs, and what was rejected. They are required before code depends on a change to:

- ABI, calling convention, data model, or mangling;
- object or executable format;
- symbol/debug format;
- target description schema or target capabilities;
- IR semantics, types, or verifier rules.

A change to a `D-xxx` decision also needs an Appendix A entry in `plan.md`. An ADR does not replace that entry; the two serve different purposes.

## Naming

`NNNN-short-title.md`, numbered in order, for example `0001-kr16-abi.md`.

## Template

```markdown
# ADR NNNN: Title

- Status: proposed / accepted / superseded
- Date: YYYY-MM-DD
- Decision: D-xxx (if applicable)
- Supersedes: ADR NNNN (if applicable)

## Context

What forces the decision; what the current contract says.

## Decision

What is being chosen, precisely enough to implement.

## Consequences

What becomes easier, what becomes harder, what must be migrated, and what the cost is.

## Alternatives considered

What was rejected and why.
```

## Index

- [0001-kr32-c-abi-freeze.md](0001-kr32-c-abi-freeze.md) — accepted; freezes the KR32 C data model, layout, calling convention, and varargs rules.
- [0002-target-descriptor-schema-freeze.md](0002-target-descriptor-schema-freeze.md) — accepted; freezes target descriptor schema v1.
- [0003-ir-core-freeze.md](0003-ir-core-freeze.md) — accepted; freezes the v1.0 typed-SSA IR core.
- [0004-ir-data-array-and-declarations.md](0004-ir-data-array-and-declarations.md) — accepted; adds byte-array constants and declaration-only functions.
- [0005-kro-object-format-freeze.md](0005-kro-object-format-freeze.md) — accepted; freezes the KRO v1 object header, tables, ordering, CRC, and relocation registry.
- [0006-recursive-aggregate-types.md](0006-recursive-aggregate-types.md) — accepted; adds forward/completion construction for named aggregates.
- [0007-varargs-base-opcode.md](0007-varargs-base-opcode.md) — accepted; adds the target-lowered `VaStart` opcode.
- [0008-debug-info-bridge.md](0008-debug-info-bridge.md) — accepted; emits a textual `.debug_info` bridge for the KR32 debugger until KDBG lands.
- [0009-kdbg-v1-freeze.md](0009-kdbg-v1-freeze.md) — accepted; freezes the `.kdbg` v1 header, records, CRC, and dump schema.
- [0010-cxx-class-data-layout.md](0010-cxx-class-data-layout.md) — accepted; freezes the first C++ class data layout and naming contract.
- [0011-m5-static-devirtualization.md](0011-m5-static-devirtualization.md) — accepted; specializes inherited methods per concrete class for G5 without a vtable ABI.
- [0012-weak-linkage.md](0012-weak-linkage.md) — accepted; adds weak IR linkage and KRO weak symbols for inline/`constexpr` functions.
- [0013-dynamic-dispatch.md](0013-dynamic-dispatch.md) — accepted; adds a vptr and per-object virtual slot table for single-inheritance dynamic dispatch.
- [0014-aggregate-parameters.md](0014-aggregate-parameters.md) — accepted; defines small by-value aggregate parameters as consecutive integer-register words on KR32.
- [0015-aggregate-returns.md](0015-aggregate-returns.md) — accepted; returns aggregates of up to 8 bytes in `R0`/`R1` on KR32.
- [0016-kr16-scalar-abi.md](0016-kr16-scalar-abi.md) — accepted; defines the scalar KR16 argument registers, frame layout, and return register for M7.
- [0017-kr16-exec-image.md](0017-kr16-exec-image.md) — accepted; emits a minimal single-unit KR16 EXEC image with a generated startup.
- [0018-global-constructors.md](0018-global-constructors.md) — accepted; initializes namespace-scope objects through a generated `main` wrapper.
- [0019-kr16-object-linker.md](0019-kr16-object-linker.md) — accepted; adds KR16 relocatable objects, absolute relocations, and the linker path.
- [0020-kdbg-source-path-policy.md](0020-kdbg-source-path-policy.md) — accepted; keeps KDBG v1 and adds `krld --source-root` relative source paths.
- [0021-external-globals.md](0021-external-globals.md) — accepted; adds external global declarations to IR and `.extern` emission.
- [0022-internal-globals.md](0022-internal-globals.md) — accepted; adds internal global linkage for C `static` objects.
- [0023-kr32-64-bit-integers.md](0023-kr32-64-bit-integers.md) — accepted; lowers 64-bit integers as a KR32 word pair, with two-word arguments, an `R0`/`R1` return, and division libcalls.
- [0024-kr32-tile-intrinsics.md](0024-kr32-tile-intrinsics.md) — accepted; adds the IR intrinsic opcode, lowers the `kr32::matrix` surface to tile instructions, and implements memory-class aggregate returns in `R4`.
- [0025-rtti-vtable-typeinfo.md](0025-rtti-vtable-typeinfo.md) — accepted; replaces the per-object vtable with weak `_ZTV`/`_ZTI` globals and implements `typeid` and pointer `dynamic_cast`.
- [0026-exception-descriptors.md](0026-exception-descriptors.md) — accepted; freezes the `.cpp_except` descriptor, the frame walk, landing pads, and the exception runtime, without destructor cleanup yet.
- [0027-exception-cleanup.md](0027-exception-cleanup.md) — accepted; adds cleanup landing pads that run live destructors and chain into the handler.
- [0028-kr16-stack-arguments.md](0028-kr16-stack-arguments.md) — accepted; freezes how KR16 passes more than three scalar arguments.
- [0029-kr16-variadic-calls.md](0029-kr16-variadic-calls.md) — accepted; freezes how KR16 passes and reads a variadic argument list.
- [0030-debug-display-names.md](0030-debug-display-names.md) — accepted; gives IR functions a debug display name used in the F record.
- [0031-function-template-instantiation.md](0031-function-template-instantiation.md) — accepted; deduces a function template's parameters and instantiates it per type.
- [0032-class-template-instantiation.md](0032-class-template-instantiation.md) — accepted; instantiates a class template per type as the record Tag__type.
- [0033-non-type-template-parameters.md](0033-non-type-template-parameters.md) — accepted; a parameter written int N becomes a literal in the instance, tagged Buffer__int__4.
- [0034-integer-literals-on-narrow-targets.md](0034-integer-literals-on-narrow-targets.md) — accepted; types an integer literal per C11 on a narrow profile and diagnoses one that fits no type, and records that a 64-bit value still cannot be carried.
- [0035-class-template-member-bodies.md](0035-class-template-member-bodies.md) — proposed; analyses an instance's member bodies against the instance's bindings, so a body no longer sees the pattern's stub type.
ame__tag.

The next expected ADRs are the KR16 ABI appendix and the KDBG record layout.
