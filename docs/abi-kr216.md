# KR216 ABI

**Status:** Accepted 2026-11-22, frozen at the M3 close; ADR 0056 adds the 64-bit
argument and result slots. The data model, register roles, calling convention, frame
layout and aggregate rules are fixed by `../../kr216/kr216-plan.md` v0.11
(sections 7, 11, 12) and recorded in
[ADR 0053](decisions/0053-kr216-target-profile.md), accepted 2026-11-21. The
appendix freezes at M3. ADR 0055 fixes the 16-bit exception descriptor and the
landing-pad entry state, and the kr216 profile reports `has_eh`.

## Data model

| Type | Size | Alignment | Notes |
|---|---:|---:|---|
| `char`, `signed char`, `unsigned char` | 1 | 1 | plain `char` is signed; no `__CHAR_UNSIGNED__` |
| `short` | 2 | 2 | |
| `int` | 2 | 2 | `INT_MAX` is 32767 |
| `long` | 4 | 4 | two words; 32-bit values are register pairs |
| `long long` | 8 | 8 | 64-bit division is a runtime helper |
| pointer, reference, member pointer | 2 | 2 | 64 KiB address space |
| `float` | 4 | 4 | stored as binary32, strict binary32 arithmetic |
| `double` | 8 | 8 | binary64 |
| `long double` | 8 | 8 | alias for `double` |
| `size_t`, `ptrdiff_t` | 2 | 2 | 16-bit; container sizes are capped at 65535 |
| `intmax_t` | 8 | 8 | follows `long long` |
| `wchar_t` | 2 | 2 | |
| `char16_t` / `char32_t` | 2 / 4 | 2 / 4 | |
| record | sum of fields, rounded to its widest field's alignment | widest field | no packing beyond the declared storage units |
| array | element size × count | element alignment | |

A scalar's alignment is `min(size, 8)` in Sema and IRGen, and a record's alignment
is its widest field's. The backend's frame planner rounds a slot to 4 or 8 bytes.

The model is `DataModel::Ip16Long32` — a new profile value, not KR16's `Ip16`,
whose `long` is 16 bits and which has no `long long`. The target descriptor is
the normative source for these values; the compiler derives `sizeof`,
`_Alignof`, `INT_MAX` and `PTRDIFF_MAX` from it.

`float` arithmetic is strict binary32 (`FLT_EVAL_METHOD = 0`). A value is
widened to binary64 on load, the operation runs in the binary64 datapath, and
`FRND` rounds the result back to binary32 precision before it is used. This
costs one instruction per floating operation and needs no second datapath.

## Registers

| Register | Role |
|---|---|
| `R0` | constant 0; reads as 0, writes are discarded |
| `R1` | constant 1; reads as 1, writes are discarded |
| `R2` | argument 0; return value (low word) |
| `R3` | argument 1; return value (high word for 32-bit results) |
| `R4` | argument 2 |
| `R5` | argument 3 |
| `R6` | scratch; aggregate-return destination, loaded just before `CALL` |
| `R7` | scratch; indirect-call target, and the `SYS` call number |
| `R8` | callee-saved general register |
| `R9` | callee-saved general register |
| `R10` | scratch; `this_register` is unset, so C++ `this` travels as argument 0 |
| `R11` | flags: Z, N, C, V, I (interrupt enable), T (trace) |
| `R12` | exception frame pointer (`EFP`) for hardware traps; 0 when none is active |
| `R13` | frame pointer (`FP`) |
| `R14` | program counter (`PC`) |
| `R15` | stack pointer (`SP`), full-descending and even |

Nine registers are allocatable: R2–R10. A member function's `this` is its first
integer argument, in R2, so no register is reserved for it; a member function that
calls another member and still needs its own `this` keeps it in R8/R9 or a frame
slot.

The exception frame pointer is the hardware trap chain and has nothing to do
with C++ exceptions. On a trap the CPU pushes `{previous EFP, faulting PC,
flags}`; FP registers are not saved, so a trap handler that uses floating point
saves F0–F3 itself.

## Calling convention

1. The caller passes `this` as argument 0, in R2. No register is set aside for it;
   `this_register` is unset.
