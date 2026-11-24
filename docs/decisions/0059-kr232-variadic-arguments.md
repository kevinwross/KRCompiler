# 0059 — Reading variable arguments on the register targets

- Status: accepted (2026-11-23)
- Date: 2026-11-23
- Decision: adds `__builtin_va_start`, `__builtin_va_arg` and `__builtin_va_end` to the C front end, gives `va_list` one shape, and fixes the single IR operation the backend needs, so a guest function declared with `...` can be written
- Scope: kr232 first, and the front end and IR change is target-neutral. The ABI is **not** reopened: ADR 0057 §"Variadic calls" already fixes where the arguments are.

## Context

A guest `printf` works: the runtime owns the calling convention, so `libc.c`'s own implementation reads its arguments from the stack without the compiler knowing they are variable. A program that declares *its own* variadic function cannot be compiled at all, because the compiler has no `va_list`, no `va_start` and no `va_arg`. That is the last runtime-facing feature gap the kr232 plan names, and `krc_kr232_run_varargs` passes today only because it exercises the runtime's `printf`, not the builtins.

The ABI question this usually raises is already answered. ADR 0057 §"Variadic calls" says a variadic callee receives **every** argument in an **eight-byte stack slot**, the first at `[FP+8]`, that the named argument at index *i* lives at `[FP+8+8i]`, that `va_start` answers `[FP+8+8·named]`, and that `int` occupies the low four bytes of its slot while `double` occupies all eight. The backend already implements both halves: the callee spills from those slots (convention 002, `kr232_backend.cpp`) and a variadic call pushes every argument regardless of the register limit (same file). So what is missing is not a convention but an expression: the compiler has no way to say "the address of the first variable argument", and no `va_list` type for a program to name.

Constraint from `AGENTS.local.md:17`: the ABI moves only by ADR. This ADR does not move it.

## Decision

### `va_list`

`va_list` is a **four-byte pointer** to the current eight-byte slot. It is a typedef in the target sysroot's `<stdarg.h>`, not a built-in type, and it is a pointer rather than an array so that copying it with `=` copies the position, which C permits and which keeps the lowering to one load. `va_arg` advances the pointer; `va_end` does nothing.

### The three builtins

| Builtin | Meaning |
|---|---|
| `__builtin_va_start(ap, last_named)` | `ap = <address of the first variable argument slot>` |
| `__builtin_va_arg(ap, T)` | read `T` from `*ap` at that type's width, then `ap += 8` |
| `__builtin_va_end(ap)` | no-op |

Widths follow ADR 0057: an `int` or a pointer reads four bytes from the low half of the slot, a `double` or `long double` reads all eight, and `char`/`short` are promoted to `int` by the normal default argument promotions before they ever reach a slot. kr232 has no 64-bit integer (ADR 0057 profile), so no `va_arg` width is unrepresentable.

`va_start` needs the number of *named* parameters, which the front end knows from the enclosing function's type; it never needs the caller's argument count.

### The one IR operation

The address `FP + 8 + 8·named` is not expressible in today's IR: the incoming slots of a variadic function are not `alloca`s, so `getelementptr` has nothing to index. The decision is to add **one** IR operation, `alloca`-shaped in that it yields a pointer and takes a constant:

```
%ap = builtin.va_start i32 <named_count>   ; pointer to the first variable slot
```

Everything else lowers to operations that already exist: `__builtin_va_arg` becomes a load at the pointer's pointee width followed by `add` of 8, and `__builtin_va_end` disappears. No new opcode is added for `va_arg`, because a load and an add are already expressible and a second opcode would be a second lowering to keep correct.

The backend lowers `builtin.va_start` to `mov rD, fp` / `add rD, #(8 + 8·named)`, which is the shape the existing prologue already uses for incoming slots.

### Front end

`<stdarg.h>` in the sysroot defines `va_list`, `va_start`, `va_arg` and `va_end` in terms of the builtins, so a guest program is ordinary C. The runtime's `printf` keeps its own implementation over `syscall6.s` and changes in no way.

## Consequences

- A guest variadic function becomes writable, which closes the last runtime-facing compiler gap in the kr232 plan.
- The cost is small and bounded: three builtins, one IR operation, one backend lowering, one sysroot header, one test. Nothing in the runtime, the linker or the image format moves.
- `va_list` as a pointer means a function that copies one and reads through both sees one position, which is the common interpretation of C's "unspecified if the pointer is advanced".
- The IR operation is target-neutral but only the register targets have a stack slot per argument. If another target ever needs a register save area, it adds its own lowering of the same operation rather than a second front-end path.
- Nothing enforces the caller's contract: a program that calls a variadic function with more arguments than its own code reads gets whatever is above the last slot. That is C's own position, not a new hazard.

## Alternatives rejected

**A register save area**, the usual System V answer. The kr232 ABI already passes every variadic argument on the stack, so a save area would be a second mechanism for data that is already addressable, and it would put a prologue burden on every variadic function.

**Lowering the builtins entirely in the front end** by taking the address of the last named parameter. That works only when the last named parameter happens to be the next slot, it bakes the frame layout into `c_irgen` for every target, and it fails as soon as the named arguments are not contiguous — which is exactly what the eight-byte slot convention makes them. A single IR operation says what is meant instead of relying on a layout coincidence.

**A `va_list` that is an array, as several ABIs have it.** It would make `va_list` decay and complicate every lowering for no gain on a target whose slots are fixed-size.

## Acceptance

1. A guest function `static int sum(int count, ...)` reading `count` integers with `va_arg`, and a second reading one `int` and one `double`, produce the values asserted by the test.
2. `krc_kr232_run_varargs` passes **unchanged**: it exercises the runtime's `printf`, which must be untouched by this.
3. The whole compiler suite passes serially, and the kr232 rows pass on both hosts.
4. `docs/abi-kr232.md` gains the `va_list` shape and a pointer to ADR 0057 for where the arguments are.
