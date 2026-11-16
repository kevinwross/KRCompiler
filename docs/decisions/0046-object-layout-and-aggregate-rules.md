# 0046 — Object layout is one number, and the aggregate class asks the target

- Status: accepted
- Date: 2026-11-16
- Decision: `CIRGen::ir_size_of` computes a record's size from each field's own
  alignment, the KR16 backend lays a record out the same way, and a by-value
  aggregate's parameter class asks the target's `aggregate_rules` instead of
  inferring a capability from a size threshold
- Scope: `src/irgen/`, `src/backend/kr16_backend.cpp`, `include/krc/target/`,
  `src/target/`; changes KR32's aggregate parameter class for records of eight
  bytes or less and KR16's record layout. No IR format, object, or symbol change.

## Context

A record's size was computed in four places and they disagreed. Measured on
`struct Three { char a, b, c; }` and `struct Nested { Three inner; int tail; }`,
each number read from the emitted IR before this change:

| Size of | KR32, `sizeof(T)` | KR32, `sizeof(<expression>)` | KR16, `sizeof(T)` | KR16, `sizeof(<expression>)` |
|---|---|---|---|---|
| `Three` | 3 | 12 | 3 | 12 |
| `Nested` | 8 | 12 | 6 | 12 |

`ir_size_of` gave every non-float field a four-byte alignment, so
`sizeof(<expression>)` reported twelve where `sizeof(<type>)` reported three — and
the same number decided whether a by-value aggregate was passed in registers, so a
three-byte record was classified as memory class.

The KR16 backend's own `type_size` rounded every field up to the two-byte word
(read from the source, not measured): its layout of `Three` was six bytes against
`sizeof`'s three, and of `Nested` eight against six. That is the dangerous
direction: `new T[n]` sizes the allocation from `ir_size_of` and strides the
elements by the backend's layout, so the two must be the same number or the second
element lands past the allocation.

## Decision

`CIRGen::ir_align_of` derives a field's alignment from its own width — an integer
or float by its bits rounded up to a byte, a pointer from `profile_.pointer_bits`
(ADR 0037's rule, extended from address-shaped constants to object layout), an
array or record recursively — and `ir_size_of` pads and rounds by it. The KR16
backend's `type_size` uses the same model through a local `type_alignment`, which
replaces the per-field word rounding. Both now produce the number `Sema::size_of`
produced all along.

The aggregate parameter class is a property of the target, not an inference from
a size. `docs/targets/schema.md`'s ABI record already declares `aggregate_rules`
in v1, so the schema is implemented, not extended:

```cpp
enum class AggregateRules : std::uint8_t { MemoryOnly, RegistersOrMemory };
```

KR32 and KR64 are `RegistersOrMemory` (the size classification in `docs/abi.md`:
eight bytes or less in integer registers, an FP-only nine to sixteen in two FPRs,
everything else in memory). KR16 is `MemoryOnly`, because its backend has no
register path for aggregates and passes a hidden pointer. A `MemoryOnly` target
classifies every aggregate as memory class whatever its size.

## Consequences

- `sizeof(<expression>)` and `sizeof(<type>)` agree on both targets, and so do the
  allocation size and the element stride of `new T[n]`. `small_record_size.cpp`
  and `array_new_subword_record.cpp` pin both, link-and-run on each target.
- **KR32's aggregate ABI changes for small records.** A record that used to be
  memory class because the wrong size said twelve bytes is now passed in integer
  registers, which is what `docs/abi.md` describes. `small_aggregate_argument.cpp`
  pins it on KR32.
- **KR16 keeps passing aggregates as hidden pointers**, so the classification
  change cannot turn a working by-value aggregate into a register-class rejection.
  Measured before the `aggregate_rules` field existed: a three-`char` aggregate
  compiled on KR32 and was rejected by the KR16 backend.
- Record layouts on KR16 change wherever a field is narrower than a word: offsets
  move, and emitted images change. Determinism is unaffected — the layout is a
  function of the type, not of iteration order — and the four-row matrix is the
  evidence.
- Not fixed here, and recorded in `implementation.md`: the copy of a
  memory-class argument is still one whole-record load and store, which the
  backends move as one word. On KR32 a four-byte word hides it for records up to
  four bytes; on KR16 two bytes do not, so a three-byte record passed by value is
  still wrong there. `small_aggregate_argument.cpp` links on KR16 and does not run
  there.

## Alternatives considered

- **Leave `ir_size_of` target-agnostic and fix only the KR16 backend** — rejected:
  it leaves `sizeof(<expression>)` wrong on both targets and keeps the
  classification reading a number no layout agrees with.
- **Move layout into the target descriptor and delete the four copies** — the
  right home eventually, and it is what a schema-version bump would buy. Deferred:
  it touches both backends and the descriptor for no additional correctness over
  this change, and the field-level agreement is the part that was wrong.
- **Carry the frontend type into `ir_size_of` and ask `Sema::size_of`** — rejected
  for now: not every IR type has a frontend type at hand (a synthesized runtime
  call's parameters, for one), so the same function would need two paths.
