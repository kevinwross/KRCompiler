# C/C++ ABI

**Status:** Draft for C++/EH; the KR32 **C ABI is frozen** by [ADR 0001](decisions/0001-kr32-c-abi-freeze.md). C++/EH ABI freeze before M5. Until the remaining sections are frozen, `plan.md` §10 is normative.
**Targets:** KR32 (ILP32), KR16 (IP16 embedded profile; scalar subset in [abi-kr16.md](abi-kr16.md)), KR64 (placeholder, LP64).

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

- **KR32:** inherits `../../kr32/docs/abi.md` revision 1. Integer args R0–R3, FP args F0–F7, integer return R0, FP return F0/F1, R7 frame pointer, R11 thread pointer, 16-byte public call alignment, `sret` above 8 bytes, stack-passed varargs in 8-byte slots, `this` per the KR32 C++ ABI. A 64-bit integer such as `long long` occupies two consecutive integer argument words, low word first, and returns in R0/R1; see [ADR 0023](decisions/0023-kr32-64-bit-integers.md).
  - The memory-class return destination travels in `R4`, as revision 1 specifies, and does not consume an argument word. The caller loads it immediately before `CALL`; an indirect call keeps its target in `R5` so that the two do not collide ([ADR 0024](decisions/0024-kr32-tile-intrinsics.md)).
  - The transform stack pointer is per-thread, 64-byte aligned, and grows down in 64-byte entries. `crt0.s` sets it to the linker's `__stack_limit`, so tile saves occupy the 64 KiB between the program stack and the heap.
- **KR16:** scalar subset frozen by [ADR 0016](decisions/0016-kr16-scalar-abi.md) and documented in [abi-kr16.md](abi-kr16.md): integer/pointer args `R1`–`R3`, return `R0`, `R4` frame pointer, two-byte stack alignment, all other registers caller-saved. Aggregate parameters/returns, varargs, floating point, and more than three arguments are deferred.
- **KR64:** LP64 placeholder; first six integer args R0–R5, first eight FP args F0–F7.

## Name mangling

Itanium-like grammar with the `_Z` prefix; `extern "C"` unmangled; `_ZTV`/`_ZTI`/`_ZTS` for vtable/typeinfo/name. The grammar, substitutions, builtin codes, and operator names are frozen here before M5.

## Vtables, RTTI, exceptions

- Vtable pointer at object offset 0; RTTI pointer at slot -1; virtual slots in declaration order; complete/deleting destructor slots.
- `type_info`: vtable pointer, name pointer, flags.
- Vtables and typeinfo objects are weak globals, one per class per program, so type identity holds across translation units ([ADR 0025](decisions/0025-rtti-vtable-typeinfo.md)):
  - `_ZTV<Class>` is `[typeinfo, slot 0, slot 1, ...]`. An object's vptr is `_ZTV<Class> + 4`, so slot `k` is `vptr + 4*k` and the RTTI pointer is `vptr - 4` — "slot -1".
  - `_ZTI<Class>` is `[0, _ZTS<Class>, flags, base typeinfo]`. The first word is the `type_info` vtable pointer and is zero in this subset, which has no virtual `type_info`. The fourth word is the base class's typeinfo or zero; it is the single-inheritance form `dynamic_cast` walks.
  - `_ZTS<Class>` is the source-level class name as a NUL-terminated byte array.
- `dynamic_cast<T*>(object)` lowers to `__krc_dynamic_cast(object, &_ZTI<T>)` unless the cast is an upcast, which needs no check; the helper returns the object pointer or null. A reference cast needs `std::bad_cast` and is not supported without exceptions.
- Exceptions: table-driven descriptors with call-site ranges, landing pads, cleanup actions, and type filters; `__cxa_*` runtime entry points; cross-TU merge by the linker.
- `-fno-exceptions` is supported; `-fno-rtti` is not implemented yet. The library subset must degrade to abort/terminate.

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
- [ ] EH descriptor encoding and personality routine.
- [ ] TCB/TLS layout and guard variables.
- [ ] Static initialization/destruction order.
- [ ] Attribute ABI effects per target.
- [ ] ABI version and mismatch diagnostics.

## Open items

- KR16 ABI appendix (`plan.md` §17.2 item 4).
- EH record encoding (`plan.md` §17.2 item 5).
- `printf` compatibility policy (runtime, not ABI, but affects varargs).
