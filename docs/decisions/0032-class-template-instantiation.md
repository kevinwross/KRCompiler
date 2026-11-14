# 0032 — Class template instantiation

- Status: accepted
- Date: 2026-11-13
- Decision: how `Box<int>` becomes a record, and what an instance's members are named
- Scope: front end and IR generation; adds a record tag rule for instances

## Context

Function templates instantiate since [ADR 0031](0031-function-template-instantiation.md),
but a `template <typename T> class Box` was only a name: the parser replaced
`T` with `int` and Sema analysed one record, so `Box<double>` had `int` fields
and `Box<int>`/`Box<double>` could not coexist. A class instantiation needs more
than substitution: its own record, its own member-function bodies, its own
constructor and destructor names, and a cache so that `Box<int>` written twice
is one type.

The pieces that make it possible already exist. `template_bindings_` resolves a
parameter name during type resolution, `clone_ast` gives the instance its own
AST nodes (the recorded expression types are keyed by node address), and Sema's
`record_type` builds a `RecordInfo` from a specifier — including the member
types, which is the only moment an instance's bindings are active.

## Decision

- The parser keeps the `int` substitution and tags a template's class specifier
  with `template-param:<name>` labels, as it does for function definitions. A
  class template stays usable through its bare name, which resolves to the
  substituted record, the same fallback a function template has.
- Sema records the tagged specifier as a class pattern. `Box<int>` in a
  declaration is parsed as a class name plus a `template-arg:` list and resolves
  through `instantiate_class_template`, which binds the parameters, clones the
  specifier, renames its tag to `Box__int`, and analyses the clone at file scope
  with only that binding active.
- The instance's constructors and destructor are renamed to the instance's tag
  (`Box__int`, `~Box__int`). A constructor or destructor is recognised by
  comparing its name with its class's name, so an unrenamed instance would get
  an ordinary member function instead of construction or destruction.
- The injected class name inside the pattern means the *instance*: the clone
  rewrites every label and identifier equal to the pattern's tag into the
  instance's tag, so C* self() and const C& other in C<int> name
  C__int. Before that rewrite the member resolved to the pattern's own type.
- A class-template name with no template arguments is not an instantiation: the
  label resolves through the ordinary typedef the pattern's record declares.
  Without that distinction a bare C consumed the label and left the
  declaration with no type, and re-entering a pattern whose instance is already
  being built recursed until the stack overflowed.
- Instances are cached by their tag, not by the spelled arguments, so `Box<int>`
  and a reference through the parameter (`Box<T>` inside the instance) are one
  record. The tag is entered in the cache before the analysis so a member that
  refers to its own instance resolves to the incomplete record instead of
  starting a second instantiation.
- IR generation collects the instance's members from the recorded specifier and
  takes a member's type from the analysed record rather than re-deriving it:
  re-deriving happens after the bindings are gone, so `T` would resolve to the
  pattern's stub.
- The instance tag follows the function-instance rule: `<tag>__<type tag>` per
  parameter in declaration order — `Box__int`, `Pair__int_double` — and the
  instance's globals and functions inherit it.

## Consequences

- `Box<int>` and `Box<double>` are two records with two layouts and two sets of
  member functions in one translation unit, `Pair<int, double>` and
  `Pair<double, int>` are different types, a member may take its own instance by
  reference, and a pointer to an instance works. `krc_run_class_templates`
  requires exit 42.
- Two instantiations of the same pattern that resolve to the same tag are one
  instance; an instantiation that needs a type the front end cannot spell
  produces no record and the declaration fails as it did before.
- A member defined outside the class body (`template <typename T> T Box<T>::get()
  const`), a non-type parameter (`Box<int, 4>`), a default argument, partial
  specialisation, and a member template are not supported. Each is a separate
  decision, and none is approximated: the parser consumes `Box<int, 4>` but the
  instantiation rejects the argument that has no type.
- The instance's destructor participates in the ordinary cleanup machinery of
  [ADR 0027](0027-destructor-cleanup.md), so a scope may hold several instances
  and their destructors run in reverse construction order.

## Alternatives considered

- Give each instance a mangled Itanium tag (`_ZN3BoxIiE`): deferred with the
  function-instance mangling; the tag form is unambiguous for this profile and
  readable in a dump.
- Reuse the pattern's AST in place with a stack of bindings: rejected for the
  same reason as ADR 0031 — the recorded expression types and the member
  declaration nodes would be shared between instances.
- Instantiate lazily during code generation instead of during analysis: rejected
  because the member types must be resolved while the binding is active, and
  code generation runs after the scopes are gone.
