# KR16 ABI (IP16 scalar subset)

**Status:** Draft. The scalar argument/return/frame rules are frozen by
[ADR 0016](decisions/0016-kr16-scalar-abi.md); aggregates, varargs, floating
point, and more than three arguments remain open. The object and link path is
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
| other registers | caller-saved scratch |

`SP` is full-descending. The stack top is `0xF000` and the default stack limit
is `0xE000`. All slots are two bytes.

## Calling convention

1. The caller evaluates arguments and places the first three in `R1`, `R2`,
   `R3`.
2. A direct call loads the callee address with `MOV R0, #callee` and executes
   `CALL R0`.
3. An indirect call loads the function pointer into `R0` after its arguments
   and executes `CALL R0`.
4. The callee saves the incoming frame pointer:
   `PUSH R4; MOV R4, SP; LEA SP, [R4 - frame_size]`.
5. Each parameter is stored into a two-byte frame slot in the prologue.
6. The result is returned in `R0`.
7. The epilogue restores `MOV SP, R4; POP R4; RET`.

All registers except `R4` are caller-saved. The compiler currently allocates
every SSA value to a frame slot rather than to a register.

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
prologue/epilogue restoration.

## Function pointers

Function values lower to their 16-bit address. The compiler emits
`MOV R0, #function` and calls through `CALL R0`. Indirect calls work for
functions defined in the same image or resolved by the linker.

## Globals, strings, and the runtime

- Globals with constant integer or byte-array initializers are emitted in the
  data stream. The assembler currently folds `.byte` data into `.text`.
- A global pointer initializer is materialized by the module's global
  initializer function.
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

- Aggregate parameters and returns.
- Variadic functions.
- Floating-point arguments and results.
- More than three arguments.
- Callee-saved register allocation.
- `.data`/`.rodata` section splitting and KEX symbols/debug records.
- A full C++ ABI for KR16.

## Diagnostics

The backend rejects unsupported features with a target-specific diagnostic
rather than emitting a wrong calling sequence. Known rejected cases include
more than three arguments, aggregate parameters or returns, variadic calls, and
floating-point values.
