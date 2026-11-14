# 0033 — Non-type template parameters

- Status: accepted
- Date: 2026-11-13
- Decision: how a parameter written with a type before its name becomes a number in an instance
- Scope: front end and IR generation; extends the class-template instance rule of ADR 0032

## Context

[ADR 0032](0032-class-template-instantiation.md) instantiates a class template
per type, which covers `Box<int>`. A fixed-size container needs more:
`template <typename T, int N> class Buffer` must give each instance an array of
`N` elements, and `N` must be usable as a value (`return N;`) as well as a size.

The parser already read the parameter list, but every name became a type: `N`
got a `typedef int` stub, so `T data[N]` was an array of one element in the
instance's fallback record and had no meaning at all in an expression.

## Decision

- The parser distinguishes the two forms while reading the list: a parameter
  preceded by declaration specifiers (`int N`, `unsigned N`, `long N`) is a
  value parameter, and one written `typename T` or `class T` is a type
  parameter. A value parameter is tagged `template-value-param:<name>` on the
  definition instead of `template-param:<name>`, and gets a
  `const int <name> = 1;` stub rather than a typedef, so the pattern's own
  record still has a name to resolve.
- Sema records which parameters are values. Instantiating resolves a type
  argument through the specifier table as before and a value argument as an
  integer literal; a value that is not an integer literal (or a hexadecimal one)
  instantiates nothing.
- The instance's tag carries the value: `Buffer__int__4`, `Buffer__double__2`.
  A value and a type parameter therefore cannot collide, and two instances with
  the same element type and different sizes are distinct records.
- Before analysis, every reference to a value parameter in the cloned specifier
  is replaced by an integer literal. Array bounds and expressions then need no
  special case in Sema or in code generation, and the record's layout follows
  from the literal.
- While the pattern's own record is analysed, each value parameter is bound to
  the stub's `1` so the fallback record is a real (if arbitrary) layout. The
  binding is consulted when a constant is folded, and a folded identifier is
  recorded so code generation can still emit the number after the binding is
  gone.

## Consequences

- `Buffer<int, 4>` is a record with `[4 x i32]` and a size of 4;
  `Buffer<double, 2>` is another record with `[2 x f64]` and a size of 2;
  `capacity()` returns the bound and `push` refuses past it.
  `krc_run_non_type_templates` requires exit 42.
- Only integer literals are accepted as value arguments: a named constant, an
  expression, or a negative value read from a constant all instantiate nothing.
  A pointer or reference value parameter, a `bool` parameter, and a value
  parameter on a *function* template are not implemented.
- The parser's stub for a value parameter is `const int <name> = 1;` at file
  scope, so the uninstantiated class template resolves with an array of one
  element. That record is not the same as any instance and is only a fallback,
  as with the type-parameter stub.
- `&&` and `||` lower to a bitwise operation over both operands, so neither
  short-circuits: a side effect on the right of a false `&&` still runs. That is
  a separate defect, recorded in `implementation.md`, and not caused by this
  decision.

## Alternatives considered

- Carry the value in `template_bindings_` and consult it in `evaluate_constant`
  only: rejected because code generation runs after the bindings are gone, so
  `return N;` had no value to emit. The literal substitution removes the whole
  question.
- Encode the value in the record's tag only and rebuild the bound by parsing it
  back: rejected as fragile; the clone is already per-instance, so substituting
  costs nothing.
