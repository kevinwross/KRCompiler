# 0057 — KR232 target profile and ABI appendix

- Status: accepted (2026-11-22)
- Date: 2026-11-22
- Decision: registers KR232 as a fifth VM target. It fits descriptor schema v2
  unchanged, takes KRO and KDBG target ID 5, and gets `docs/abi-kr232.md` as its
  ABI appendix
- Scope: identity, data model, capabilities, aggregate rules, the aggregate-return
  register and the appendix. The profile row, descriptor, assembler/encoder, image
  writer, sysroot, runtime and guest packages are kr232 plan M2–M3.

## Context

`kr232-plan.md` 0.17 is approved, with `kr232-isa.md` 0.4 and
`kr232-devices.md` 0.4 as the machine and device contracts it rests on. Four of
the plan's §2 decisions are compiler-side contracts and need a record before any
code is written: `MemoryOnly` aggregates (2.2), no 64-bit integer type (2.3),
signed `char` (2.4) and a 64-bit `long double` (2.5). §5 adds the aggregate-return
register, and §2.6 fixes one target ID shared by KRO and KDBG.

The guide is explicit that the first commit for a new target is documentation and
a failing test, not a backend (`new-target-guide.md` §0, §9). This is that record.

Unlike KR216 (ADR 0053), nothing here needs a schema change. Schema v2 already
carries `this_register` and `sret_register`, and every width is a per-profile
field — including `long_long_bits`, which this target sets to 0. No new
`DataModel` value is required, because `Ilp32` already names this target's
pointer, `int` and `long` relationship exactly.

## Decision

### Identity

`TargetId::Kr232`, profile name `kr232`, predefined macro `__kr232__`,
`TargetFamily::Register`, little-endian, 32-bit pointers. KRO and KDBG both carry
target ID **5**; 1–4 are KR32, KR16, KR64 and KR216, the last fixed by ADR 0053.

The number is recorded here and in `docs/symbols.md` now; `kROTargetId` gains its
case with the profile row, in M2. That function switches on `TargetId`, so the
constant cannot precede the enumerator, and adding the enumerator early would
force `to_string`, `to_target` and every other switch over the enum to handle a
target that has no profile — the failure that broke GCC's `-Wswitch` for KR216
(`implementation.md`, 2026-11-22T00:41).

### Data model

`DataModel::Ilp32` with three values set for this target:

| Field | Value | Why |
|---|---|---|
| `char_bits` / `char_unsigned` | 8 / false | C's plain `char` is signed here (plan 2.4) |
| `short_bits` | 16 | |
| `int_bits` | 32 | |
| `long_bits` | 32 | `Ilp32` |
| `long_long_bits` | **0** | there is no 64-bit integer type (plan 2.3) |
| `float_bits` / `double_bits` | 32 / 64 | IEEE binary32 / binary64 |
| `long_double_bits` | 64 | an alias for `double` (plan 2.5) |
| `size_t_bits`, `ptrdiff_bits`, `wchar_bits` | 32 | pointer-wide |
| `intmax_bits` | 32 | follows from 2.3 |
| `aggregate_rules` | `MemoryOnly` | every by-value aggregate is memory class (plan 2.2) |

`long_long_bits = 0` is a new profile *value*, not a new data model. The model
names the pointer/`int`/`long` relationship; the absence of a 64-bit type is a
capability of this target. If the schema later requires a positive value, that is
a schema ADR, not a kr232 one.

### Descriptor

`this_register` = **10** and `sret_register` = **6**. Both fields exist in schema
v2 (ADR 0053); nothing is added, no version changes, and no wire representation
is involved — descriptors are `constexpr` tables compiled into `krc`.

R6 carries a memory-class aggregate's destination, loaded by the caller after
argument evaluation so an argument that itself returns an aggregate cannot
overwrite it. It is an ordinary caller-saved scratch at all other times, and R2–R5
stay argument registers (kr232-plan §5).

### Capabilities

`has_fpu` and `has_cpp` are true. `has_matrix`, `has_atomics`, `has_threads`,
`has_tls`, `has_eh` and `has_rtti` are false; `backend_available` stays false
until the M3 backend lands. `has_rtti` false is documentation rather than
enforcement — the front end has no `-fno-rtti` — so a `dynamic_cast` compiles and
fails at link on the absent `__krc_dynamic_cast` (kr232-plan §2.8, §13).

### ABI appendix

`docs/abi-kr232.md` records the data model, register roles, calling convention,
frame layout, aggregate parameters and the R6 return destination, varargs, the
floating-point convention over F0–F3, function pointers and virtual calls, the
vtable and typeinfo layout with the reserved `[vptr - 4]` slot, and the
object/image rules. It is a draft until the M3 freeze (kr232-plan §8).

## Consequences

- The descriptor schema stays at v2. A fifth profile row is added; no new
  `DataModel`, no new field, no wire change.
- The target ID is fixed in two documents before any code exists, and the
  constant lands with the row that can use it.
- `long long` must be diagnosed rather than silently treated as 64 bits. M2
  verifies that Sema reads `long_long_bits` instead of deriving the size from the
  data model; if it derives it, that is an M2 task, not a later surprise.
- R6 removes nothing from the argument registers, and the caller's ordering
  obligation (load R6 last) becomes a backend test in M3.
- `dynamic_cast` and polymorphic `typeid` are unsupported by omission, and the
  link error names the missing helper, which is debuggable without a diagnostic.
- The per-feature matrices in `plan.md` §6.3 and Appendix D gain a kr232 column
  when the backend lands and support is measurable.

## Alternatives considered

- **A new `DataModel` value, as KR216 needed.** Rejected: `Ilp32` already names
  this target's pointer, `int` and `long` widths; the only difference is an
  absent 64-bit type, which one profile field expresses. A new model would make
  every switch over `DataModel` handle a value that differs in one field.
- **Assign the ID together with the enumerator, as KR216 did.** Rejected: it
  drags the profile row, `parse_target`, `to_target` and the toolchain wiring
  into M0, and the guide's first step is documentation only. The ID is a
  contract, so it is recorded as one; the constant follows the code.
- **Pass the aggregate destination as a hidden stack argument, as KR16 does
  (ADR 0048).** Rejected: schema v2 has a register field, R6 is otherwise only
  scratch, and a slot costs a store and a load on every aggregate-returning call.
- **Put KR232's ABI in `docs/abi.md` only.** Rejected for ADR 0053's reason: the
  values that differ are target values, and every target with its own appendix
  keeps the comparison honest.
