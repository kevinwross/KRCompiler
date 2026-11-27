# C/C++ ABI

**Status:** Draft for C++/EH; the KR32 **C ABI is frozen** by [ADR 0001](decisions/0001-kr32-c-abi-freeze.md). C++/EH ABI freeze before M5. Until the remaining sections are frozen, `plan.md` §10 is normative.
**Targets:** KR32 (ILP32), KR16 (IP16 embedded profile; scalar subset in [abi-kr16.md](abi-kr16.md)), KR216 (IP16 with a 32-bit `long`, in [abi-kr216.md](abi-kr216.md)), KR64 (placeholder, LP64).

## Purpose

This document owns the binary contract between generated code, the runtime, the linker, and the debugger: data layout, calling conventions, mangling, vtables/RTTI, exceptions, varargs, TLS, guards, and attributes.

## Data models

### KR32 — ILP32

| Type | Size | Align | Notes |
|---|---:|---:|---|
| `char` | 1 | 1 | plain `char` signed |
| `short` | 2 | 2 | |
| `int` | 4 | 4 | |
| `long` | 4 | 4 | |
| `long long` | 8 | 8 | 64-bit division is a runtime libcall |
| pointer | 4 | 4 | |
| `float` / `double` | 4 / 8 | 4 / 8 | IEEE binary32 / binary64 |
| `long double` | 8 | 8 | alias for `double` |
| `size_t` / `ptrdiff_t` | 4 | 4 | |
| `intmax_t` | 8 | 8 | |
| `wchar_t` | 4 | 4 | |

### KR16 — IP16 embedded profile

| Type | Size | Align | Notes |
|---|---:|---:|---|
| `char` | 1 | 1 | unsigned; `__CHAR_UNSIGNED__` |
| `short` / `int` / `long` | 2 / 2 / 2 | 2 | **non-conforming**; documented profile |
| `long long` | — | — | unsupported |
| pointer | 2 | 2 | |
| `float` / `double` | — | — | unsupported |
| `size_t` / `ptrdiff_t` | 2 | 2 | |
| `intmax_t` | 2 | 2 | profile-limited |
| `wchar_t` | 2 | 2 | |

The KR16 profile is intentionally non-conforming. Diagnostics that depend on it must say so.

### KR64 — placeholder

LP64: `int` 32, `long` 64, `long long` 64, pointer 64, `short` 16, `char` 8, `float` 32, `double` 64. To be confirmed with the 64-bit VM specification.

## Calling conventions

- **KR32:** inherits `../../kr32/docs/abi.md` revision 1. Integer args R0–R3, FP args F0–F7, integer return R0, FP return F0, R7 frame pointer, R11 thread pointer, 16-byte public call alignment, `sret` above 8 bytes, stack-passed varargs in 8-byte slots, `this` per the KR32 C++ ABI. A 64-bit integer such as `long long` occupies two consecutive integer argument words, low word first, and returns in R0/R1; see [ADR 0023](decisions/0023-kr32-64-bit-integers.md).
  - The memory-class return destination travels in `R4`, as revision 1 specifies, and does not consume an argument word. The caller loads it immediately before `CALL`; an indirect call keeps its target in `R5` so that the two do not collide ([ADR 0024](decisions/0024-kr32-tile-intrinsics.md)).
  - A memory-class aggregate *argument* travels as an invisible pointer to a copy the caller owns, so it consumes one integer argument word. The callee copies the pointed-to object into its own frame slot on entry, which is why a callee that modifies a parameter leaves the caller's copy untouched. A trivial aggregate of 8 bytes or less stays in integer registers; every larger aggregate is memory class ([ADR 0001](decisions/0001-kr32-c-abi-freeze.md)).
  - The transform stack pointer is per-thread, 64-byte aligned, and grows down in 64-byte entries. `crt0.s` sets it to the linker's `__stack_limit`, so tile saves occupy the 64 KiB between the program stack and the heap.
- **KR16:** scalar subset frozen by [ADR 0016](decisions/0016-kr16-scalar-abi.md) and documented in [abi-kr16.md](abi-kr16.md): integer/pointer args `R1`–`R3`, return `R0`, `R4` frame pointer, two-byte stack alignment, all other registers caller-saved. Arguments past the third are pushed and released by the caller; a variadic call pushes every argument ([ADR 0029](decisions/0029-kr16-variadic-calls.md)). A by-value **aggregate parameter** travels as an invisible pointer to a caller-owned copy that the callee copies into its own frame on entry — the descriptor's `aggregate_rules` makes a KR16 aggregate memory class rather than a size threshold guessing it. Aggregate **returns** pass their destination as a hidden stack argument ([ADR 0048](decisions/0048-kr16-aggregate-return-destination.md)); floating point remains deferred.
- **KR64:** LP64 placeholder; first six integer args R0–R5, first eight FP args F0–F7.

## Name mangling

Itanium `_Z` mangling is not implemented. A member symbol is emitted as `<Class>_<method>`, with `_dtor` for a destructor and `_op_` for an operator (`src/irgen/c_irgen_internal.h`); `extern "C"` is unmangled; `_ZTV`/`_ZTI`/`_ZTS` keep the Itanium spelling for the vtable, typeinfo, and name globals. The grammar, substitutions, builtin codes, and operator names below are the intended contract, not the current emission.

