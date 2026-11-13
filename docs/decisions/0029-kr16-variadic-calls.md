# 0029 — KR16 variadic calls

- Status: accepted
- Date: 2026-11-13
- Decision: freezes how KR16 passes and reads a variadic argument list
- Extends: [ADR 0016](0016-kr16-scalar-abi.md) and [ADR 0028](0028-kr16-stack-arguments.md)

## Context

ADR 0028 gave KR16 a rule for arguments beyond the third, but a variadic *call*
still has no convention: the callee cannot see the register arguments once the
body starts, so a `va_list` that walks memory needs one contiguous area. The
backend also rejects the `vastart` opcode outright, which is why `printf` cannot
exist on KR16 — the last blocker after ADR 0028.

`va_arg` is a macro in `<stdarg.h>`, not a builtin: `(*(type *)(((ap) += 8) - 8))`
for KR32's eight-byte slots. KR16 slots are two bytes, so the header has to
differ as well.

## Decision

- A call whose callee type is variadic pushes **every** argument on the stack,
  last first, one two-byte slot each, and leaves `R1`–`R3` unused. The first
  argument therefore sits at the lowest address and the rest follow in
  declaration order.
- The callee releases nothing; the caller releases the whole area with
  `LEA SP, [SP + 2 * count]` after `CALL`, as for a non-variadic stack
  argument.
- In a variadic function's prologue, **every** parameter is read from the stack
  area: parameter `i` is at `[R4 + 4 + 2 * i]`. This is the same address the
  register parameters would otherwise occupy, so one rule covers fixed and
  variable arguments.
- `va_start` computes `R4 + 4 + 2 * N`, where `N` is the number of named
  parameters of the function being compiled — the first variable argument.
- `va_list` is `char *` on both targets; `<stdarg.h>` advances by two bytes per
  argument on KR16 and by eight on KR32, selected by `__kr16__`.
- Arguments are passed in their slot form, which is already canonical: a `char`
  load zero-extends before it is stored, so a two-byte slot holds the promoted
  value. `char` is unsigned on IP16.

## Consequences

- A variadic function works on KR16: `va_start`/`va_arg` walk the caller's
  stack area, and a five-argument variadic helper returns the value its
  arithmetic produces.
- Fixed arguments in a variadic call cost a stack slot each instead of a
  register. That is the price of one contiguous area; `printf`-style calls
  spend most of their time formatting, not passing.
- A non-variadic call keeps ADR 0028's register rule, so the common path is
  unchanged.
- `printf` still needs its implementation ported to `targets/kr16/runtime`,
  but nothing in the calling convention blocks it now.
- `va_arg` reads two bytes; `long long` and floating-point arguments do not
  exist on this profile, so no slot is wider than two bytes.

## Alternatives considered

- Spill `R1`–`R3` into a register save area and make `va_arg` jump from there to
  the stack area: rejected because the transition needs a strategy label in the
  `va_list` and a per-argument decision, for no benefit on a 16-bit target whose
  `printf` arguments are all one slot wide.
- Keep register passing for the named parameters and have `va_start` point at
  the first *stack* argument: rejected because the variable arguments then begin
  mid-register-bank when the function has fewer than three named parameters, and
  `va_arg` cannot see `R1`–`R3` after the body starts.
- A memory `va_list` struct with a register index and a stack pointer: rejected
  as more machinery than the profile needs.
