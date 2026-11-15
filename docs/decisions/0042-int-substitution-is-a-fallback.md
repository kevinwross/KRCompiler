# 0042 — The int substitution is a fallback, not an authority on a template body

- Status: proposed
- Date: 2026-11-15
- Decision: a template pattern whose body calls a parameter of
  template-parameter type is not analysed or generated; only its instances are
- Amends: ADR 0031 (the int-substituted pattern)
- Scope: front end and IR generation

## Context

ADR 0031 analyses each function-template pattern with every type parameter
substituted by `int`, so that the common all-`int` call has a definition to call
without an instance being created. That works because most bodies do not care
what `T` is.

Six rounds of measurement on one shape show it does not hold for a body that
**calls a parameter**:

```cpp
template <typename Compare>
int larger_of(int left, int right, const Compare& compare) {
    return compare(left, right) ? right : left;   // line 19
}
```

`larger_of(3, 7, less)` fails with *called object is not a function*. What the
measurements established, in order:

- The failure is **not** call syntax, `operator()`, namespace scope, or
  reference parameters: a non-template function taking `Less` by value works,
  and so does a plain struct with the same `operator()`.
- At the call site the template case reports `callee_kind=4` — the **int stub** —
  **before any instance exists**. The pattern's best-effort analysis errors, and
  the error is reported as though the program were wrong.
- The diagnostic exists in **two** places, `sema.cpp` and `c_irgen.cpp`, so
  suppressing it in the front end moved the failure rather than removing it.
- Skipping the pattern's body and telling IR generation the same moved the error
  into the **instance**, at the same call.
- With the pattern skipped, a by-reference parameter then links with
  `undefined symbol 'larger_of'` — the call site still targets the pattern,
  because `instantiate_template_call` returns empty for a parameter declared
  `const Compare&`.

So there are **three** distinct defects, not one: the pattern's analysis, the
instance's analysis of the same call, and instantiation through a deduced
reference parameter.

## Decision

For a pattern whose body calls a parameter declared with a template-parameter
type:

- the body is **not** analysed from the pattern and **not** generated from it;
- the pattern remains a declaration, and the instance is the only definition;
- IR generation is told the same, so the two diagnostics cannot disagree.

The int substitution keeps its purpose for bodies it can describe. It is a
fallback for the common all-`int` call, not a claim about what the body means,
and a diagnostic derived from it is not a verdict.

## Consequences

- A body that calls a template-parameter-typed parameter stops producing a false
  error at the pattern. That part is measured: with the pattern skipped, all four
  callable forms pass `-fsyntax-only`, where none did before.
- **It is not a complete fix**, and the two remaining defects are open: the
  instance still fails at the same call, and instantiation through a deduced
  reference parameter still returns empty so the call targets the pattern.
  Neither is addressed here, and this record should not be read as if it were.
- Every attempt so far has been reverted rather than landed, because each one
  fixed one defect and left the other two, and a state where `-fsyntax-only`
  accepts what `-c` rejects is worse than one where both reject.
- The next work is the instantiation gap on its own — `instantiate_template_call`
  returning empty for `const Compare&` — because it is independent of both
  analysis defects and is measured by a link failure rather than a diagnostic.