2. Integer and pointer arguments go in R2, R3, R4, R5 in order. An argument
   that does not fit is pushed, and the pushed area is ordered so that argument
   4 sits at `FP + 4` and each later argument at the next higher address. A
   32-bit value occupies two slots and a 64-bit value four slots; a 64-bit value
   that does not fit is pushed as an eight-byte little-endian area (ADR 0056).
3. Floating-point arguments go in F0 and F1. A further floating-point argument
   is pushed in an eight-byte slot; the two argument files are independent, so a
   function may take four integers and two doubles without conflict.
4. A direct call is `CALL addr16`; an indirect call loads the target into R7 and
   executes the one-byte `CALL R7`.
5. The callee's prologue is `ENTER #frame, #mask` — four bytes when the frame is
   at most 255 bytes, five otherwise — and its epilogue is `LEAVE #mask`
   followed by `RET`. A leaf function with no locals and no saved registers uses
   `push fp; mov fp, sp` and `pop fp; ret` instead.
6. The result is returned in R2 for 16-bit scalars and pointers, in R2:R3 for
   32-bit values, in R2–R5 for 64-bit values (ADR 0056), in F0 for floating
   point, and for a memory-class aggregate through the destination the caller
   loaded into R6.
7. Caller-saved: R2–R7, R10 and F0–F3. Callee-saved: R8, R9, FP and SP. Flags
   and EFP are not preserved.

FP is mandatory in every function: there is no omit-frame-pointer profile,
because the unwinder and the debugger both read `[FP]`.

## Frame layout

```text
        higher addresses
   +----------------------+
   | caller's frame       |
   +----------------------+
   | arg 4                |  <- FP + 4      first stack argument
   | arg 5                |  <- FP + 6
   +----------------------+
   | return address       |  <- FP + 2      pushed by CALL
   +----------------------+
   | saved caller FP      |  <- FP
   +----------------------+
   | saved R8, R9         |  (ascending push order, so R9 is below R8)
   | locals, spills       |
   | outgoing arguments   |
   +----------------------+  <- SP
        lower addresses
```

`ENTER` saves FP, points FP at the saved slot, pushes the mask and subtracts the
frame size. `LEAVE` restores SP from FP — which is what makes `alloca` and
variable-length arrays unwindable — pops the masked registers and the saved FP,
and leaves the return address on top of stack for `RET` or for a tail-call
`JMP`. The mask is the same immediate in both instructions; bits 13, 14 and 15
are illegal in it, the frame size is even, and the assembler rejects an odd one.
Locals are at negative offsets from FP.

## Aggregate parameters and returns

`aggregate_rules` is `MemoryOnly`: every aggregate is memory class whatever its
size.

A by-value aggregate argument is passed as a hidden pointer to a caller-owned
copy, and the pointer consumes one integer argument word — R2–R5 in order, or a
stack slot past the fourth argument. The caller builds the copy; the callee
copies the object into its own frame on entry, so a write to the parameter leaves
the caller's object untouched.

An aggregate return destination is loaded into R6 by the caller just before the
`CALL`, so R2–R5 stay argument registers. The callee copies the result to that
address in two-byte words. R6 is an ordinary caller-saved scratch at all other
times. Register returns for small aggregates would need an ADR 0046 extension
and are out of scope.

## Variadic calls

A variadic or unprototyped call passes every argument on the stack, including
floating point, each in its natural size: two bytes for integers, pointers and
the hidden aggregate pointer, eight bytes for a double. The callee reads the
first stack argument at `FP + 4`, and `va_start` yields a pointer to it;
`<stdarg.h>` advances by the size of the type being read. The caller keeps SP
two-byte aligned.

## Function pointers and virtual calls

A function value is its 16-bit address, and an indirect call loads it into R7.
A virtual call is `ld r7,[r2]`; `ld r7,[r7+2k]`; `call r7` — five bytes for
slot 0 and six when the slot offset needs a displacement byte.

## Vtables, typeinfo and RTTI

The layout is ADR 0025's, scaled to two-byte words:

