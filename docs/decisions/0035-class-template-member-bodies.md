# 0035 — A class-template instance's member bodies resolve its parameters

- Status: proposed
- Date: 2026-11-14
- Decision: a member body of a class-template instance is analysed against the
  instance's bindings, not the pattern's stub
- Scope: front end; amends a claim in [ADR 0032](0032-class-template-instantiation.md)

## Context

[ADR 0032](0032-class-template-instantiation.md) says an instance's clone is
"analysed at file scope with only that binding active", and that IR generation
takes a member's type from the analysed record rather than re-deriving it. That
holds for **declarations**: `Holder<Item>` produces
`Holder__Item_ctor_1_value(ptr<%Holder__Item> %this, ptr<%Item> %value)`.

It does not hold for **bodies**. A class-template destructor that owns a `T*` and
deletes it destroys nothing:

```cpp
template <typename T>
class Holder {
public:
    explicit Holder(T* value) : pointer(value) {}
    ~Holder() { delete pointer; }
private:
    T* pointer;
};
```

`Holder<Item> held(new Item());` leaves `~Item` uncalled, and the IR shows why:
`Holder__Item_dtor` calls only `free`, never `Item_dtor`. Copying the member into
a local first — `T* local = pointer; delete local;` — changes nothing, so the
lowering of `delete` is not at fault; the body's idea of `T` is. Delegating to a
function template makes it visible in the symbol: `destroy_owned(pointer)` is
instantiated as `destroy_owned(ptr<i16>)` for `unique_ptr<Item>`, the stub
substitution `int` the parser writes for a class parameter.

The same `delete` destroys correctly in `main` and in a **function** template, so
the defect belongs to class-template member bodies.

## Decision

A class-template instance's member bodies are analysed while its bindings are
active, so every expression type recorded for the body names the instance's
types. Concretely, `T* pointer` in `Holder<Item>` is `Item*` inside
`~Holder__Item`, `delete pointer` selects `Item`'s destructor, and a call that
deduces from such a pointer deduces `Item`.

The analysis has one pass per instantiation, keyed by the instance's AST nodes,
so the pattern's own analysis of the same body cannot supply the instance's
types. The pattern keeps its stub analysis, which is what makes a pattern usable
through its bare name.

## Consequences

- An owning class template stops leaking: `std::unique_ptr` becomes correct, and
  `tests/fixtures/c/unique_ptr.cpp` — written and unregistered — is the check.
- `delete` on a member of type `T*` and template-argument deduction from such a
  member both follow the instance, which is what `<memory>`, `<tuple>`, and
  `<functional>` need.
- Analysis is per instance, so a class template with many instances analyses its
  member bodies that many times. That is the same cost function-template
  instantiation already pays.
- Nothing in the ABI, object format, or IR changes: the instance's symbols are
  already the instance's. The defect is entirely in which types the front end
  records for a body.
