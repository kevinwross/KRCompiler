# 0027 — Destructor cleanup on propagation

- Status: accepted
- Date: 2026-11-13
- Decision: adds cleanup landing pads and chained resumption, removing the
  diagnostic ADR 0026 used to reject the unsupported case
- Supersedes: ADR 0026's "destructor cleanup is not implemented" consequence

## Context

ADR 0026 implements exceptions but deliberately refuses a `try` or `throw`
while an automatic object whose destructor must run is live: the descriptor had
one landing pad per call site and no way to run the destructors between the
throw and the handler, so skipping them silently would have miscompiled valid
C++. This change supplies that mechanism.

The constraints from ADR 0026 still hold. A landing pad is entered by the
unwinder with the exception object in `R0` and its typeinfo in `R1`; the pad's
block starts with `landingpad`; and `_Unwind_Resume` re-enters the walk using
the return address at the resume call site.

## Decision

- Every lexical scope that owns a destructible automatic object gets a
  **cleanup landing pad**, created on the scope's first such declaration. Calls
  emitted after that point in the scope are invokes to it; calls before it stay
  protected by the enclosing pad, which is correct because no object of the
  scope is live yet.
- The pad runs the scope's destructors in reverse construction order, then
  continues unwinding into the enclosing pad. When the enclosing pad is a catch
  pad (the `try` body's pad chains to its handler), resuming lands in the
  handler. When there is no enclosing pad, the chain ends at a `resume`, which
  leaves the frame.
- Chaining uses the same descriptor mechanism as everything else: the resume
  call site inside a cleanup pad is itself an invoke whose region names the
  enclosing pad, so the unwinder's next lookup lands there. No new opcode, no
  personality routine, and no change to the descriptor format.
- The `try` body is an ordinary scope: its pad chains to the handler pad, so
  objects declared directly in the try block are destroyed before the handler
  runs, and nested scopes chain through their own pads.
- Destructor calls on the normal exit path are protected by the *enclosing*
  pad, never by the scope's own pad: an exception thrown by a destructor during
  normal exit must not re-enter that scope's cleanup.
- IRGen tracks the scope pads in `scope_pads_`, a stack parallel to lexical
  scopes with the scope's cleanup mark and its enclosing pad. `emit_resume`
  chooses between chaining (an `_Unwind_Resume` invoke) and the IR `resume` op
  at the end of the chain.
- A pad belongs to one function. Synthetic constructors, destructors, and
  global init functions are generated while another function's scope state is
  live, so a pad whose parent block is a different function is ignored; without
  that check a generated constructor branched to a block of the caller's
  function and failed the verifier.

## Known defect

A scope holding two or more destructible objects does not compile: the scope's
cleanup pad is created at the first construction that can unwind and filled at
scope exit, so an invoke emitted for the second object unwinds into a pad that
destroys objects declared after it, and the IR verifier reports *instruction
does not dominate its use*. Three ordinary objects reproduce it without
templates:

```cpp
struct Box { int value; Box() : value(0) {} ~Box() {} };
int main() { Box a; Box b; Box c; return 0; }
```

The fix is a pad per construction point whose snapshot is the objects built
before it. A first attempt (one pad per construction, emitted eagerly) and a
second (one pad per construction, emitted at scope exit) both left a pad whose
resume invokes into a pad that does not dominate it, so the change needs the pad
chain rebuilt as an explicit stack instead of the single `enclosing` field.
## Consequences

- `try`/`throw` runs live destructors during unwinding: reverse order, nested
  scopes, guards constructed after the throw are not destroyed, and the same
  holds when the throw crosses a function boundary. `krc_run_exception_cleanup`
  requires exit 42 and records the order by folding the guard's index into a
  counter, so a wrong order fails rather than passing.
- The `exception cleanup of automatic objects is not implemented yet` diagnostic
  and its test are gone.
- A throw that leaves a function now runs that function's destructors, but only
  through pads that exist: a function with no destructible locals and no `try`
  still has no regions, and skipping it is correct because it has nothing to
  clean.
- A destructor that itself throws is not terminated as the standard requires;
  the call is protected by the enclosing pad and unwinds there. This is a
  documented deviation rather than a silent one.
- The descriptor format is unchanged, so ADR 0026's encoding stays frozen.

## Alternatives considered

- A personality routine that decides between cleanup and catch: deferred. The
  pad-side chain needs no selector and keeps the runtime smaller; a personality
  becomes worthwhile when call-site tables and filters grow.
- Run *all* of a function's cleanups at its single pad and mark them done with
  completion flags: rejected because objects alive at one call site are not
  alive at another, and flags add state for every object.
- Emit the destructor calls inline before every `throw` statement: rejected
  because it misses exceptions raised by calls, which is the common case.