| Object | Layout |
|---|---|
| vptr | first word of a polymorphic object; 2 bytes |
| `_ZTV<C>` | weak global: `[typeinfo address, virtual slot 0, virtual slot 1, ...]` |
| vptr value | `_ZTV<C> + 2`, so slot *k* is `vptr + 2k` and the typeinfo is at `vptr - 2` |
| `_ZTI<C>` | weak global: `[0, name address, flags, base typeinfo address]`, four two-byte words |
| `_ZTS<C>` | NUL-terminated class name |

`typeid` is a load from `[vptr - 2]` for a polymorphic operand, and the `_ZTI`
address otherwise. `dynamic_cast` calls `__krc_dynamic_cast(object, &_ZTI<T>)`,
which walks the thrown typeinfo's base chain; single inheritance means no address
adjustment. RTTI is always emitted for polymorphic classes because the compiler
has no `-fno-rtti`.

Static initialization walks `.init_array`; destructors register through
`__cxa_atexit`. A function-local static uses a one-byte guard and
`__cxa_guard_acquire`/`__cxa_guard_release`.

## Floating point

F0–F3 are binary64 registers, 64 bits each, all caller-saved. `FLD`/`FST` move
eight bytes; `FLDS`/`FSTS` move four and convert between binary32 and binary64;
`FPUSH`/`FPOP` move eight bytes through the stack. Arithmetic follows IEEE-754
binary64 with round-to-nearest-even and never traps — overflow gives infinity,
an invalid operation a quiet NaN, division by zero infinity. `FSQRT` is
correctly rounded. The one trap is a NaN or out-of-range float↔integer
conversion, which C++ leaves undefined and the machine makes visible.

## Globals, startup and the runtime

- `crt0` is linked first at `0x0200`: it sets SP, zeroes `.bss`, walks
  `.init_array`, calls `main(argc, argv)` and then `exit(R2)`. `exit` runs the
  `__cxa_atexit` handlers and calls `SYS exit`.
- The heap starts at the linker-defined `_end` and grows up over a first-fit
  free list; `sbrk` fails when it would come within a 256-byte guard band of SP.
- `SYS` is a one-byte hypercall, not an architectural trap: the call number is
  in R7, arguments are in R2–R5, the result is in R2, and R7 is not preserved.
  The initial calls are `exit`, `write`, `read`, `time` and `debug_write`;
  `open`/`close`/`seek` arrive with a filesystem.

Default memory map (the linker may place sections elsewhere):

| Range | Use |
|---|---|
| `0x0000`–`0x00FF` | null guard; any access traps |
| `0x0100`–`0x011F` | reset vector and 15 exception vectors |
| `0x0120`–`0x01FF` | boot information, runtime data, syscall trampoline |
| `0x0200`–`0xDFFF` | program image, then the heap |
| `0xE000`–`0xEFFF` | stack, growing down from `0xF000` |
| `0xF000`–`0xFEFF` | available to the image; not part of the default stack |
| `0xFF00`–`0xFF7F` | memory-mapped I/O: console `out`/`in`/`status`, timer, system |
| `0xFF80`–`0xFFFF` | reserved; traps |

## Object, relocation and image rules

- KRO target ID 4, little-endian, with 64-bit canonical header and relocation
  fields and 16-bit target addresses.
- Sections: `.text`, `.rodata`, `.data`, `.bss`, `.init_array`, `.fini_array`
  and `.cpp_except` for exception descriptors.
- Relocations are the common absolute and PC-relative families plus branch
  relaxation between `CALL rel9` and `CALL addr16`.
- The image is a KRCompiler container: magic, target and ABI version, sections,
  entry, image base and the embedded KDBG payload. KDBG also uses target ID 4
  and travels in the image; `krld --debug-file` may write an optional sidecar
  instead.
- The debugger's frame record is `frame = 0` with the return PC at `FP + 2` and
  locals at negative offsets, which is what `ENTER`/`LEAVE` produce.

## Deferred

- Multiple inheritance, virtual bases, and `dynamic_cast` between sibling types.
- RTTI across library boundaries and `-fno-rtti`.
- Threads, atomics and TLS.

## Diagnostics

The backend exists and the kr216 suite exercises it, so the diagnostics name the
feature and the target rather than a missing code path.
