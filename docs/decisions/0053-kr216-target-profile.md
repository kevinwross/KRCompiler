# 0053 — KR216 target profile, descriptor schema v2, and ABI appendix

- Status: accepted (2026-11-21)
- Date: 2026-11-21
- Decision: adds KR216 as a fourth VM target — a new data-model value, two ABI
  fields on the descriptor, and `docs/abi-kr216.md` as its ABI appendix
- Scope: identity, data model, descriptor fields, capabilities, ABI appendix.
  The backend, assembler/encoder, KRO writer, sysroot, runtime and the 16-bit
  exception descriptor are separate work.

## Context

`../../../kr216/kr216-plan.md` v0.11 is approved and names this compiler as its
target (D32). Three of that plan's decisions need a compiler-side record before
any code is written.

- D29 / Q20: the data model is IP16 with a 32-bit `long` and a 64-bit
  `long long`. It is not `Ip16` (whose `long` is 16 bits, ADR 0016) and not
  `Ilp32` (whose pointers are 32 bits), so `DataModel` has no value for it.
- Q21: `this` lives in R10 and the descriptor declares it. The frozen schema
  (ADR 0002, `docs/targets/schema.md`) has no field for a `this` register.
- Q25: a memory-class aggregate returns through a destination in R6 that the
  caller loads just before `CALL`. The schema's `sret_threshold` classifies an
  aggregate but says nothing about where the destination travels.

The plan also requires `docs/abi-kr216.md` as the target's ABI appendix (M0),
and fixes KRO and KDBG target ID 4 for it; `kROTargetId` in `kro_object.cpp`
assigns 1, 2 and 3 today.

## Decision

### Identity

`TargetId::Kr216`, profile name `kr216`, predefined macro `__kr216__`,
`TargetFamily::Register`, little-endian, 16-bit pointers. KRO and KDBG both
carry target ID 4.

### Data model

`DataModel::Ip16Long32` is added for exactly this profile: `char` 8 signed,
`short` 16, `int` 16, `long` 32, `long long` 64, pointer 16, `float` 32,
`double` 64, `long double` 64 as an alias for `double`, `size_t` and
`ptrdiff_t` 16, `intmax_t` 64, `wchar_t` 16, `max_alignment` 2, and
`AggregateRules::MemoryOnly`.

### Descriptor schema v2

`docs/targets/schema.md` gains two ABI fields and `schema_version` becomes 2:

- `this_register` — the register index that carries C++ `this` at member-call
  entry. An invalid index means the target passes `this` as an ordinary first
  argument.
- `sret_register` — the register index the caller loads with a memory-class
  aggregate's destination before a call. An invalid index means the target
  passes the destination as a hidden stack argument, which is KR16's rule
  (ADR 0048).

Neither field has a wire representation. Descriptors are `constexpr` tables
compiled into `krc`, and the object header stores only `id`, `abi_version` and
`pointer_bits`. The version bump is source-compatibility only, and every
existing target sets both new fields to "invalid".

### Capabilities

`has_fpu` and `has_cpp` are true. `has_matrix`, `has_atomics`, `has_threads`,
`has_tls`, `has_eh`, `has_rtti` and `backend_available` are false.

`has_eh` stays false until the 16-bit exception descriptor has its own ADR
(kr216-plan Q22) and the runtime exists; `has_rtti` stays off with it, as the
plan's capability row requires. `backend_available` stays false until the
backend lands, so `--target=kr216` resolves the profile and then has nothing to
emit code with.

### ABI appendix

`docs/abi-kr216.md` records the data model, register roles, calling convention,
frame layout, aggregate rules, varargs, the vtable/RTTI scaling and the
object/image rules. It is a draft and freezes at M3 (kr216-plan §16). Where the
appendix and kr216-plan.md disagree, the plan is the authority until that
freeze.

## Consequences

- The descriptor schema is version 2. Nothing on disk encodes the schema
  version, so no object, image or debug payload changes; a reviewer of a new
  target checks two more fields.
- KR216 can be named on the command line and resolved by the target table while
  it still has no backend. Registering the profile before the backend lets the
  parser, Sema and the driver be exercised against the target's data model
  before any code generation exists.
- KRO and KDBG writers and readers must accept ID 4 before an image can be
  produced. `krld` already rejects a mixed-target link by comparing object IDs,
  so a kr216 object linked with a KR16 one fails without a new check.
- The ABI appendix is not frozen; M3 freezes it with a version bump.
- The per-feature matrices in `plan.md` §6.3 and Appendix D gain a KR216 column when
  the backend lands and support is measurable; the capability flags in the
  descriptor are the contract until then, and §4.1, §10.1, §10.2 and Appendix C
  carry the target now.
- KR216 is the first 16-bit target with floating point, 64-bit integers and
  C++, so it will expose every place the IR, ABI, EH runtime or debug format
  assumes 32-bit words. That is the plan's recorded schedule risk, not a
  consequence of this ADR.

## Alternatives considered

- Reuse `DataModel::Ip16`. Rejected: its `long` is 16 bits, so a 32-bit value
  cannot exist and every `long` becomes a pair.
- Reuse `DataModel::Ilp32`. Rejected: 32-bit pointers double the size of every
  pointer and every spill on a machine with a 64 KiB address space.
- Pass `this` as argument 0. Rejected by the target plan (Q13): it costs an
  argument register on every member call.
- Pass an aggregate return destination as a hidden stack argument, as KR16 does
  (ADR 0048). Rejected by Q25: R6 is free on KR216, and a stack slot costs a
  push and a pop on every aggregate-returning call.
- Put KR216's ABI in `docs/abi.md` only. Rejected: that document is the KR32
  base ABI, KR16 already has its own appendix, and the values that differ here
  (R6 destination, R10 `this`, two-byte stack slots) are target values.
