# KR232 ABI

**Status:** Draft, accepted with ADR 0057 on 2026-11-22 and frozen at the M3 close.
The data model, register roles, calling convention, frame layout and aggregate
rules are fixed by `kr232-plan.md` 0.18 (§2, §5) and `kr232-isa.md` 0.4, and
recorded in [ADR 0057](decisions/0057-kr232-target-profile.md). The container is
[ADR 0058](decisions/0058-kr232-image-container.md). Where this appendix and the
plan disagree, the plan is the authority until the freeze.

## Data model

| Type | Size | Alignment | Notes |
|---|---:|---:|---|
| `char`, `signed char`, `unsigned char` | 1 | 1 | plain `char` is signed; `char_unsigned` false |
| `short` | 2 | 2 | |
| `int` | 4 | 4 | `INT_MAX` is 2147483647 |
| `long` | 4 | 4 | 32-bit, same as `int` |
| `long long` | — | — | **does not exist**; `long_long_bits` is 0 and the front end rejects the type |
| pointer, reference, member pointer | 4 | 4 | 4 GiB address space |
| `float` | 8 | 8 | IEEE binary64; kr232 has one floating-point format |
| `double` | 8 | 8 | IEEE binary64 |
| `long double` | 8 | 8 | alias for `double` |
| `size_t`, `ptrdiff_t` | 4 | 4 | 32-bit |
| `intmax_t`, `uintmax_t` | 4 | 4 | follows from the absent 64-bit type |
| `wchar_t` | 4 | 4 | |
| `char16_t` / `char32_t` | 2 / 4 | 2 / 4 | |
| record | sum of fields, rounded to 4 | 4 | no packing beyond the declared storage units |
| array | element size × count | element alignment | |

The model is `DataModel::Ilp32` with four target values overridden:
`long_long_bits` 0, `intmax_bits` 32, `long_double_bits` 64, and `float_bits` 64. The
descriptor is the normative source; the compiler derives `sizeof`, `_Alignof`,
`INT_MAX` and `PTRDIFF_MAX` from it.

There is one floating-point format. `float` and `double` are both binary64, so a
`float` value is stored and computed as eight bytes and no widening or rounding
step exists; `FLT_MANT_DIG` is 53 for both. The ISA's `FLDS`/`FSTS` conversion
forms are unused by the 1.x compiler.

There is no 64-bit integer type on this target. `long long`, `__int64` and
anything that needs one are a diagnostic, not a pair of registers.

## Registers

| Register | Role |
|---|---|
| `R0` | constant 0; reads as 0, writes are discarded |
| `R1` | constant 1; reads as 1, writes are discarded |
| `R2` | argument 0; result |
| `R3` | argument 1 |
| `R4` | argument 2 |
| `R5` | argument 3 |
| `R6` | scratch; aggregate-return destination, loaded just before `CALL` |
| `R7` | scratch; indirect-call target, and the `SYS` call number |
| `R8` | scratch (address register); caller-saved |
| `R9` | callee-saved general register |
| `R10` | scratch; `this_register` is unset, so C++ `this` travels as argument 0 |
| `R11` | flags: Z, N, C, V, I, T, K; `K` and `I` are hardware-owned |
| `R12` | exception frame pointer (`EFP`) for hardware traps; 0 when none is active |
| `R13` | frame pointer (`FP`) |
| `R14` | program counter (`PC`) |
| `R15` | stack pointer (`SP`), full-descending, 4-byte aligned, 8 recommended |

Nine registers are allocatable: R2–R10. A member function's `this` is its first
integer argument, in R2, so no register is reserved for it; a member that calls
another member and still needs its own `this` keeps it in a frame slot, because
every allocatable register is caller-saved except R9.

K and I are not ordinary flag bits. Only trap entry sets K, only `IRET` clears
it, and only `EI`/`DI` change I; no ordinary instruction or register write
touches either (ISA §2.2, §15.29). Code generation must therefore never treat R11
as a value it can restore: saving and restoring R11 preserves K by construction,
and writing it cannot escalate privilege.

The exception frame pointer is the hardware trap chain and has nothing to do with
C++ exceptions. A trap pushes `{previous EFP, faulting PC, flags, saved SP}` — 16
bytes — and does not save F0–F3, so a handler that uses floating point saves them
itself.

## Calling convention

1. The caller passes `this` as argument 0, in R2. No register is set aside for it.
2. Integer and pointer arguments go in R2, R3, R4, R5 in order. A further
   argument is pushed, ordered so that argument 5 sits at `FP + 8`: the return
   address occupies `FP + 4` and the saved FP `FP`, and each later argument sits
   at the next higher address.