A function or class template instance is named `<name>__<tag>` for each template parameter in declaration order, where the tag is the type's source spelling: `add__double`, `doubled__char`, `first_of__int`. Pointers use `ptr_<tag>` and arrays `array_<tag>`; a class instance uses its tag, and a non-type parameter contributes its number: `Buffer__int__4` ([ADR 0033](decisions/0033-non-type-template-parameters.md)). Instances are weak definitions, so two translation units that instantiate the same type agree ([ADR 0031](decisions/0031-function-template-instantiation.md)). The all-int instance keeps the template's own symbol. A class instance is a record whose tag is that name and whose constructors and destructor are renamed to it, so Box<int> and Box<double> are two records ([ADR 0032](decisions/0032-class-template-instantiation.md)).

## Vtables, RTTI, exceptions

- Vtable pointer at object offset 0; RTTI pointer at slot -1; virtual slots in declaration order; complete/deleting destructor slots.
- `type_info`: vtable pointer, name pointer, flags.
- Vtables and typeinfo objects are weak globals, one per class per program, so type identity holds across translation units ([ADR 0025](decisions/0025-rtti-vtable-typeinfo.md)):
  - `_ZTV<Class>` is `[typeinfo, slot 0, slot 1, ...]`. An object's vptr is `_ZTV<Class> + 4`, so slot `k` is `vptr + 4*k` and the RTTI pointer is `vptr - 4` — "slot -1".
  - `_ZTI<Class>` is `[0, _ZTS<Class>, flags, base typeinfo]`. The first word is the `type_info` vtable pointer and is zero in this subset, which has no virtual `type_info`. The fourth word is the base class's typeinfo or zero; it is the single-inheritance form `dynamic_cast` walks.
  - `_ZTS<Class>` is the source-level class name as a NUL-terminated byte array.
- `dynamic_cast<T*>(object)` lowers to `__krc_dynamic_cast(object, &_ZTI<T>)` unless the cast is an upcast, which needs no check; the helper returns the object pointer or null. `dynamic_cast<T&>(object)` lowers to the same call and throws `std::bad_cast` when the helper returns null, so the reference form needs the exception runtime. The operand may be a pointer, a reference, or an lvalue of a class type.
- Exception objects and landing pads ([ADR 0026](decisions/0026-exception-descriptors.md)):
  - The `.cpp_except` section is allocated read-only memory in the run image, not debug data. The linker merges the objects' sections and defines `__krc_eh_start` and `__krc_eh_end`.
  - Every function contributes a descriptor: `magic "KREH"`, function start and end, frame size, and a table of protected call sites `{start_pc, end_pc, landing_pad}`. The call's return address is the inclusive upper bound of its region.
  - `__cxa_throw(object, typeinfo, destructor)` is a naked assembly routine; the unwinder walks the saved `R7` chain using each descriptor's frame size, restores `R7`/`SP` to the frame that owns a landing pad, and enters it with the exception object pointer in `R0` and its typeinfo in `R1`.
  - The exception object carries a sixteen-byte header below it: typeinfo, destructor, the previously handled exception, and a spare word. `__cxa_allocate_exception`, `__cxa_begin_catch`, `__cxa_end_catch`, `__cxa_rethrow`, and `__cxa_free_exception` maintain it.
  - A catch clause tests `__krc_eh_matches(thrown_typeinfo, &_ZTI<T>)`, which walks the typeinfo base chain; an unmatched exception resumes at the next frame and aborts with exit status 134 when no frame handles it.
- Destructor cleanup on propagation runs through cleanup landing pads: each lexical scope that owns a destructible object gets a pad that destroys it and continues into the enclosing pad ([ADR 0027](decisions/0027-exception-cleanup.md)). There is still no personality routine.
- `-fno-exceptions` is not implemented: the driver rejects the option as unknown. `-fno-rtti` is not implemented either. The library subset must degrade to abort/terminate.

## Varargs, TLS, guards, static init

- Varargs are stack-passed; `float` promotes to `double`; `va_list` points at the first variadic slot.
- Static TLS in v1.0; dynamic TLS in v1.1. KR32 TP is R11; TCB layout is frozen here before codegen depends on it. KR16 has no TLS in v1.0.
- Function-local statics use `__cxa_guard_acquire`/`release`/`abort` plus a completion flag.
- Constructors use `.init_array`; destructors use `.fini_array` and/or `__cxa_atexit`; the linker provides the array bounds.

## Attributes and versioning

- Attribute ABI effects are defined in [attributes.md](attributes.md).
- `ABI_VERSION` is embedded in objects and the runtime; the linker rejects mismatches. ABI changes require an ADR, a version bump, and a runtime rebuild. No cross-version binary compatibility is promised.

## Freeze checklist

- [x] KR16 scalar calling convention, frame layout, and return register ([abi-kr16.md](abi-kr16.md)).
- [ ] KR16 aggregate passing, varargs, floating point, `this`, and more than three arguments.
- [ ] Itanium-like mangling grammar and examples.
- [ ] Vtable/RTTI layout, including virtual destructors.
- [x] EH descriptor encoding and landing-pad convention ([ADR 0026](decisions/0026-exception-descriptors.md)).
- [x] Cleanup landing pads for automatic objects ([ADR 0027](decisions/0027-exception-cleanup.md)).
- [ ] EH personality routine and exception specifications.
- [ ] TCB/TLS layout and guard variables.
- [ ] Static initialization/destruction order.
- [ ] Attribute ABI effects per target.
- [ ] ABI version and mismatch diagnostics.

## Open items

- KR16 ABI appendix (`plan.md` §17.2 item 4).
- EH record encoding (`plan.md` §17.2 item 5).
- `printf` compatibility policy (runtime, not ABI, but affects varargs).
