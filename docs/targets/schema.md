# Target descriptor schema (v1)

**Status:** Frozen — 2026-11-09 ([ADR 0002](../decisions/0002-target-descriptor-schema-freeze.md)).

**Schema version:** 1

**Scope:** the shape of the C++ `constexpr` target descriptor. Target-specific values live in the target profile documents and the per-target descriptor tables.

A target descriptor has six sections. Every field is `constexpr`-representable; strings are `std::string_view`, arrays are `std::span` or fixed arrays, and all sizes are in **bits** while alignment and offsets are in **bytes** unless a field says otherwise.

## Identity

| Field | Type | Meaning |
|---|---|---|
| `id` | stable target ID | Numeric ID stored in objects. |
| `name` | string | Command-line/profile name (`kr32`, `kr16`, `kr64`). |
| `macro` | string | Target predefined macro (`__kr32__`). |
| `schema_version` | `uint32_t` | Descriptor schema version; currently `1`. |
| `abi_version` | `uint32_t` | C ABI version from `docs/abi.md`. |
| `family` | enum | Register, accumulator, or stack machine. |
| `pointer_bits` | `uint32_t` | Pointer address width. |
| `endianness` | enum | Little or big. |
| `max_alignment` | `uint32_t` | Maximum supported object alignment in bytes. |
| `address_space_count` | `uint32_t` | Number of address spaces; `1` in v1. |

## Data model

| Field | Type | Meaning |
|---|---|---|
| `char_bits`, `short_bits`, `int_bits`, `long_bits`, `long_long_bits` | `uint32_t` | Integer widths; `0` means unsupported. |
| `float_bits`, `double_bits`, `long_double_bits` | `uint32_t` | FP widths; `0` means unsupported. |
| `pointer_bits` | `uint32_t` | Pointer size. |
| `size_t_bits`, `ptrdiff_bits`, `intmax_bits`, `wchar_bits` | `uint32_t` | Standard typedef widths. |
| `char_unsigned` | `bool` | Plain `char` signedness. |
| `float_limits`, `double_limits` | limits record | `FLT_*`/`DBL_*` values. |

## Registers

| Field | Type | Meaning |
|---|---|---|
| `register_classes` | span of register classes | Integer, FP, matrix, flags, special. |
| `registers` | span of register records | Name, class, index, fixed role, callee-saved, reserved. |
| `stack_pointer`, `frame_pointer`, `thread_pointer`, `program_counter`, `flags_register` | register index | Fixed roles; invalid index means absent. |

## Instructions

| Field | Type | Meaning |
|---|---|---|
| `patterns` | span of instruction patterns | IR operation, pattern ID, operand forms, result form. |
| `addressing_modes` | span | Mode ID, operand forms, encoding cost. |
| `immediate_ranges` | span | Signed/unsigned bounds per operand class. |
| `branch_ranges` | span | Short/long branch distances, relaxation policy. |
| `encoding_sizes` | span | Instruction/extension sizes in bytes. |
| `cost_model` | record | Latency/throughput/size costs used by selection. |
| `relaxation_rules` | span | Which patterns may grow and the growth sequence. |

## ABI

| Field | Type | Meaning |
|---|---|---|
| `integer_argument_registers`, `fp_argument_registers` | index spans | Argument locations in order. |
| `integer_return_registers`, `fp_return_registers` | index spans | Return locations. |
| `stack_alignment` | `uint32_t` | Public call-boundary alignment in bytes. |
| `sret_threshold` | `uint32_t` | Aggregate size above which `sret` is used. |
| `stack_argument_slot` | `uint32_t` | Stack argument slot size in bytes. |
| `varargs_slots` | enum/record | Varargs placement and promotion rules. |
| `tls_model` | enum | Static/dynamic/none plus TP register. |
| `aggregate_rules` | enum/record | Register/stack/`sret` classification. |
| `eh_rtti` | record | Exception/RTTI conventions; empty in v1.0 C mode. |

## Capabilities

Boolean feature flags: `has_fpu`, `has_matrix`, `has_atomics`, `has_threads`, `has_eh`, `has_rtti`, `has_tls`, `has_cpp`, `backend_available`. A feature flag MUST be false when the target cannot execute the feature; code generation must not infer a capability from the data model.

## Rules

1. `schema_version` changes only with an ADR and a compatibility note. Adding a field is a schema change even if the default is zero.
2. An unsupported width or capability is represented by `0` or `false`, not by a host default.
3. Sizes are bits; alignments and offsets are bytes; alignment values are positive powers of two and must not exceed `max_alignment` for ordinary objects.
4. The descriptor is data, not policy: target-specific selection choices belong in `patterns`, `addressing_modes`, and the ABI record.
5. Object headers store the stable `id`, `abi_version`, and `pointer_bits`; the linker rejects mismatches.

The KR32/KR16 values are in [kr32.md](kr32.md), [kr16.md](kr16.md), and [kr64.md](kr64.md). Register and instruction tables are filled when the M2 backend is implemented; the schema above is frozen now.
