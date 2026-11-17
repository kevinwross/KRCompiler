# KR16 ABI (IP16 scalar subset)

**Status:** Draft. The scalar argument/return/frame rules are frozen by
[ADR 0016](decisions/0016-kr16-scalar-abi.md), stack arguments by
[ADR 0028](decisions/0028-kr16-stack-arguments.md), and variadic calls by
[ADR 0029](decisions/0029-kr16-variadic-calls.md); aggregates and floating point
remain open. The object and link path is
defined by [ADR 0019](decisions/0019-kr16-object-linker.md).

## Data model

| Type | Size | Alignment | Notes |
|---|---:|---:|---|
| `char` | 1 | 1 | unsigned |
| `short` | 2 | 2 | |
| `int` | 2 | 2 | |
| `long` | 2 | 2 | |
| pointer | 2 | 2 | 16-bit address space |
| `long long` | — | — | not supported on this profile |
| `float`/`double` | — | — | not supported on this profile |
| record | sum of fields | 2 | no bit-field packing beyond the declared storage units |
| array | element size × count | element alignment | |

The target descriptor is the normative source for these values. The compiler
derives `sizeof`, `_Alignof`, `INT_MAX`, and `PTRDIFF_MAX` from it.

## Registers

| Register | Role |
|---|---|
| `R0` | return value; scratch for indirect-call targets and expression temporaries |
| `R1`–`R3` | first three scalar arguments |
| `R4` | frame pointer |
| `R5` | stack pointer (`SP`) |
| `R6` | status register (`SR`) — **not** scratch; writing it traps |
| `R7` | program counter (`PC`) |

There is **no free general-purpose register**: `R0`–`R5` carry the roles above and `R6`/`R7`
are the status register and the program counter. A value that must survive a call lives in a
frame slot, not in a register. The assembler accepts `R6` and `R7` as register names, so a
mistake here assembles cleanly and traps at run time; the disassembler prints them as `SR`
and `PC`, which is how such a mistake is found.

`SP` is full-descending. The stack top is `0xF000` and the default stack limit
is `0xE000`. All slots are two bytes.

## Calling convention

1. The caller evaluates arguments and places the first three in `R1`, `R2`,
   `R3`. Further arguments are pushed last first, one two-byte slot each, so
   the fourth argument ends at the lowest address and the rest follow in
   declaration order; the caller releases the pushed area with
   `LEA SP, [SP + 2 * count]` immediately after `CALL`.
2. A direct call loads the callee address with `MOV R0, #callee` and executes
   `CALL R0`.
3. An indirect call loads the function pointer into `R0` after its arguments
   and executes `CALL R0`.
4. The callee saves the incoming frame pointer:
   `PUSH R4; MOV R4, SP; LEA SP, [R4 - frame_size]`.
5. Each parameter is stored into a two-byte frame slot in the prologue: the
   first three from `R1`-`R3`, and argument `n >= 4` from
   `[R4 + 4 + 2 * (n - 4)]`.
6. The result is returned in `R0`.
7. The epilogue restores `MOV SP, R4; POP R4; RET`.

Frame offsets are two-byte aligned. A scalar SSA value occupies one two-byte
slot; an array or record `alloca` reserves its full type size, rounded to two
bytes, so aggregate locals cannot overlap neighboring slots.

All registers except `R4` are caller-saved. The compiler currently allocates
every SSA value to a frame slot rather than to a register.

## Aggregate parameters

A record passed by value is passed **by hidden pointer**, because the target
descriptor's `aggregate_rules` says a KR16 aggregate is memory class rather than
because a size threshold decided it. The pointer occupies one argument slot by the
rule above — `R1`–`R3` in order, or the stack past the third argument — so a
three-byte record and a pointer cost the same.

Two consequences follow, and both are visible in the emitted code for
`tests/fixtures/c/small_aggregate_argument.cpp`:

