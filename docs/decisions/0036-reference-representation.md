# 0036 — How a reference is represented

- Status: proposed
- Date: 2026-11-14
- Decision: what a record value is in IR, what a reference slot holds, and what a
  reference-returning call answers with
- Scope: IR generation only; no ABI, object, symbol, or IR-format change

## Context

Four attempts at one rule — `Item& named = pick(item);` should name the object
`pick` returned — each failed differently, and the reason is that three
representations are used interchangeably for the same thing.

- A **record value** is its address. `alloca %Item` produces an address, and a
  member access goes through it. So `Struct`-typed IR values are pointers in
  substance, and `store %Item %value, %slot` copies through an address.
- A **reference** is also an address, stored in a slot of pointer type. The
  declaration path allocates `alloca ptr<%Item>` and stores the address there.
- A **reference-returning call** answers with an address, but `reference_result`
  *loads* the record from it, producing a value that no longer identifies the
  object. That load is correct for a scalar (`int& next()` copied into an `int`)
  and wrong for a record, where the address *is* the value.

The binding then has a value where it expected an address and cannot tell the
difference, because a `Struct`-typed IR value looks the same either way. Two
further hazards make patches unreliable: `TypePtr` equality is pointer equality,
so two structurally identical types compare unequal (`ir::same_type` exists for
this), and asking Sema for an expression's type from IR generation re-analyses
the expression outside its scopes.

## Decision

1. A record-valued expression **is** its address, always. No load is emitted to
   "read" a record, and no temporary is allocated to hold one.
2. A reference slot holds an address. Binding a reference stores an address;
   reading through a reference loads the pointer from the slot and then reads
   the object through it.
3. A reference-returning call answers with its address. `reference_result`
   returns that address for a record element and loads only for a scalar or
   floating element, where a value context genuinely wants the value.
4. Type comparisons in IR generation that decide whether a cast is needed use
   `ir::same_type`, never `TypePtr` equality.

## Consequences

- `Item& named = pick(item)` aliases the object, so a later write through the
  object is visible through the binding, and `values.front() = 11` — the recorded
  gap in `handoff.md` — becomes expressible.
- A value-returning record call still binds to its return slot, which lives in
  the caller's frame for as long as the reference does.
- A scalar reference from a call keeps its temporary, because there the load is
  what the value context wants.
- The four failed attempts and the instrumented traces that led here are in
  `implementation.md` under 2026-11-14; the trace that decided it prints
  `REFBIND addr=1 addr_kind=3 slot_is_ptr=1 value_kind=3 value_is_slot=0`, which
  says the slot is a pointer, the value is a pointer, and only the pointer
  comparison against the slot type failed.
