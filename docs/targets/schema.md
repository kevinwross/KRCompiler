# Target descriptor schema (v2)

**Status:** Frozen — schema v1 on 2026-11-09 ([ADR 0002](../decisions/0002-target-descriptor-schema-freeze.md)); v2 adds the `this_register` and `sret_register` ABI fields ([ADR 0053](../decisions/0053-kr216-target-profile.md)). Revised 2026-11-27 to describe the implemented record (documentation audit A14).

**Schema version:** 2

**Scope:** one `TargetProfile` record per target — the record Sema and IRGen read — plus the per-target descriptor in `src/backend/` that a backend reads for registers and ABI tables. Sizes are in **bits**; alignments and offsets are in **bytes** unless a field says otherwise.

## Identity

| Field | Type | Meaning |
|---|---|---|
| `id` | `TargetId` | Stable numeric ID stored in objects and KDBG (`1` KR32, `2` KR16, `3` KR64, `4` KR216, `5` KR232). |
| `name` | string | Command-line and image name (`kr32`, `kr16`, `kr64`, `kr216`, `kr232`). |
| `macro` | string | Target predefined macro (`__kr32__`). |
| `family` | enum | Register, accumulator, or stack machine. |
| `pointer_bits` | `uint32_t` | Pointer address width. |
| `endianness` | enum | Little or big. |
| `data_model` | enum | `Ilp32`, `Ip16`, `Lp64`, or `Ip16Long32`. |

## Data model

| Field | Type | Meaning |
|---|---|---|
| `char_bits`, `short_bits`, `int_bits`, `long_bits`, `long_long_bits` | `uint32_t` | Integer widths; `0` means unsupported. |
| `float_bits`, `double_bits`, `long_double_bits` | `uint32_t` | FP widths; `0` means unsupported. |
| `size_t_bits`, `ptrdiff_bits`, `intmax_bits`, `wchar_bits` | `uint32_t` | Standard typedef widths. |
| `char_unsigned` | `bool` | Plain `char` signedness. |

A scalar's alignment is not a profile field: Sema, IRGen, and the backends derive it as `min(size, 8)`, and a record takes its widest field's alignment.

## Capabilities

Boolean feature flags: `has_fpu`, `has_matrix`, `has_atomics`, `has_threads`, `has_eh`, `has_rtti`, `has_tls`, `has_cpp`, `backend_available`. A feature flag MUST be false when the target cannot execute the feature; code generation must not infer a capability from the data model.

## Aggregate rules

| Field | Type | Meaning |
|---|---|---|
| `aggregate_rules` | enum | `MemoryOnly`: every by-value aggregate travels through a hidden pointer. `RegistersOrMemory`: an aggregate of 8 bytes or less stays in integer registers, and every larger one is memory class. |

## The per-target descriptor

The register file and calling convention live beside the backend, not in the profile: `src/backend/kr32_descriptor.{h,cpp}` and `kr232_descriptor.{h,cpp}` are the two descriptor files. They supply:

| Field | Meaning |
|---|---|
| argument and return registers | Integer and FP locations, in order. |
| `this_register` | Register carrying C++ `this` at member-call entry. `kr232_descriptor` records `10`; no backend reads it, so `this` travels as argument 0 (audit A4). |
| `sret_register` | Register the caller loads with a memory-class aggregate's destination before a call; invalid means a hidden stack argument. |
| `stack_alignment` | Public call-boundary alignment in bytes. |
| `aggregate_rules` | The profile value above, restated for the backend. |

KR16 and KR216 encode the same facts inside their backends rather than in a descriptor file. The values for each target are in [kr32.md](kr32.md), [kr16.md](kr16.md), [../abi-kr216.md](../abi-kr216.md), [../abi-kr232.md](../abi-kr232.md), and [kr64.md](kr64.md).

## Rules

1. The schema version changes only with an ADR and a compatibility note; adding a field is a schema change even if its default is zero.
2. An unsupported width or capability is represented by `0` or `false`, not by a host default.
3. Sizes are bits; alignments and offsets are bytes.
4. The profile is data, not policy: target-specific selection choices belong in the backend and its descriptor.
5. Object and KDBG headers store the stable `id`; the linker rejects a mismatch.
