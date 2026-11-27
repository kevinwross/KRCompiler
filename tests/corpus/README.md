# Corpus and tiers

`plan.md` §5.2 defines five corpus tiers. This file maps the fixtures that exist
to those tiers so that M8's "full Tier 0–2 corpus green on KR32 and Tier 0–1 on
KR16" can be checked rather than asserted.

## Where the fixtures live

- `tests/fixtures/c/*.c`, `*.cpp` — 589 fixtures. Each is registered in
  `tests/CMakeLists.txt` as a syntax-only check, a KR32 compile, a KR16 compile,
  or a link-and-run pair, depending on what it exercises.
- The link, run, and syntax-only registrations are in `tests/CMakeLists.txt`;
  their counts move with every fixture, so this file does not restate them.

## Tier mapping

| Tier | Plan content | Where it is covered |
|---|---|---|
| **0 — Smoke** | hello, arithmetic, functions | `hello`, `variadic`, `printf`, `vsprintf` (KR16 run pairs) |
| **1 — C core** | structs, pointers, arrays, control flow, preprocessor, varargs, `printf` | the remaining KR16 run pairs: `bits*`, `records_array`, `struct_local`, `switch`, `fnptr`, `matrix`, `pointer`, `string`, `shift`, `gep_stride`, `ptrdiff`, `call_loop`, `index`, `arguments`, `data`, `extern`, `multi`, `linked`, `runtime`, `heap` |
| **2 — C++ core** | classes, single inheritance, virtual, `new`/`delete`, templates, lambdas, `constexpr` | the C++ fixtures: `moved_record`, `record_copy`, `const_overload`, `value_parameter`, `class_template_signature`, `member_pointer_type`, `record_array_lazy`, `template_shared`, `utility`, `functional`, `std_array`, `thread_local_storage`, `dynamic_cast_void`, `typeinfo_members`, `call_operator_reference`, `variadic_count` |
| **3 — Runtime/OS** | `libc.s`, `resources/fileio.c` | `file_layer` covers the stream layer's open/close/transfer under `--fs-root` |
| **4 — Stress** | `C-for-U.c`, extracted C++ fixtures, compiler sources | not attempted |

## What this is not

The plan's extraction path is `tests/corpus/extracted/<tier>/<name>.c`. The
extraction below is an inventory of example programs, not the doctest-string
corpus the plan describes: the strings have not been split out, and expected
output and exit codes are not recorded per tier. Two consequences:

- The tier-2 coverage above is a list of C++ fixtures, not a complete map of the
  language; nothing checks that the list is exhaustive.
- Nothing enforces this file. A fixture added without a tier entry is not
  detected, and a file renamed here would not break a test.

Completing the doctest-string extraction and recording per-tier expected output
is what M8's "full Tier 0–2 corpus green" gate still needs.

## What has been extracted

`tests/corpus/extracted/` holds fixtures copied from `../kr32/examples` at commit
`ba69940`, with a provenance header added:

| Tier | Files |
|---|---|
| 0 | `hello.c`, `arithmetic.c`, `functions.c` |
| 1 | `arrays.c`, `binarytree.c`, `control.c`, `floats.c`, `hello.cpp`, `mainentry.c`, `pointers.c`, `preprocessor.c`, `printf.c`, `strings.c`, `structs.c`, `varargs.c` |
| 2 | `balanced-tree.cpp`, `classes.cpp`, `constexpr2.cpp`, `cxx_libc.cpp`, `cxx_tls.cpp`, `FileOpen.cpp`, `inheritance.cpp`, `lambdas.cpp`, `new_delete.cpp`, `templates.cpp`, `virtual.cpp` |

`hello`, `mainentry`, `binarytree`, and `strings` link and run on KR16; the rest
are syntax-only rows, per target, as `tests/CMakeLists.txt` registers them.
`floats.c` and `cxx_tls.cpp` are KR32-only because the KR16 profile rejects
`double` and `long long`.

`matrix.cpp` is **not** extracted: it includes `kr32/matrix.hpp`, which this
sysroot does not carry. That is a dependency gap rather than a compiler one.

## KR32-only fixtures

| Files | Why |
|---|---|
| `cxx_tls.cpp`, `floats.c` | KR16 rejects `long long` and `double`, which these use |

`balanced-tree.cpp`, `cxx_libc.cpp`, and `FileOpen.cpp` compile for both targets.

`libcpp.cpp` compiles on neither target: KR32 rejects variable length arrays at
line 2265 and KR16 rejects `long long` at line 30. Both are recorded compiler
limits, not corpus problems.