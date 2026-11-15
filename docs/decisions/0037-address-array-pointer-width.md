# 0037 — A verifier rule names the module's pointer width

- Status: accepted
- Date: 2026-11-14
- Decision: a rule about an address-shaped constant asks the module's target for
  the pointer width instead of naming a number
- Scope: `src/ir/`; changes a verifier rule, not the IR format

## Context

The verifier's rule for a constant array of addresses read:

```cpp
!is_integer(constant->type->element) || constant->type->element->bits != 32
```

Thirty-two is KR32's pointer width. On KR16 an address is sixteen bits, so a
typeinfo record — `[4 x i16]` holding two addresses and two zeros — failed
verification with *address-array constant has an invalid type or length*, and
**every** translation unit that included `<array>` failed on KR16 because
`<stdexcept>`'s typeinfo is emitted there.

`src/ir/` must not encode a specific target's numbers (`AGENTS.local.md`,
Architecture boundaries). The module already knows its target: `Module`
carries a `TargetId` and `src/ir/ir.h` already includes the target header.

## Decision

Rules about address-shaped values compare against
`target::get_target(module.target()).pointer_bits`. The rule's meaning — "the
element type is wide enough to hold an address" — is what it asks, and it holds
on every target a module can name.

## Consequences

- `<array>` verifies on KR16, which is what let it run there once the backend and
  the runtime stopped refusing the rest.
- The same shape of rule elsewhere in the verifier is subject to the same
  reading: a rule may name a *property* of the target, never a number that
  belongs to one.
- No IR format, ABI, object, or symbol change: the constants were always emitted
  with the target's own pointer width; only the check was wrong.
