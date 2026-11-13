# 0026 — Exception descriptors, landing pads, and the unwinder

- Status: accepted
- Date: 2026-11-13
- Decision: freezes the `.cpp_except` descriptor encoding, the unwinder walk, and
  the runtime API for `throw`/`try`/`catch`
- Supersedes: none (implements plan §10.4 and the sibling ABI's outline)

## Context

Plan §10.4 and §14.4 require table-driven exceptions with a personality routine
and an unwinder; `docs/abi.md` already states the contract in outline — a
`.cpp_except` LSDA per function, a walk over the saved `R7` chain with the
return address at `R7 + frame + 4`, landing pads that receive the exception in
`R0` and a selector in `R1`, and `__cxa_*` runtime entry points. Both documents
leave the record encoding to an ADR before v1.1.

Two facts constrain the design. First, a frame cannot be unwound from `R7`
alone: the saved `R7` sits at `[R7 + frame_size]`, and `frame_size` is not
recoverable from the frame. Every function therefore needs its frame size in a
table keyed by program counter. Second, this backend emits no frame descriptor
anywhere else, so the table is the only place the unwinder can learn the frame
size and the landing pad for a call site.

There is also a language obstacle: destructor cleanup on propagation is not
implemented in this change. Shipping exceptions that silently skip destructors
would miscompile valid C++, so the compiler diagnoses the cases it cannot
handle yet instead (see Consequences).

## Decision

### Section and table

- `.cpp_except` is an ordinary allocated, read-only section of the run image,
  not a debug section. The linker merges every object's `.cpp_except` by name
  and defines `__krc_eh_start` and `__krc_eh_end` around the merged bytes.
- The section is a concatenation of per-function descriptors. **Every** function
  with a body gets one, even when it has no protected region, because the
  unwinder needs the frame size of every frame it walks.
- Descriptor layout, little-endian:

  ```text
  u32 magic       0x4845524B ("KREH")
  u32 func_start  address of the function's first instruction
  u32 func_end    one past its last instruction
  u32 frame_size  bytes the prologue subtracts from SP
  u32 region_count
  region[]        u32 start_pc, u32 end_pc, u32 landing_pad
  ```

- A region describes one call site that can unwind. `start_pc` is the address of
  the CALL instruction, `end_pc` is `start_pc + 8` (the two-word direct call the
  backend emits), and the return address pushed by CALL is `start_pc + 8`.
  Lookup is inclusive of both bounds so that return address is found.
- Regions are unordered; the runtime scans them. The descriptor scan is linear
  over the section. Both are deliberate: v1.1 has tens of functions per image,
  and a sorted index is an optimisation that does not change the encoding.

### Unwinder

- `__cxa_throw(object, typeinfo, destructor)` is a naked assembly routine: it
  must read the return address and keep `R7` intact. It stores the exception
  object and typeinfo in runtime globals, records the destructor in the
  exception header, and enters the walk.
- The walk keeps the current frame in a callee-saved register and the current
  program counter in another, and repeats:
  1. `__krc_eh_lookup(pc)` scans the table and returns the landing pad for `pc`,
     or zero; it also publishes the descriptor's frame size in
     `__krc_eh_frame_size`. A zero result with no block found also publishes a
     frame size of zero, which terminates the walk.
  2. With a pad: `SP` is restored to `frame + frame_size + 8` (past the saved
     `R7` and the return address), `R0` receives the exception object pointer,
     `R1` the typeinfo, and the unwinder jumps to the pad with `R7` still
     naming the frame.
  3. Without a pad: `frame` becomes `[frame + frame_size]` and `pc` becomes
     `[frame + frame_size + 4]` of the frame just left. A zero frame means the
     outermost frame was passed, and the runtime calls `std::terminate`
     (abort).
- The return address of a direct call is two words into the call, because the
  assembler always emits the two-word `CALL <symbol>` form.

### Landing pads and catch matching

- `landingpad` yields a `ptr<i8>` to an eight-byte frame scratch holding the
  exception object pointer at `+0` and its typeinfo at `+4`. The backend stores
  `R0`/`R1` there when the pad is entered; the frame gains eight bytes when a
  function has a landing pad. This keeps `landingpad` a single SSA value without
  adding an aggregate-extraction instruction to the backend.
- A catch clause tests `__krc_eh_matches(thrown_typeinfo, &_ZTI<T>)`, which
  walks the thrown typeinfo's base chain exactly as `dynamic_cast` does, so a
  base-class handler catches a derived exception.
- `resume` calls `_Unwind_Resume(exception)`, which re-enters the walk with the
  same exception so an outer handler can run. A rethrow (`throw;`) calls
  `__cxa_rethrow`, which does the same with the current exception.
- Handler code binds `catch (T& e)` to the exception object pointer; single
  inheritance means no address adjustment. `catch (T e)` copies the object.

### Exception object

- `__cxa_exception` is a sixteen-byte header immediately below the thrown
  object: typeinfo at `-16`, destructor at `-12`, the previous exception being
  handled at `-8`, and a spare word at `-4`.
- `__cxa_allocate_exception(size)` allocates `size + 16` and returns the object
  pointer; `__cxa_begin_catch(object)` publishes it as the current exception and
  returns the object; `__cxa_end_catch()` frees it and restores the previous
  one; `__cxa_free_exception(object)` frees without publishing.
- The current exception is a single global in v1.1. The VM is single-threaded
  for the supported subset; per-thread exception state arrives with dynamic TLS.

## Consequences

- Exceptions work across functions: a throw in a callee reaches a handler in a
  caller, and an unmatched exception aborts through a zero frame.
- **Destructor cleanup on propagation is not implemented.** The compiler rejects
  a call or `throw` that is inside a protected region while an automatic object
  whose destructor must run is live, with
  `exception cleanup of automatic objects is not implemented yet`. Rejecting is
  deliberate: silently skipping a destructor would miscompile valid C++. The
  next change adds cleanup regions and removes the diagnostic.
- Only the throwing function's own `try` regions can catch; a function without a
  protected region still gets a descriptor, so it can be walked through.
- Catch clauses are matched in the landing pad rather than by a personality
  routine, so there is no `__kr32_personality_v0` yet. The descriptor's
  per-region landing pad is the only selector handed to the runtime. A
  personality routine becomes worthwhile when cleanup regions and call-site
  tables grow.
- `noexcept`, exception specifications, and `std::exception` hierarchies are
  outside this change. `throw;` outside a handler, and `throw` of a pointer or
  scalar, are rejected with a diagnostic.
- The descriptor table is walked linearly and is not sorted; a large program
  pays one bounds check per descriptor per frame. Deterministic either way.
- `__cxa_end_catch` frees the exception object immediately. A handler that
  copies the object is fine; one that keeps a reference past the handler is
  undefined, as the standard says.

## Alternatives considered

- Recover the frame size from the frame itself (a frame-link word): rejected
  because the ABI has no fixed slot — the outgoing argument area occupies
  `[R7 + 0]` — and a slot below the frame is overwritten by deeper calls.
- Keep a handler stack maintained by `try` (setjmp/longjmp style): rejected
  because a throw from a callee would jump into a frame that has already
  returned, and cleanup could never be correct.
- Put the table in `.data` and find it by a magic scan: rejected because magic
  scanning can false-positive on program data and makes the runtime's bounds
  depend on content.
- Have the personality routine decide the handler: rejected for this slice
  because it duplicates the catch matching the pad already performs, and the
  selector is only needed once cleanup regions exist.