3. Floating-point arguments go in F0, F1, F2, F3 in order. A further one is
   pushed in an eight-byte slot at the next stack argument position. The two
   argument files are independent, so a function may take four integers and four
   doubles without conflict.
4. A direct call is `call rel16`, widened to `ld r8,#symbol; call r8` when the
   target is out of range. An indirect call puts the target in R8 and the return
   address in R7 (`ld r7,#back; push r7; jmp r8`); the ISA has no call through a
   register.
5. The prologue is `ENTER #frame, #mask`; the epilogue is `LEAVE #mask; RET`, and
   a tail call is `LEAVE #mask; JMP target`. A leaf with no locals and no saved
   registers may use `push fp; mov fp, sp` and `pop fp; ret`.
6. The result is in R2 for a scalar or pointer, in F0 for floating point, and for
   a memory-class aggregate through the destination the caller loaded into R6.
7. Caller-saved: R2–R8, R10 and F0–F3. Callee-saved: R9, FP and SP. Flags and
   EFP are not preserved.
8. SP is 4-byte aligned and SHOULD be 8-byte aligned at a call, so an 8-byte
   `double` pushed for a variadic call stays aligned (ISA §15.38).

FP is mandatory in every function: there is no omit-frame-pointer mode, because
the unwinder and the debugger both read `[FP]`.

## Frame layout

```text
        higher addresses
   +----------------------+
   | caller's frame       |
   +----------------------+
   | arg 5                |  <- FP + 8      first stack argument
   | arg 6                |  <- FP + 12
   +----------------------+
   | return address       |  <- FP + 4      pushed by CALL
   +----------------------+
   | saved caller FP      |  <- FP
   +----------------------+
   | saved R9             |  (push order follows the mask)
   | locals, spills       |
   | outgoing arguments   |
   +----------------------+  <- SP
        lower addresses
```

`ENTER` saves FP, points FP at the saved slot, pushes the mask and subtracts the
frame size; `LEAVE` restores SP from FP, pops the mask and the saved FP, and
leaves the return address on top of the stack for `RET` or a tail-call `JMP`. The
mask is the same immediate in both, bits 13–15 are illegal in it, and the frame
size is a multiple of 4. Locals are at negative offsets from FP, which is why the
debugger's frame record is `frame = 0` with the return PC at `FP + 4`.

## Aggregate parameters and returns

`aggregate_rules` is `MemoryOnly`: every aggregate is memory class whatever its
size, and no size threshold classifies one.

A by-value aggregate argument travels as a hidden pointer to a caller-owned copy
and consumes one integer argument word — R2–R5 in order, or a stack slot past the
fourth. The caller builds the copy; the callee copies the object into its own
frame on entry, so a write to the parameter leaves the caller's object untouched.
The copy is **size-exact**: whole 4-byte words, then a 2-byte halfword and a byte
for the tail. A three-`char` record must move three bytes, not four — the defect
ADR 0046 records for the existing backends is the reason this sentence exists.

An aggregate return destination is loaded into R6 by the caller **after**
argument evaluation, so an argument that itself returns an aggregate cannot
overwrite it, and R2–R5 stay argument registers. The callee copies the result to
that address, again size-exactly. R6 is ordinary caller-saved scratch at all
other times.

## Variadic calls

A variadic or unprototyped call passes every argument in an **eight-byte**
stack slot, whatever its type, the first at `FP + 8` and the argument at index
*i* at `FP + 8 + 8i`. The caller keeps SP 8-byte aligned, and the hidden
aggregate pointer occupies a slot like any other argument.

`va_list` is a plain `char *` at the current slot (ADR 0059), `va_start` yields
`FP + 8 + 8 x named`, and `va_arg(ap, T)` reads `T` from the low bytes of the
slot and advances the pointer by eight. An `int` therefore occupies the low four
bytes of its slot and a `double` all eight, which is why a mixed call needs no
type tag: the callee reads each argument at the width its own code names. The
definitions live in `sysroot/kr232/include/stdarg.h`.

## Function pointers and virtual calls

A function value is its 32-bit address. A virtual call is `ld r7,[r10]`;
`ld r7,[r7+4k]`; `call r7` — eight bytes for slot 0, twelve when the displacement
needs its own word.

## Vtables, typeinfo and RTTI

The layout is ADR 0025's, scaled to four-byte words:

| Object | Layout |
|---|---|
| vptr | first word of a polymorphic object; 4 bytes |
| `_ZTV<C>` | weak global: `[typeinfo address, virtual slot 0, virtual slot 1, …]` |
| vptr value | `_ZTV<C> + 4`, so slot *k* is `vptr + 4k` and the typeinfo is at `vptr - 4` |
| `_ZTI<C>` | weak global: `[0, name address, flags, base typeinfo address]`, four words |
| `_ZTS<C>` | NUL-terminated class name |

