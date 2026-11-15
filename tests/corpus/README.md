# Corpus and tiers

`plan.md` §5.2 defines five corpus tiers. This file maps the fixtures that exist
to those tiers so that M8's "full Tier 0–2 corpus green on KR32 and Tier 0–1 on
KR16" can be checked rather than asserted.

## Where the fixtures live

- `tests/fixtures/c/*.cpp` — 380 fixtures, all C++ source. Each is registered in
  `tests/CMakeLists.txt` as a syntax-only check, a KR32 compile, a KR16 compile,
  or a KR16 link-and-run pair, depending on what it exercises.
- 44 of them are registered as `krc_run_kr16_<name>` link-and-run pairs and 34 as
  KR32 run tests. Those are the ones whose behaviour is verified by execution
  rather than by compilation.

## Tier mapping

| Tier | Plan content | Where it is covered |
|---|---|---|
| **0 — Smoke** | hello, arithmetic, functions | `hello`, `variadic`, `printf`, `vsprintf` (KR16 run pairs) |
| **1 — C core** | structs, pointers, arrays, control flow, preprocessor, varargs, `printf` | the remaining KR16 run pairs: `bits*`, `records_array`, `struct_local`, `switch`, `fnptr`, `matrix`, `pointer`, `string`, `shift`, `gep_stride`, `ptrdiff`, `call_loop`, `index`, `arguments`, `data`, `extern`, `multi`, `linked`, `runtime`, `heap` |
| **2 — C++ core** | classes, single inheritance, virtual, `new`/`delete`, templates, lambdas, `constexpr` | the C++ fixtures: `moved_record`, `record_copy`, `const_overload`, `value_parameter`, `class_template_signature`, `member_pointer_type`, `record_array_lazy`, `template_shared`, `utility`, `functional`, `std_array`, `thread_local_storage`, `dynamic_cast_void`, `typeinfo_members`, `call_operator_reference`, `variadic_count` |
| **3 — Runtime/OS** | `libc.s`, `resources/fileio.c` | `file_layer` covers the stream layer's open/close/transfer under `--fs-root` |
| **4 — Stress** | `C-for-U.c`, extracted C++ fixtures, compiler sources | not attempted |

## What this is not

The plan's extraction path is `tests/corpus/extracted/<feature>/<name>.c`, taken
from the doctest strings and from `../kr32/examples/`. This file is an
**inventory** of fixtures that already exist, not that extraction. Two
consequences:

- The tier-2 coverage above is a list of C++ fixtures, not a complete map of the
  language; nothing checks that the list is exhaustive.
- Nothing enforces this file. A fixture added without a tier entry is not
  detected, and a file renamed here would not break a test.

The extraction itself is M8's remaining work, and it is larger than this file:
it needs the doctest strings split out and the corpus examples imported, with
expected output and exit codes recorded per tier.

## What has been extracted

`tests/corpus/extracted/` now holds fixtures copied verbatim from
`../kr32/examples` at commit `ba69940`, with a provenance header added:

| Tier | Files | Registered as |
|---|---|---|
| 0 | `hello.c` | `krc_corpus_kr32_*`, `krc_corpus_kr16_*` (syntax-only) |
| 1 | `mainentry.c`, `binarytree.c`, `strings.c`, `hello.cpp` | the same, per target |

Ten tests, all passing: four C sources on both targets and `hello.cpp` on both.
They are **compile-only** — the link script does not provide the C library, so a
program that calls `printf` cannot be linked yet, which is why these are
syntax-only rather than link-and-run.

Not extracted, because the KR16 profile rejects what they use: `types.c` and
`libc-test.c` (`double`, `float`) and `varargs.c` (`double`). They compile for
KR32 and belong to a KR32-only tier-1 set when that exists.

## Tier 2, added after measuring the C++ examples

| Files | Targets | Notes |
|---|---|---|
| `balanced-tree.cpp`, `cxx_libc.cpp`, `FileOpen.cpp` | KR32 and KR16 | syntax-only |
| `cxx_tls.cpp`, `floats.c` | KR32 only | `long long` and `double`, which the KR16 profile rejects |

`matrix.cpp` is **not** extracted: it includes `kr32/matrix.hpp`, which this
sysroot does not carry. That is a dependency gap rather than a compiler one, and
it is the reason the plan's "six example files, only three substantial" is worth
reading carefully — one of the six needs a header we do not ship.

`libcpp.cpp` compiles on neither target: KR32 rejects variable length arrays at
line 2265 and KR16 rejects `long long` at line 30. Both are recorded compiler
limits, not corpus problems.