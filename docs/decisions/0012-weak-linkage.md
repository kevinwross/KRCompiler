# ADR 0012: Weak function linkage

- Status: accepted
- Date: 2026-11-10
- Decision: add `weak` to the IR linkage model and emit KRO weak symbols
- Supersedes: none

## Context

C++ `inline` and `constexpr` functions may be defined in every translation
unit that uses them. Emitting each definition as a strong global symbol made a
multi-object link fail with a duplicate-symbol error. An earlier fallback gave
each copy internal linkage, which links but duplicates code and prevents the
linker from choosing one definition.

KRO v1 already defines `KroSymbolBinding::Weak`, and the linker already prefers
a strong definition over a weak one and accepts multiple weak definitions. The
missing piece was a way for the compiler to mark an IR function weak.

## Decision

The IR `Linkage` enum gains a third value, `Weak`. The IR printer renders
`weak define`, the KR32 backend emits `.weak <name>`, the assembler records the
name in a weak set, and `kro_writer` maps it to `KroSymbolBinding::Weak`.

`inline` and `constexpr` functions use weak linkage. `static` functions remain
internal. Ordinary functions remain external.

The KRO object format is unchanged: weak binding already existed in v1. The IR
core's linkage enum is extended under [ADR 0003](0003-ir-core-freeze.md), so
this ADR is the required IR-freeze amendment.

## Consequences

- Two translation units may define the same inline function; the linker keeps
  one weak definition.
- A strong definition can override a weak one, which is the usual inline
  replacement behavior.
- Weak functions are emitted once per defining object until link time; section
  GC and COMDAT group folding remain future work.
- IR consumers must handle three linkage values.

## Alternatives considered

- **Keep internal copies.** Rejected: no symbol sharing, larger images, and it
  does not match C++ inline semantics.
- **Add COMDAT group sections.** Deferred: KRO has no group model yet; weak
  binding solves the immediate duplicate-definition failure.