The slot at `vptr - 4` is **reserved from the start**, even though 1.x ships no
cast path. Emitting vtables without it would move every virtual slot the day RTTI
arrives, invalidating existing images and their goldens (plan §2.8, §13).

`_ZTI` is emitted because the front end has no `-fno-rtti`, and the slot holds the
class's `_ZTI`, so `typeid` on a polymorphic operand is a load from `vptr - 4`.
The full runtime, `targets/kr232/runtime/libc_full.c`, provides
`__krc_dynamic_cast` together with `malloc`, `free` and `atexit`, so a program
that uses `dynamic_cast` links and runs against it. The minimal `libc.c` runtime
does not provide the helper; a program linked against that one fails with the
symbol named.

## Floating point

F0–F3 are binary64 registers, 64 bits each, all caller-saved. `FLD`/`FST` move
eight bytes, and `FPUSH`/`FPOP` move eight bytes through the stack.
`FLDS`/`FSTS` exist for a binary32 format that the 1.x compiler does not use.
Arithmetic follows IEEE-754
binary64 with round-to-nearest-even and never traps — overflow gives infinity, an
invalid operation a quiet NaN, division by zero infinity. `FSQRT` is correctly
rounded. The one trap is a NaN or out-of-range float↔integer conversion, which C
leaves undefined and the machine makes visible as vector 3.

The F registers are not part of the hardware trap frame. A handler that uses
floating point saves F0–F3 itself, and a call does not preserve them: they are
caller-saved like R2–R7.

## Globals, startup and the runtime

- `crt0` is linked at `0x0200`, the linker's default (ISA §12). It establishes a
  frame, calls `main`, and passes the status to `SYS exit`. It does **not** set
  SP: the reset SP is already `0x08000000`, the top of kernel RAM (ISA §15.22).
- The full runtime (`libc_full.c`) provides `malloc`, `free`, `atexit` and the C++
  ABI helpers, so a program that allocates, registers an `atexit` function, or
  calls `dynamic_cast` links against it. The minimal `libc.c` provides none of
  these; a program that needs them links the full runtime.
- `SYS` is a one-byte hypercall, not an architectural trap: the call number is in
  R7, arguments in R2–R5, the result in R2, and R7 is not preserved. The initial
  calls are `exit`, `write`, `read`, `time` and `debug_write` (ISA §13); user code
  reaches the same services through `SYSCALL` once a kernel exists.

Default layout for a kernel program (the linker may place sections elsewhere
inside kernel RAM):

| Range | Use |
|---|---|
| `0x00000000`–`0x000000FF` | null page; any access traps (vector 4) |
| `0x00000100`–`0x0000013F` | reset word and the fifteen further vectors |
| `0x00000140`–`0x000001FF` | boot information |
| `0x00000200`–… | program image, then unallocated kernel RAM |
| …–`0x08000000` | stack, growing down from the top of kernel RAM |
| `0x08000000`–`0x0FFFFFFF` | user RAM; a user image is linked at `0x08000000` |

## Object, relocation and image rules

- KRO target id **5**, little-endian, 32-bit target addresses, with the frozen
  64-bit canonical header and relocation fields (ADR 0005).
- Sections: `.text`, `.rodata`, `.data`, `.bss`, `.init_array`, `.fini_array` and
  `.kdbg`, plus a 4-byte `.reset` at `0x0100` for a kernel image.
- Relocations are the common absolute and PC-relative families.
- The image is a `.kr232` container per [ADR 0058](decisions/0058-kr232-image-container.md):
  magic, container and ABI version, target id 5, sections, entry, image base and
  the embedded KDBG payload. `image_base` is 0 for a kernel image and
  `0x08000000` for a user image.
- KDBG carries target id 5 and its payload's `image_base` matches the container;
  krdbg rejects a mismatch rather than pairing a stale payload with a new image.
- The debugger's frame record is `frame = 0`, the return PC at `FP + 4`, and
  locals below FP; the adapter negates `Location::FrameOffset` because the
  record's offsets are non-negative.

## Deferred

- 64-bit integers, and every library function that would need one.
- Exceptions (`has_eh` false), and with them catch-clause type matching.
- Multiple inheritance and virtual bases.
- `.init_array` and the global-destructor path.
- Threads, atomics and TLS.
- User mode: the calling convention is identical with K = 0, but the kernel's
  syscall table does not exist until M5.

## Diagnostics

The backend exists and the kr232 suite exercises it. `long long` is rejected
because the profile has no 64-bit type, and `dynamic_cast` has a runtime
implementation: `targets/kr232/runtime/libc_full.c` provides
`__krc_dynamic_cast`.
