# ADR 0011: M5 static devirtualization

- Status: accepted; dynamic dispatch is added by ADR 0013, which retains static devirtualization as an optimization
- Date: 2026-11-10
- Decision: specialize inherited C++ methods per concrete class for G5
- Supersedes: none

## Context

G5 requires `hello.cpp` and `balanced-tree.cpp` to run on KR32. The C++
front end has single inheritance, override methods, pure virtual declarations,
and constructors, but no vtable layout or dynamic dispatch yet. The AVL fixture
calls virtual `compare`/`printValue` hooks from inherited base methods on
concrete `EngineerTree` and `IntegerTree` objects.

## Decision

For this milestone, the compiler performs closed-world static devirtualization:

- A concrete derived class receives a specialized clone of each inherited base
  method that has a body.
- The clone's class context is the derived class, so unqualified virtual calls
  inside the clone resolve to the derived override at compile time.
- The derived object uses the base data layout plus derived fields; no vptr is
  inserted, and the base method is not emitted as a callable body when the base
  has pure virtuals.
- Constructors call the nearest base constructor; inherited methods are called
  through the derived clones.

This is not a general vtable ABI. It works when the concrete class is known at
each call site, which is true for the G5 fixtures. A future ADR will introduce
vptr layout and dynamic dispatch for polymorphic pointers before claiming
general virtual-function support.

## Consequences

- G5 runs without a vtable format, vtable relocations, or dynamic dispatch.
- Code size grows by one copy of each inherited method per concrete class.
- Calls through a base pointer to a derived object are not generally correct;
  the implementation must not claim that support.
- The pure-base method bodies are skipped, avoiding unresolved pure calls.

## Alternatives considered

- **Full vtable ABI now.** Deferred: it needs a data-relocation format, vptr
  layout, construction/destruction ordering, and a debug type story.
- **Hand-written specializations in the fixture.** Rejected: the compiler must
  still parse and lower the real C++ fixture.