1. **The caller owns a copy.** It builds the copy from the argument's address, one
   store per field, so a record wider than a machine word keeps all of its bytes. A
   copy made by storing the loaded record instead moves a single word — two bytes on
   KR16 — and silently drops the tail.
2. **The callee copies on entry.** `sum` stores the incoming `R1` and then writes
   each field of the pointed-to record into its own frame slot (`[SP+0]` for a
   three-byte record), so a write to the parameter changes only the callee's copy.
   The caller's object is untouched.

An aggregate **return** passes its destination as a hidden stack argument placed after every
declared argument ([ADR 0048](decisions/0048-kr16-aggregate-return-destination.md)). The caller
pushes the destination address first, so it lands at the highest address; the callee reads it at
`[R4 + 4 + 2 * (parameters beyond the third)]`, keeps it in a frame slot, and copies the record
to it in two-byte words. `make_unique` therefore works on KR16 as it does on KR32. `make_tuple`
is still absent, but for a different reason: `<tuple>` documents no by-value factory.

## Frame layout

```text
higher addresses
    caller frame
    argument registers at call
    [return address]        <- pushed by CALL
    [saved R4]              <- PUSH R4
    R4 -> saved R4
    [frame slot 0]          <- SP + 0
    [frame slot 1]          <- SP + 2
    ...
    SP -> lowest slot
lower addresses
```

Frame slots are addressed relative to the current `SP`; `R4` is used only for
prologue/epilogue restoration. Scalar slots are two bytes; aggregate/array
allocations reserve their full size.

## Variadic calls

A call whose callee type is variadic pushes every argument, named and variable,
last first in two-byte slots, and leaves `R1`-`R3` unused. The caller
releases the whole area after `CALL`. The callee reads parameter `i` from
`[R4 + 4 + 2 * i]`, and `va_start` yields `[R4 + 4 + 2 * N]` for a
function with `N` named parameters (
[ADR 0029](decisions/0029-kr16-variadic-calls.md)). `va_list` is `char *` and
`<stdarg.h>` advances two bytes per argument under `__kr16__`. A non-variadic
call keeps the register rule above.

## Function pointers

Function values lower to their 16-bit address. The compiler emits
`MOV R0, #function` and calls through `CALL R0`. Indirect calls work for
functions defined in the same image or resolved by the linker.

## Globals, strings, and the runtime

- Globals with constant integer or byte-array initializers are emitted in the
  data stream. The assembler currently folds `.byte` data into `.text`.
- A global pointer initializer is materialized by the module's global
  initializer function.
- The linker defines `__heap_start` (the first byte past the image), `__heap_end` and `__stack_limit` (`0xE000`), and `__stack_top` (`0xF000`). The runtime heap is a first-fit free list over `__heap_start`..`__heap_end`: each block carries its payload size, a forward link, and a free flag, `free` merges with a free neighbour in either direction, and `malloc` splits a block when the remainder can hold a header and one aligned pair.
- `puts` and `_start` live in `targets/kr16/runtime/crt0.s`. The startup object
  calls `main` through `MOV R0, #main; CALL R0`.
- `krc -c --target=kr16` keeps the single-unit `.kex` path. `--emit-object`
  plus `krld` is the relocatable path.

## Object and relocation rules

- `MOV Rd, #label` and `LEA Rd, [label]` emit a 16-bit absolute relocation at
  the immediate word. This applies to internal and external labels because
  linking shifts object text.
- `.extern` labels become undefined KRO symbols and relocations.
- `krld` sets the default KR16 base to `0x0100`; the executable's load address
  and entry point are independent.
- KRO v1 has no KR16 PC-relative relocation; calls use the register sequence
  above instead.

## Deferred

- Floating-point arguments and results.
- Callee-saved register allocation.
- `.data`/`.rodata` section splitting and KEX symbols/debug records.
- A full C++ ABI for KR16.

## Diagnostics

The backend rejects unsupported features with a target-specific diagnostic
rather than emitting a wrong calling sequence. Known rejected cases include
aggregate parameters or returns and floating-point values.
