# 0031 — Function template instantiation

- Status: accepted
- Date: 2026-11-13
- Decision: how a function template pattern becomes a callable definition per deduced type
- Scope: front end and IR generation; adds a symbol naming rule for instances

## Context

`template <typename T>` was syntax and nothing more. The parser turned each
parameter into a `typedef int T;` and the body was analyzed once with that
substitution, so `add(3, 4)` worked for `int` and `add(1.5, 2.5)` silently
converted its arguments to `int`. Two templates that both named their parameter
`T` could not appear in one translation unit, because the second stub was a
redefinition.

Real templates need three things the current front end does not have: a place to
keep the pattern, a way to deduce a parameter from a call's arguments, and a way
to analyze the body once per deduced type. The `expression_types_` map is keyed
by AST node, so two instances cannot share one body: each needs its own copy.

## Decision

- The parser keeps the `int` substitution as a fallback and also records the
  template's parameter names on the definition (`template-param:<name>` labels).
  The stub is emitted once per name per translation unit.
- Sema stores a pattern for a definition that carries those labels and still
  analyzes the int-substituted definition, so an all-int call resolves to that
  definition exactly as before. Nothing that works today changes.
- On a call whose callee names a template, Sema deduces each parameter from the
  matching argument: a parameter whose specifiers name a template parameter
  binds that parameter to the argument's type, and `T*`, `T&`, and `const T&`
  parameters bind through a pointer, a reference, or an array that decays to a
  pointer. Every template parameter must be deduced and the deductions must
  agree, or no instance is made and the call falls back to the int definition.
- When the deduction is not the all-int substitution, Sema deep-clones the
  definition, renames the clone's function declarator, removes its
  `template-param:` labels, binds the parameters, and analyzes the clone with
  the file scope only — a template body never sees the caller's scope. The clone
  is kept alive by Sema and recorded for code generation.
- A binding is consulted while types are resolved, before the typedef lookup, so
  a template parameter shadows the name the parser stubbed.
- The instance's symbol is `<name>__<tag>` for each parameter in declaration
  order, where the tag is the type's source spelling: `add__double`,
  `doubled__char`, `first_of__int`. Records use their tag, pointers use
  `ptr_<tag>`, arrays `array_<tag>`. Instances are weak definitions like other
  inline functions, so two translation units that instantiate the same type
  agree.
- An explicit argument list is kept on the call as `template-arg:<spelling>`
  labels; a named parameter is bound to that type and the parameters the list
  does not name are deduced from the arguments. The spelling is resolved through
  the same specifier table the declaration path uses, so `unsigned long` and a
  class name both work.
- The call records the resolved instance, and code generation emits a direct
  call to it and generates the instantiation's definition.

## Consequences

- `add`, `maximum`, `doubled`, and `first_of` work for `int`, `double`, and
  `char` in one translation unit, and `krc_run_template_functions` requires exit
  status 42 across fourteen checks. Two templates may share the name `T`.
- The int instance keeps its old symbol (`add`), so existing objects and the
  fixtures that relied on the substitution still link.
- An explicit argument list binds the parameters it names, and the rest are
  still deduced, so `convert<int>(2.75)` instantiates `convert__int_double`. A
  spelling that does not resolve to a type instantiates nothing and the call
  falls back to the int definition.
- The supported deduction is one level deep: a parameter that needs a nested
  pattern (`T**`, `T* const*`, or a deduction through a conversion) is not
  matched. That limit is recorded rather than approximated.
- Class templates, partial specialization, variadic templates, and fold
  expressions are separate decisions.

## Alternatives considered

- Instantiate from scratch by re-analyzing the original definition in place:
  rejected because `expression_types_` and the recorded AST node addresses are
  shared, so the second instance would overwrite the first's recorded types.
- Mangle with the full Itanium grammar: deferred. The tag form is unambiguous
  for the types the profile has, readable in a symbol dump, and swappable later
  without touching the analysis.
- Reject the all-int substitution and always instantiate: rejected for now,
  because it renames existing symbols and breaks the two fixtures that already
  rely on the substitution; the fallback can be dropped once the mangling is
  settled.
