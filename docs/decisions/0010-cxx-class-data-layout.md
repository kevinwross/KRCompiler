# ADR 0010: C++ class data layout v0.1

- Status: accepted
- Date: 2026-11-09
- Decision: freezes the first C++ class data-layout and naming contract
- Supersedes: none

## Context

The C++ front end starts with class declarations. The parser now accepts
`class Name { ... };`, access labels, and `Name value;` declarations. Before
adding methods, inheritance, or vtables, the data layout and the class-as-type
rule need a stable contract.

## Decision

- A class with data members lays out exactly like the equivalent C `struct`:
  declaration order, natural alignment, no hidden fields, no padding changes.
- A class tag is also an ordinary type name; `class Point` and `Point` denote
  the same record type.
- `public:`, `private:`, and `protected:` are accepted and currently do not
  change access checking or layout.
- Virtual dispatch, base classes, constructors, and destructors are not part of
  this version. They will add a vtable pointer and base subobjects in a later
  ADR before codegen.

The future member-function contract (not implemented yet) is:

- `Class::method` mangles to `Class_method`.
- The implicit `this` pointer is the first integer argument, in `R0` on KR32.
- Member functions otherwise use the frozen KR32 C ABI.

## Consequences

Class data programs compile through the existing struct lowering and can use
the current member access, aggregate initialization, and debug type records.
Adding virtual functions or bases will change class layout and therefore
requires a new ABI version, not an in-place edit of this one.

Access labels currently parse but are not enforced; this is a known M5 gap.

## Alternatives considered

- **Add a hidden vtable pointer now.** Rejected: it would change every class
  layout before virtual dispatch exists and break struct compatibility.
- **Mangle all class names immediately.** Deferred until methods are emitted;
  data-only classes need no symbol names beyond the type record.
