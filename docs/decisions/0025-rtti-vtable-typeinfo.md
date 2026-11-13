# 0025 — Read-only vtables, typeinfo, and RTTI

- Status: accepted
- Date: 2026-11-13
- Decision: replaces the per-object vtable with weak `_ZTV`/`_ZTI` globals and
  implements `typeid` and pointer `dynamic_cast`
- Supersedes: the per-object table in ADR 0013 (dispatch semantics are unchanged)

## Context

ADR 0013 gave polymorphic objects a vptr and built a **per-object** table with
`malloc` at construction time. It recorded why that was temporary: no linker
relocations were emitted, and RTTI was outside its scope. Plan §8.6 and §10.4
require the Itanium-like layout instead — vtable pointer at object offset 0,
RTTI pointer at vtable slot -1, typeinfo objects as globals with explicit
layout — and M10 lists `typeid` and `dynamic_cast` as deliverables.

A per-object table cannot serve RTTI: `typeid` must return the same
`type_info` object for a type in every translation unit, and two objects of one
class must report the same type. That requires one linker-merged object per
class, which requires data initializers that name symbols. The IR could express
only integer and byte-array initializers, and the assembler's `.word` accepted
only integers, so the enabling work was an address-valued constant and
section-aware data relocations.

## Decision

### Static metadata layout

For a polymorphic class `C`, IRGen emits three weak globals:

- `_ZTS<C>` — a byte array holding the source-level class name and its
  terminator. A qualified tag flattens to underscores, because the assembler
  symbol must be valid.
- `_ZTI<C>` — four words: the `type_info` vtable pointer (0: the subset has no
  virtual calls on `type_info`), the `_ZTS<C>` address, flags (0), and the base
  class's `_ZTI<Base>` address or 0. The fourth word is the single-inheritance
  `__si_class_type_info` form.
- `_ZTV<C>` — `_ZTI<C>` in word 0, then one word per virtual slot in the
  ADR 0013 order.

An object's vptr is `_ZTV<C> + 4`. Virtual slot `k` is therefore `vptr + 4*k`,
exactly as ADR 0013's dispatch code already reads it, and the RTTI pointer is
`vptr - 4`, which is "slot -1" in words.

The globals are weak: two translation units that use the same class emit the
same definitions, and the linker keeps one, so type identity holds across the
program. Construction no longer allocates a table; it stores the address of
`_ZTV<C> + 4` into the object's vptr slot.

### Enabling IR and object changes

- `ConstantAddressArray` is an array constant whose elements are functions,
  globals, or zero words. It is the initializer of a table of addresses.
- `GlobalVariable::weak` marks a weak definition; the KR32 backend emits
  `.weak` for it. (`internal` already existed for C `static` objects.)
- The assembler accepts `.word <symbol>`, which emits a zero word and a
  relocation; `AssembledRelocation` carries the section the relocation patches
  (0 `.text`, 1 `.data`), and the KRO writer passes it through. Until now every
  relocation was forced into `.text`, which is why a patched data table came
  out as zeros.

### `typeid`

- The parser accepts `typeid ( type-name )` and `typeid ( expression )` and
  builds a `TypeIdExpression`; Sema gives it the `std::type_info` type and
  records the operand's type, because IR generation runs after the function's
  scopes are gone and cannot re-analyse the operand.
- IRGen yields an lvalue of `type_info`, the address of the typeinfo object:
  the object's `vptr[-1]` when the operand's static type is polymorphic, and
  `_ZTI<T>` otherwise. A type-name operand is always static.
- `typeid(a) == typeid(b)` compares the two typeinfo addresses, which is type
  equality because one object exists per class.
- The sysroot gains `<typeinfo>` with the fixed three-word `std::type_info`
  layout, `name()`, and the equality operators.

### `dynamic_cast`

- Pointer form only. A reference cast would need `std::bad_cast`, i.e.
  exceptions, so it is rejected with
  `dynamic_cast to a reference is not supported without exceptions`.
- An upcast — the target class is in the source class's base chain — lowers to
  a bitcast; no object can fail it.
- Every other cast calls `__krc_dynamic_cast(object, &_ZTI<Target>)`, a
  runtime helper in `targets/kr32/runtime/libc.c`. The helper reads the
  object's vptr, takes its typeinfo, and walks the typeinfo base chain looking
  for the target; it returns the object pointer or null. Single inheritance
  means no address adjustment.
- Sema requires the operand and target to be pointers to classes, and requires
  a polymorphic source for anything that is not an upcast.

## Consequences

- `typeid` and `dynamic_cast` work end to end on the linked-VM path;
  `krc_run_rtti` requires exit 42.
- A program that never constructs a polymorphic object no longer allocates a
  table per object, and every object's metadata is shared and read-only.
- Type identity is per program, not per translation unit.
- `-fno-rtti` is not implemented: RTTI is always emitted for polymorphic
  classes, and there is no profile flag yet.
- `typeid` of a non-class type is rejected with a diagnostic rather than
  producing a `_ZTIi`-style object.
- `dynamic_cast` to a reference, cross-casts between siblings, multiple and
  virtual inheritance, and `type_info::before()` are not implemented.
- The typeinfo's first word is zero, so `type_info` is not virtual and its
  destructor is not virtual. Code that calls through it would be wrong; nothing
  in the subset does.
- No per-object metadata remains to free, so the ADR 0013 note "does not free
  it" is obsolete.

## Alternatives considered

- Keep the per-object table and put the typeinfo in slot 0: rejected because
  two objects of one class would then report different `type_info` addresses,
  which breaks type equality, and it keeps one allocation per object.
- Emit a full Itanium `__class_type_info` hierarchy with offset-to-top and
  virtual-base tables: rejected as more layout than single inheritance needs;
  the fourth word carries the base chain and the ADR records the limit.
- Lower `typeid` to a runtime call instead of a static object: rejected because
  the answer is known at compile time for a non-polymorphic operand and needs
  no runtime state.
- Give the runtime a type registry keyed by name: rejected because it would
  make type identity depend on initialization order and duplicate the
  linker's symbol merging.
