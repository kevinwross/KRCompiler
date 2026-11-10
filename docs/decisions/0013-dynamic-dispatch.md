# ADR 0013: Polymorphic layout and dynamic dispatch

- Status: accepted
- Date: 2026-11-10
- Decision: give polymorphic classes a vptr and build a per-object virtual slot table
- Supersedes: none (extends ADR 0010 for polymorphic classes; static devirtualization from ADR 0011 remains an optimization)

## Context

ADR 0011 introduced closed-world static devirtualization for the G5 fixtures.
Calls through a base pointer to a derived object were explicitly unsupported.
The M5/M6 surface now needs those calls to select the derived override at run
time.

## Decision

- A class is polymorphic when it declares a virtual method or derives from a
  polymorphic class. Non-polymorphic classes keep the ADR 0010 layout.
- A polymorphic root class places a vtable pointer at object offset 0. A class
  derived from a polymorphic base inherits that slot, so the base and derived
  addresses are equal for the single-inheritance hierarchy supported today.
- The vtable is a per-object array of 32-bit function pointers, one slot per
  virtual method. Slots are ordered base-class methods first, then newly
  declared virtual methods; an override reuses the base slot.
- Construction initializes the vtable before the constructor body runs. The
  array is allocated with the runtime `malloc`; the implementation does not
  emit a relocation-bearing read-only vtable yet.
- A pure virtual slot with no implementation holds a null pointer. A dynamic
  call through `p->method()`, a base reference, or an unqualified virtual call
  on `this` inside a base method loads the vptr, indexes the slot, bitcasts
  the function pointer to the static method type, and calls it with the same
  argument list.
- Direct calls on a concrete object, and inherited calls inside ADR 0011
  clones, continue to use static dispatch.

## Consequences

- Object layout changes only for polymorphic classes; `sizeof` includes the
  vptr and the first member moves to offset 4.
- Each polymorphic object allocates one table at construction. The current
  implementation does not free it in the destructor; a later ADR may replace
  the per-object table with a read-only vtable and linker relocations.
- Single inheritance is supported. Multiple inheritance, virtual bases, and
  RTTI remain outside this ADR. A virtual destructor occupies one slot; a
  derived destructor replaces the base implementation in that slot, and
  `delete base_pointer` calls it before freeing. A base reference initializes
  by bitcasting the object address and dispatches through the same slot load
  as a base pointer.
- `docs/abi.md` already states "vtable pointer at object offset 0"; this ADR
  makes that statement true for emitted code.
