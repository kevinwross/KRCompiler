# C/C++ ABI

**Status:** Draft for C++/EH; the KR32 **C ABI is frozen** by [ADR 0001](decisions/0001-kr32-c-abi-freeze.md). C++/EH ABI freeze before M5. Until the remaining sections are frozen, `plan.md` §10 is normative.
**Targets:** KR32 (ILP32), KR16 (IP16 embedded profile), KR64 (placeholder, LP64).

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

- **KR32:** inherits `../../kr32/docs/abi.md` revision 1. Integer args R0–R3, FP args F0–F7, integer return R0, FP return F0/F1, R7 frame pointer, R11 thread pointer, 16-byte public call alignment, `sret` above 8 bytes, stack-passed varargs in 8-byte slots, `this` per the KR32 C++ ABI.
- **KR16:** to be frozen in this document before M7. Initial rule: integer args R1–R3 then stack, return R0, R4 frame pointer, 2-byte stack alignment, aggregates by value on the stack, varargs on the stack, `this` as the first integer argument.
- **KR64:** LP64 placeholder; first six integer args R0–R5, first eight FP args F0–F7.

## Name mangling

Itanium-like grammar with the `_Z` prefix; `extern "C"` unmangled; `_ZTV`/`_ZTI`/`_ZTS` for vtable/typeinfo/name. The grammar, substitutions, builtin codes, and operator names are frozen here before M5.

## Vtables, RTTI, exceptions

- Vtable pointer at object offset 0; RTTI pointer at slot -1; virtual slots in declaration order; complete/deleting destructor slots.
- `type_info`: vtable pointer, name pointer, flags.
- Exceptions: table-driven descriptors with call-site ranges, landing pads, cleanup actions, and type filters; `__cxa_*` runtime entry points; cross-TU merge by the linker.
- `-fno-exceptions` and `-fno-rtti` profiles are supported; the library subset must degrade to abort/terminate.

## Varargs, TLS, guards, static init

- Varargs are stack-passed; `float` promotes to `double`; `va_list` points at the first variadic slot.
- Static TLS in v1.0; dynamic TLS in v1.1. KR32 TP is R11; TCB layout is frozen here before codegen depends on it. KR16 has no TLS in v1.0.
- Function-local statics use `__cxa_guard_acquire`/`release`/`abort` plus a completion flag.
- Constructors use `.init_array`; destructors use `.fini_array` and/or `__cxa_atexit`; the linker provides the array bounds.

## Attributes and versioning

- Attribute ABI effects are defined in [attributes.md](attributes.md).
- `ABI_VERSION` is embedded in objects and the runtime; the linker rejects mismatches. ABI changes require an ADR, a version bump, and a runtime rebuild. No cross-version binary compatibility is promised.

## Freeze checklist

- [ ] KR16 calling convention, aggregate passing, varargs, and `this` rules.
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
