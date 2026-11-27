# Attributes

**Status:** Draft, and not implemented. No attribute reaches Sema or a backend, so nothing below has a code-generation effect. Freeze before M5. Until frozen, `plan.md` §6.4 and §10.8 are normative.

## Purpose

Attributes select target behaviour that the C/C++ type system cannot express: interrupt handlers, naked functions, section placement, alignment, and calling conventions. They are part of the ABI contract when they change register/stack behaviour.

## Current state

The parser skips `__attribute__((...))`, `__declspec(...)`, and `__asm__(...)` without recording what it skipped, so the GNU spelling parses and is ignored. The standard `[[...]]` form is not parsed at all: `[[nodiscard]]` and `[[krc::interrupt]]` are syntax errors. The tables and rules below describe the intended contract, not behaviour the compiler has.

## Syntax

The intended surface accepts both spellings:

```cpp
[[krc::interrupt]] void timer_isr(void);
__attribute__((interrupt)) void timer_isr(void);
```

Standard attributes would use their normal C++ spelling (`[[nodiscard]]`, `[[maybe_unused]]`, `[[deprecated]]`, `[[noreturn]]`). Unknown attributes are diagnosed or ignored according to the dialect's rules; they are never silently treated as an ABI feature.

## v1 attributes (planned)

| Attribute | Applies to | Effect |
|---|---|---|
| `interrupt` | Functions | Saves/restores the target's interrupt context and returns with the target's interrupt-return instruction. |
| `naked` | Functions | No prologue/epilogue; the body is entirely user-provided assembly/intrinsics. |
| `noreturn` | Functions | The function does not return; callers may omit fallthrough code. |
| `section("name")` | Functions/objects | Places the entity in the named section. |
| `aligned(n)` | Functions/objects | Raises alignment up to the target maximum. |
| `krc::cc("name")` | Functions | Selects a documented calling convention for the target. |

## ABI effects

- `interrupt` and `naked` change saved registers, stack alignment, and the return instruction. Their exact behaviour is target-specific and must be tested on KR32 and KR16.
- `section` and `aligned` affect object layout; the linker validates placement and alignment.
- Custom calling conventions must be documented in [abi.md](abi.md) before code generation depends on them.

## Validation

- Reject attributes that are meaningless or unsafe for the selected target with a target-specific diagnostic.
- Reject `naked` on functions with C++ objects requiring construction/destruction.
- Reject `interrupt` on functions with a non-void return type unless the target documents otherwise.
- Do not let an attribute silently change the ABI without a corresponding ABI document entry.

## Freeze checklist

- [ ] Grammar and placement rules for both spellings.
- [ ] Target behaviour of `interrupt` and `naked` on KR32 and KR16.
- [ ] Custom calling-convention registry.
- [ ] Section/alignment validation rules.
- [ ] Diagnostics for invalid combinations.
- [ ] Interaction with C++ constructors/destructors and exceptions.

## Open items

- Whether `packed` is added as an extension (currently diagnosed).
- Interrupt attributes for a future 64-bit target.
