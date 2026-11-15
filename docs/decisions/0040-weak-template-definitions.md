# 0040 — A template definition is weak, and the weak binding survives the object

- Status: accepted
- Date: 2026-11-15
- Decision: a template's definition is emitted with weak linkage; the KR16
  assembler accepts `.weak`, and the object writer records the weak binding
- Scope: symbol format and object format

## Context

Two translation units that instantiate the same template — or that both include
an inline definition — each emit the same symbol. C++ expects that to link: the
definitions are identical, one is chosen, and the rest are discarded.

`krld` already implements that rule: it accepts a duplicate when either
definition is weak, and lets a strong definition override a weak one. What it
never received was a weak symbol. Compiling `twice(21)` in two units and linking
them reported `duplicate symbol 'twice'`, because the definition arrived as a
strong global.

Three layers dropped the binding on the way:

- IR generation decided linkage from `inline` and `constexpr` only, so a
  template's definition — including the all-int pattern, which is reused as the
  `int` instance — was **external**.
- the KR16 printer emitted `.global` for every non-internal symbol, with no
  `.weak` form to emit;
- `AssembledSymbol` had `global` and `external` but no `weak`, so even a printer
  that wanted to emit one had nothing to record it in, and the object writer
  chose between Global and Local only.

## Decision

- A function definition carrying a `template-param:` or `template-value-param:`
  label is emitted with `ir::Linkage::Weak`, whether it is the pattern or an
  instance.
- The KR16 printer emits `.weak <name>` for a weak function or global.
- The KR16 assembler accepts `.weak <name>`, which also makes the name global,
  and records `AssembledSymbol::weak`.
- The object writer maps a weak symbol to `KroSymbolBinding::Weak`.

## Consequences

- A template instantiated in two translation units links and runs; the two
  units whose `twice` was `duplicate symbol` now link to exit 42.
- Every template definition changes from a strong symbol to a weak one, so a
  symbol table shows `.weak` where it showed `.global`. Nothing in the suite
  depended on the old spelling, and the determinism checks still pass.
- Section GC and relocation relaxation, the other two parts of the linker
  hardening item, are untouched by this decision.
- The multi-translation-unit case has **no automated test yet**: the KR16 link
  script takes a single source, and the reproduction is the pair
  `build/corpus/tu_a.cpp` and `build/corpus/tu_b.cpp` linked by hand. Adding a
  second source parameter to that script is the next step, not an optional one.
