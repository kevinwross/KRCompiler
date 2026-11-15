# 0039 — A template instance's member bodies are built when used

- Status: proposed
- Date: 2026-11-14
- Scope: front end and IR generation for class templates

## Context

A class template instance analyses and generates every member body at the moment
the instance is created, whether or not anything calls them. That is wrong for a
template whose body is only valid for some element types, and it is what blocks
the library work, because the natural way to write a header is to give every
member a definition and let the caller's use decide whether it is valid.

Two measurements, both from the current tree:

```
std::array<Item, 2> items;      // Item has only an int member
    items[0].value = 5;
    items[1].value = 6;
```

reports `IR verifier: function 'array__Item__2_op_eqeq': icmp has invalid operand
or result types`. Nothing compares the arrays; `array`'s `operator==` compares
elements with `!=`, the elements are records, and a record comparison needs a
`Record::operator==` that does not exist. The body is built and rejected even
though no call reaches it.

```
template <typename T, int N>
int count_of(const std::array<T, N>& values) { return values.size(); }
```

reports `member access on a non-record value`, because the parameter's type
cannot be formed while the class template has no binding.

## Decision

A member function body of a **class template instance** is analysed and generated
when it is first used, not when the instance is created. A body nothing uses is
neither analysed nor emitted; the declaration remains, so the symbol and its type
exist for overload resolution and for the vtable.

Concretely, the work is:

1. **Sema** analyses an instance's member bodies on demand rather than in
   `analyze_class`. The instance records the un-analysed definitions so a later
   use can analyse one.
2. **IR generation** collects an instance member's definition without generating
   it, and generates it the first time a call resolves to that symbol. Everything
   that already looks a member up — `class_methods_`, `class_constructors_`,
   `class_assignments_`, the vtable build — keeps working against the
   declaration.
3. The eager **diagnostic** that fires today for an unused body disappears with
   it. A body that is never used is never checked, which is the rule every C++
   implementation follows.

## Consequences

- `std::array<Item, 2>` compiles for a record with no comparison operator, which
  is the case that blocks `<array>` over records.
- A function template whose signature names a class template becomes reachable,
  because the parameter type can be formed without building that instance's
  members.
- An error in an unused member body is no longer reported. That is the intended
  trade: the alternative is rejecting valid programs to keep a diagnostic.
- Member bodies are generated in use order rather than declaration order, so the
  object file's function order can change. Determinism still holds — the order is
  a function of the source, not of the run — but the double-build comparison has to
  be re-run when this lands.
- The vtable build already tolerates a member with no body (a pure virtual does
  exactly that), so it is the model for the declaration-only case.
