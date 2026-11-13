# KRCompiler — Implementation Tracker

**Status:** Plan approved 2026-11-09. Current milestone: **M0 — implemented locally; G0 remote CI check pending**.
**Plan:** [plan.md](plan.md)
**Review record:** [docs/planning/nemo-plan-review.md](docs/planning/nemo-plan-review.md)

This file is the live task tracker. `plan.md` is the design contract; this file records what is being built, what passes, and what remains. Update it in the same change as the work it describes.

## 1. Rules

- Milestone scope, target data models, gates, and decisions are owned by `plan.md`. A change to a `D-xxx` decision needs an Appendix A entry in `plan.md`; ABI, object, symbol, target-description, or IR changes also need an ADR under `docs/decisions/`.
- Do not mark a gate passed until the command that proves it has run and its output is recorded here.
- A milestone is complete only when its gate passes on both required host toolchains, the relevant docs are updated, and the next milestone's prerequisites are identified.
- No external runtime dependency. doctest is the only vendored test dependency.
- The compiler is host-only. `src/` exists from M0 onward.

## 2. Definition of done

A change is done when:

- [ ] It builds clean with no warnings on MSVC (`/W4 /WX`) and GCC/Clang (`-Wall -Wextra -Wpedantic -Werror`).
- [ ] `ctest --test-dir build --build-config Release` is green.
- [ ] The deterministic-output check passes: the same inputs built twice compare byte-for-byte.
- [ ] `docs/` and this file are updated in the same change.
- [ ] An ADR exists for ABI, object format, symbol format, target description, or IR changes.
- [ ] An Appendix A entry exists if a `D-xxx` decision changed.
- [ ] No secrets, debug output, or unrelated edits are in the diff.

## 3. Milestones

| Milestone | Gate | Status | Evidence |
|---|---|---|---|
| M0 Foundation | G0 | complete locally; remote CI pending | MSVC Debug/Release, WSL GCC/Clang Debug, WSL GCC Release, WSL Clang ASan: 8/8 CTest at M0 close, 14/14 after M1 Sema tests; fixture extractor 10 fixtures |
| M1 C front end + sysroot | G1 | complete locally; remote CI pending | Lexer, preprocessor, C11 parser, Sema, generated sysroot bits, `-fsyntax-only`, negative diagnostics, 15/15 CTest, and frozen C ABI/target schema; remote CI pending |
| M2 IR + KR32 backend + assembler | G2a | G2a passed locally; allocator/peepholes landed | Typed SSA IR, C IRGen, KR32 assembly emitter, encoder, descriptor, assembler, allocator, and peepholes; pattern selection/relaxation and allocator emission remain |
| M3 Object + linker + startup | G2a/G2b passed locally | in progress | KRO, linker, archives, map/size/layout scripts, runtime, and image writer work; weak/COMDAT, GC, and relaxation remain |
| M4 C core + debug format | G3/G4 passed locally | complete locally | C core, bit-fields, debugger session, frozen KDBG v1, and dump tooling are in place; M5 C++ is next |
| M5 C++ core | G5 passed locally | in progress | G5 runs; classes, methods, constructors, destructors, `new`/`delete`, default copy, inheritance, and static devirtualization work; general vtables, move semantics, and dynamic dispatch remain |
| M6 C++11/14/17 | G6 | in progress | `nullptr`, `auto`/`decltype`, range-for, `constexpr`, `enum class`, and `::` qualification work; templates, lambdas, move semantics, and structured bindings remain |
| M7 KR16 backend + C | G7 | complete locally | KR16 syntax, IP16 data model, IR, scalar codegen, assembler, global data, and a minimal `.kex` image work; `--emit-object` plus `krld` link startup and multiple user objects with absolute relocations. `hello.c`, `sum.c`, and a cross-object fixture run through the linked path; the wider runtime remains |
| M8 v1.0 hardening | G8 | in progress | KR16 object and linked-image determinism checks pass; corpus, debugger parity, docs, and the release tag remain |
| M9 v1.1 runtime/libc/host services | G9 partial | in progress | A KR16 string/memory runtime object builds and links; host services, file I/O, and the full libc port remain |
| M10 v1.1 language/runtime completion | G9 | pending | — |

Gate definitions are in `plan.md` §5.7. A gate belongs to the milestone that can produce its evidence; earlier milestones do not claim later gates.

## 4. Milestone checklists

### Approval close-out (completed 2026-11-09)

- [x] §18.1 checklist signed off; §18.3 sign-off recorded.
- [x] `plan.md` status and version updated.
- [x] `implementation.md` created.
- [x] Docs index and draft specifications created under `docs/`.
- [x] `AGENTS.md` and `AGENTS.local.md` updated for the approved status.

### M0 — Foundation (2–3 weeks)

- [x] Create the repository tree: `include/krc/`, `src/frontend/`, `src/ir/`, `src/backend/`, `src/asm/`, `src/linker/`, `src/debug/`, `src/tools/`, `targets/`, `tests/`, `sysroot/`, `tools/`, `examples/`. Host compiler code stays under `src/`; target source is under `targets/`.
- [x] Add `CMakeLists.txt`, `CMakePresets.json`, and `cmake/` with C++20, warnings-as-errors, Debug/Release, and sanitizer presets.
- [x] Vendor doctest and add the first unit-test target.
- [x] Add Windows and Linux CI workflows; add a Linux ASan/UBSan job. Workflow present; remote run pending because no GitHub remote is configured.
- [x] Add the deterministic-build check harness. The M0 test covers CLI output; full object determinism lands with M2/M3.
- [x] Implement the source manager and diagnostics skeleton (file/line/column, include stack, severity, notes, error limit).
- [x] Add the target-descriptor skeleton and the KR32/KR16/KR64 identity records.
- [x] Add `krc_extract_fixtures` and extract the first C/C++ doctest fixtures (10 seed fixtures).
- [x] Record G0 evidence in this file.

**Gate G0:** repository builds, CI is green, docs skeleton and fixture extractor exist.

**M0 evidence (2026-11-09):**

- Windows MSVC 19.51 Debug: configure, build, `ctest` 8/8.
- Windows MSVC 19.51 Release: configure, build, `ctest` 8/8.
- WSL Alpine 3.22 GCC 14.2 Debug: configure, build, `ctest` 8/8.
- WSL Alpine 3.22 GCC 14.2 Release: configure, build, `ctest` 8/8.
- WSL Alpine 3.22 Clang 20.1 Debug: configure, build, `ctest` 8/8.
- WSL Alpine 3.22 Clang 20.1 ASan/UBSan Debug: configure, build, `ctest` 8/8.
- `krc --version`, `krc --help`, `krc --list-targets`, `krld --version`, and the missing-file diagnostic behave as specified.
- The extractor produced 10 seed fixtures from `../kr32/tests/unit/test_cc_1.cpp` and `../kr32/tests/cxx/test_cxx_codegen_1.cpp`; provenance is in `tests/fixtures/extracted/manifest.tsv`. A second extraction produced byte-identical output.
- `CMakePresets.json` parses as JSON and `.github/workflows/ci.yml` parses as YAML.
- The CI workflow is `.github/workflows/ci.yml`; it has not run remotely because this checkout has no GitHub remote. The CI-green half of G0 remains unverified.
- After the M1 Sema tests were added, the same six configurations pass 14/14 CTest.

### M1 — C front end + sysroot (6–8 weeks)

- [x] Preprocessor: macros, conditionals, includes, `#pragma once`, `__has_include`, `__COUNTER__`, `#line`, target feature macros.
- [x] Lexer: C tokens, literals, digraphs, source locations.
- [x] C11 parser: declarations, expressions, statements, initializers, attributes/directives. The parser builds a generic AST and accepts the KR32 C examples; Sema runs over the AST.
- [x] Sema core: scopes, types, conversions, structs/unions/enums, bit-fields, `_Generic`, `_Static_assert`, `_Alignas`/`_Alignof`. Scopes, built-in types, pointers, arrays, functions, records, enums, typedefs, member lookup, calls, sizeof/alignof/offsetof, bit-field and `_Alignas` layout, and basic conversions are implemented; generic-selection typing is intentionally shallow until IRGen.
- [x] Real sysroot: common headers plus KR32 `bits/` generated from the target descriptor. Common headers and per-target `bits/types.h`/`bits/stdint.h` exist; `generate_bits_types`/`generate_bits_stdint` regenerate them from `TargetProfile` and `test_sysroot_bits` verifies the checked-in files.
- [x] AST dump (`--dump-ast`) and `-fsyntax-only`.
- [x] Freeze the C ABI and target-descriptor schema before M2. The KR32 C ABI is frozen by [ADR 0001](docs/decisions/0001-kr32-c-abi-freeze.md); target descriptor schema v1 is frozen by [ADR 0002](docs/decisions/0002-target-descriptor-schema-freeze.md).
- [x] Negative diagnostics for unsupported features (VLAs, `_Complex`, `_Atomic`, `<threads.h>`, K&R, implicit declarations).
- [x] Gate G1: `krc -fsyntax-only` accepts Tier 1 C on KR32. KR16 syntax and IR generation now run on the IP16 profile; KR16 codegen remains gated at G7. Local evidence: all nine KR32 examples, the five stored C fixtures, and 112/132 extracted `test_cc_*` C fixtures pass; the remainder are expected negatives, unsupported extensions, or extractor artifacts. Remote CI remains pending.

**M1 evidence (2026-11-09 20:36:18 -08:00):**

- Windows MSVC 19.51 Debug/Release, WSL Alpine 3.22 GCC 14.2 Debug/Release, WSL Clang 20.1 Debug, and WSL Clang 20.1 ASan/UBSan each pass 15/15 CTest.
- `krc -fsyntax-only` exits 0 for all nine KR32 C examples: `hello.c`, `mainentry.c`, `libc-test.c`, `binarytree.c`, `types.c`, `varargs.c`, `floats.c`, `strings.c`, and `fileio.c`.
- `krc -fsyntax-only` exits 0 for `../kr16/examples/hello.c` and `sum.c`; KR16 `puts` produces an implicit-declaration warning because the source does not include a header. This is not the G7 KR16 gate.
- The five stored C fixtures under `tests/fixtures/extracted/test_cc_1/` pass with Sema.
- Corpus check: 132 C fixtures extracted from `../kr32/tests/unit/test_cc_*.cpp`; 112 pass. The 20 rejects are 3 `<input>` artifacts, 1 missing `<stdlib.h>` include, 2 GCC builtin extensions, 1 `typeof`, 8 atomic/thread cases, 2 complex/imaginary cases, and 3 expected header/#error/invalid-syntax negatives.
- `krc -E` prints a preprocessed token stream, `krc --dump-ast` prints the generic AST, and `__has_include`, `#line` (including filename override), deterministic `__DATE__`/`__TIME__`, `__func__`, `__builtin_offsetof`, anonymous structs/unions, nested designators, `_Generic`, and `_Alignas` are handled.
- `_Static_assert` is evaluated: `libc-test.c` passes its `sizeof`/`offsetof` assertions; record, bit-field, and `_Alignas` layout tests pass.
- The sysroot has 16 common headers plus per-target `bits/types.h` and `bits/stdint.h` for KR32, KR16, and KR64. `krc_gen_sysroot_bits` writes those bits into the staged sysroot at `build/<preset>/stage/sysroot`; `test_sysroot_bits` verifies the generator output matches the checked-in headers.
- `tests/unit/test_sema.cpp` covers scopes, duplicate declarations, struct members, function arity, arrays/pointers, enums, sizeof/offsetof, bit-fields, `_Alignas`, static assertions, K&R rejection, and unsupported features.
- M1 has no local checklist items left. The only open evidence item is the remote CI run, which cannot start until a GitHub remote exists. C++ remains M5.

### M2 — IR + KR32 backend + assembler (8–10 weeks)

- [x] Typed SSA IR model, builder, verifier, and textual printer.
- [ ] Freeze `docs/ir.md` and `docs/targets/kr32.md` before implementation. `docs/ir.md` is frozen by [ADR 0003](docs/decisions/0003-ir-core-freeze.md) and extended by [ADR 0004](docs/decisions/0004-ir-data-array-and-declarations.md); the KR32 register/instruction tables and their values remain the M2 descriptor task.
- [ ] C IRGen: scalars, aggregates, pointers, arrays, calls, varargs, `volatile`, debug locations. Scalars, pointers, arrays, calls, varargs calls, globals/string literals, returns, if/while/do/for, break/continue, switch, goto, and labels are implemented; aggregates are largely implemented; direct `volatile` local/global accesses carry IR flags; volatile pointer/reference qualifiers, debug locations, and some conversions remain.
- [ ] KR32 target descriptor: data model, registers, patterns, ABI, encodings, relaxation. `src/backend/kr32_descriptor.*` has the register/ABI subset and `src/asm/kr32_encoder.*` has the base encoding subset; pattern selection, relocation/relaxation, and the remaining encodings are pending.
- [x] Instruction selection, graph-colouring register allocation, frame layout, peepholes. The backend uses a deterministic stack-slot selection/frame baseline; `src/backend/register_allocator.*` implements liveness, interference, degree-ordered graph colouring, and spill reporting; `src/backend/peephole.*` removes identity/zero-strength operations. Register allocation is not yet wired into code emission.
- [x] Assembly printer and shared assembler framework with the KR32 syntax/encoding module. `src/asm/kr32_assembler.*` parses the emitted subset, lays out text/data, resolves local labels, records external relocations, and emits section bytes; the object writer path remains M3.
- [ ] Gate G2a: `hello.c`/`mainentry.c` compile to KR32 assembly with correct call/return sequences. Both emit assembly, both assemble as `.k32` objects with the KR32 assembler, and `hello` links with a stub runtime and runs to exit 0. G2a is passed locally; the shared encoder and descriptor tables remain M2 hardening work.

**M2 evidence (2026-11-09 21:16:52 -08:00, in progress):**

- `src/ir/ir.h` and `src/ir/ir.cpp` implement the frozen v1.0 IR plus byte-array constants and declaration-only functions: structural types, named records, constants, globals, functions, blocks, SSA instructions, memory flags, debug locations, and reserved v1.1 opcodes.
- `IRBuilder` covers arithmetic, shifts, comparisons, casts, loads/stores, allocas, GEP, calls, branches, switch, returns, phi, select, and aggregate extract/insert. It uniquifies SSA names per function.
- `Verifier` checks terminators, phi placement and predecessors, SSA dominance, type consistency, memory operations, calls, branches, globals, and reserved v1.1 opcodes.
- `print_module`/`print_function` produce a deterministic human-facing dump with target, named types, globals, declarations, and instructions.
- `src/irgen/c_irgen.*` lowers the C AST through Sema: scalar types, pointers, arrays, function declarations/definitions, local allocas, loads/stores, calls (including variadic `printf`), string globals, returns, `if`/`while`/`do`/`for`, and `break`/`continue`.
- `krc --dump-ir` generates and verifies IR; `hello.c` and `mainentry.c` both produce verified IR.
- `src/backend/kr32_backend.*` emits KR32 assembly with a frame for every SSA value and alloca object, R0–R2 scratch lowering, R0–R3 call arguments, 8-byte varargs slots, prologue/epilogue, and `.text`/`.data` output. `krc -S` emits assembly for both smoke programs.
- External validation: `../kr32/build/.../kr32 as --object` accepts the emitted `hello.s` and `mainentry.s` as `.k32` objects. Linking `hello` with a stub `_start`/`putchar`/`exit` and running under the KR32 VM halts with `exit=0`, validating the frame, call/return, loop, and pointer path. `mainentry` assembly contains two `CALL printf` sequences with staged varargs; its stub run is not used as gate evidence because the stub `_start` does not supply argc/argv.
- `src/asm/kr32_encoder.*` encodes the base KR32 word fields with range validation and extension words; `tests/unit/test_kr32_encoder.cpp` reproduces the ISA golden encodings and checks invalid immediates/registers. `src/backend/kr32_descriptor.*` records the C ABI register roles, argument/return registers, frame/thread pointers, and stack/call alignment.
- `src/asm/kr32_assembler.*` parses the backend's `.text`/`.data` subset: labels, `.global`/`.extern`, `.align`/`.byte`/`.word`/`.zero`, registers, immediates, frame-memory operands, branches, calls, and returns. It lays out sections, resolves local labels, records absolute external relocations, and emits section bytes. The in-tree assembler accepts both `hello` and `mainentry` backend output.
- `tests/unit/test_assembler.cpp` covers encoding, labels, data layout, external relocation offsets, and undefined-symbol rejection; `test_irgen.cpp` assembles the backend output for both smoke programs.
- `src/backend/register_allocator.*` builds per-instruction liveness, interference edges, and a degree-ordered colouring with spill reporting; `tests/unit/test_register_allocator.cpp` covers a two-colourable function, spill pressure, and peephole identity removal. The backend still emits through frame slots by default.
- `tests/unit/test_ir.cpp` covers construction, verification failures, phi/dominance, reserved opcodes, deterministic printing, and structural type printing. `tests/unit/test_irgen.cpp` covers scalar functions, `hello.c`, `mainentry.c`, and the assembly emitter. `krc_tests` has 79 doctest cases; all six configurations pass 18/18 CTest.
- Remaining M2 work: instruction-pattern selection and relocation/relaxation; the register allocator is not yet the emission path.

### M3 — Object + linker + startup (6–8 weeks)

- [x] `KRO` object writer/reader: header, sections, symbols, relocations, debug table, CRC.
- [x] Freeze `docs/object.md` before the object writer. Frozen by [ADR 0005](docs/decisions/0005-kro-object-format-freeze.md).
- [ ] `krld`: target validation, archives, weak/COMDAT, GC, relaxation, map/size, layout scripts. Target validation, symbol resolution, absolute relocation patching, KR32 image writing, KRO archive extraction, deterministic `--map`/`--print-size` output, and NAME/ADDRESS layout scripts are implemented; weak/COMDAT, GC, and relaxation remain.
- [x] `crt0`, heap symbols, minimal libc (`putchar`, `puts`, `exit`), KR32 image writer. `targets/kr32/runtime/crt0.s` and `targets/kr32/runtime/libc.c` provide startup, console, `puts`, `printf` integer/string formats, and exit; the linker defines `__heap_start`/`__heap_end`/`__stack_top`/`__stack_limit`; floating-point formatting and the allocator remain.
- [x] Gate G2b: `hello.c` and `mainentry.c` assemble, link, and run on KR32. Both pass through `krc -c` + `krld` + the KR32 VM; `hello` prints `Hello, KR32!` and exits 0, and `mainentry` prints argc/argv and exits 0.

**M3 evidence update (2026-11-09T22:03:10-08:00):**

- `targets/kr32/runtime/crt0.s` is assembled by the in-tree assembler through `krld`; `krld` now accepts `.s` inputs alongside KRO objects.
- `krld hello.kro targets/kr32/runtime/crt0.s -o hello.k32` links and runs: output `Hello, KR32!`, exit 0.
- `krld mainentry.kro targets/kr32/runtime/crt0.s -o mainentry.k32` links and runs to exit 0; `mainentry` prints argc/argv through the integer/string `printf` path.
- Local-label and data-symbol references now emit relocations, so runtime calls such as `CALL exit` and `LEA .str` are patched when sections are placed.
- `cmake/LinkRuntime.cmake` and `krc_link_hello`/`krc_link_puts` cover the object → link image path in CTest.
- `targets/kr32/runtime/libc.c` supplies `puts`; `krc -c puts.c` + `krc -c libc.c` + `krld` + crt0 prints `hello from puts` and exits 0. The linker defines the four heap/stack boundary symbols.
- `krc_tests` has 86 cases / 462 assertions; all six configurations pass 21/21 CTest.
- The double-build determinism check now covers both a KR32 object and a KR16 `.kex`: `krc_determinism_object` and `krc_determinism_kr16_image` compile the same input twice and compare SHA-256 digests.

**M3 evidence (2026-11-09 21:49:58 -08:00, in progress):**

- `docs/object.md` and [ADR 0005](docs/decisions/0005-kro-object-format-freeze.md) freeze KRO v1: 120-byte header, fixed 64/40/32-byte section/symbol/relocation records, table order, string table, payload alignment, CRC32, build ID, and the relocation registry.
- Static file-scope objects lower to IR `internal global` and emit a local symbol without `.global`, so two objects may each define the same static name. See [ADR 0022](docs/decisions/0022-internal-globals.md).
- External global declarations lower to IR `external global`, emit `.extern`, and link across objects; a later definition in the same translation unit replaces the declaration. See [ADR 0021](docs/decisions/0021-external-globals.md).
- `src/linker/kro_object.*` writes and reads KRO objects deterministically, sorts symbols, remaps relocation indices, validates magic/version/bounds/CRC, and round-trips a section/symbol/relocation fixture.
- `src/asm/kro_writer.*` converts the in-tree assembler result (including external relocations) into a KRO object; `krc -c file.c -o file.kro` emits one.
- `src/linker/linker.*` merges sections, resolves local/global/weak symbols, patches absolute and PC-relative relocations, validates target/ABI/pointer width, and writes a KR32 `.k32` executable with sections, symbols, strings, and CRC32.
- `krld objects... -o image` reads KRO files, links, and writes the K32 executable. Unit tests cover resolution, relocation patching, entry-symbol errors, KRO round-trips/CRC/determinism, and K32 header emission.
- `krc_tests` has 86 doctest cases; all six configurations pass 20/20 CTest.
- `targets/kr32/runtime/crt0.s` provides `_start`, `putchar`, and `exit`; `targets/kr32/runtime/libc.c` provides `puts` and integer/string `printf`.
- Remaining M3 hardening: weak/COMDAT, section GC, relaxation. `krld` extracts GNU/BSD KRO archive members, writes deterministic maps/section sizes, and honors NAME/ADDRESS layout placements.

**M4 evidence (2026-11-09T22:19:21-08:00, in progress):**

- [ADR 0006](docs/decisions/0006-recursive-aggregate-types.md) adds `struct_forward`/`complete_struct` to the IR type context so self-referential C records can be constructed.
- `CIRGen` maps `struct` records by `RecordInfo`, caches the forward type, completes fields, and lowers `.`/`->` member lvalues with struct GEPs; null pointer constants now coerce correctly.
- The KR32 backend computes recursive aggregate layout/alignment and lowers struct-member GEPs with field offsets.
- `tests/fixtures/c/struct.c` links/runs on KR32 and exits 5; the unit suite covers recursive struct IR and assembly.
- [ADR 0007](docs/decisions/0007-varargs-base-opcode.md) adds `vastart`; `stdarg.h` now defines `va_list` and real `va_start`/`va_arg`/`va_copy` macros. Integer varargs functions work end to end.
- `targets/kr32/runtime/libc.c` supplies integer/string `printf`, `puts`, a free-list `malloc`/`free`/`calloc`/`realloc`, `_heapwalk`, and string/memory routines; function pointers and indirect calls work through `CALL Rn`. `mainentry` prints `argc`/`argv`, the varargs fixture prints `6`, the function-pointer fixture exits 5, and the extracted `binarytree.c` prints its AVL tree and exits 0.
- `libc-test.c` runs to exit 0 with `Point size=16 y-offset=8`, `point x=7 y=3.25`, `float 3.142  exp 1.23e+04  general 0.000123`, `hex=0xff`, `heap reuse: ok`, and `heap walk=0`.
- Full `varargs.c` runs to exit 0 printing `6`, `4`, and `7` for the integer, double, and mixed cases.
- Floating-point lowering covers FPR parameter/return/argument handling, `FLOAD.W/.D`, `FSTORE.W/.D`, `FADD/FSUB/FMUL/FDIV`, `FEQ/FLT/FLE`, `FCVT.*`, FP comparisons, `%f/%e/%g/%x/%h`, and float→double varargs promotion.
- Basic-block names are uniquified per function, fixing duplicate backend labels in generated control flow.
- Local `static` variables are emitted as internal globals with zero or constant initialization, so state persists across calls; non-constant scalar initializers and object constructors (including argument-taking ones) run once behind a guard flag. The fixtures return 3, 6, 3, and 5.
- Namespace-scope objects with constructors run through `__krc_init_globals` from a generated `main` wrapper (ADR 0018); `global.get()` observes `Counter(5)` and returns 5. Destructors run through `__krc_destroy_globals` after user `main` in reverse order. Non-constant scalar global initializers run in `__krc_init_globals`; `int global = square(3)` returns 9.
- `&&`/`||` operands now become `i1` by comparing against zero; the earlier truncation made even nonzero integers false, so loops such as `while (index < count && source[index])` stopped early. The `logical_truth` and string-library fixtures cover it.
- The KR32 libc adds `strcat`, `strncpy`, `strchr`, `strstr`, `atoi`, `atol`, and `abs`; `libc_strings` builds `abcdef` and scores 5.
- It also adds `strrchr`, `strncat`, insertion-sort `qsort`, and `bsearch`; `libc_sort` sorts `{4,1,3,0,2}`, finds 3, and prints `alpha/beta 1 1`.
- The KR32 call path now passes fixed integer arguments beyond `R0`–`R3` on the stack as ADR 0001 specifies; `sum5(1,2,3,4,5)` returns 15 and `bsearch` receives its fifth argument correctly.
x formats into a caller buffer through the same formatter as `printf`; `sprintf(text, "%d:%s:%03d", 7, "ok", 5)` writes `7:ok:005` and returns 8.
- `snprintf` bounds the write while returning the untruncated length: with a five-byte buffer, `snprintf(text, 5, "%s", "abcdef")` writes `abcd` and returns 6.
- `strtol`/`strtoul` parse optional signs and `0x`/octal prefixes with a base argument; `srand`/`rand` expose the existing LCG. The conversion fixture parses `0x1f` as 31, `-42`, and octal `17` as 15.
- Destructible function-local statics register with `__krc_destroy_globals` and run after user `main`. The fixture prints `ctor`, `main`, then `dtor`.
- `__func__` lowers to the source-level function name for free functions and methods; the fixtures verify `"main"` and `"method_name"`.
- Adjacent string literals concatenate their decoded contents, including prefixed literals; `"hello" " world"` compares equal to `"hello world"`.
- Default member initializers are collected by Sema and applied by constructors or by default construction; the `int value = 5`, plain `int value = 7`, and polymorphic/derived fixtures return 5, 7, and 12. Base-level defaults are applied before derived ones, and inherited data members resolve through the base record.
- The KR32 libc adds `strspn`, `strcspn`, `strpbrk`, and `strtok`; the tokenizer fixture splits `alpha,beta;gamma` into three tokens. Unsized `char text[] = "..."` now infers its length from the literal and copies the bytes.
- Global string arrays use the same inference and emit a constant byte array; `char text[] = "hello"` reports length 5.
- Braced global aggregate initializers run from `__krc_init_globals` before user `main`; `struct Point point = {3, 4}` returns 7.
- Global arrays with braced initializers infer unsized lengths and initialize before `main`; `int values[] = {1,2,3}` plus a fixed four-element array returns 15.
- Global pointer scalars and pointer arrays initialize from string literals before `main`; the fixture checks `name` and two array entries with `strcmp`.
- Function-local static arrays and structs initialize once behind a guard; `static int values[] = {1,2,3}` and a static `Pair {4,5}` sum to 15.
- Compound literals allocate a temporary and initialize it in place; `(struct Point){3,4}` returns 7 when copied into a local.
- Conditional expressions use their inferred result type instead of always `int`, preferring a double/float/pointer branch from either side; this fixed `fabs(-3.5)` returning 3.0 and pointer results when the null branch comes first.
- The KR32 libc adds `fabs`, `floor`, `ceil`, and Newton-iteration `sqrt`; the math fixture prints `4.0 -2.0 2.0 3.5`.
- It also adds overlap-safe `memmove`, `memchr`, and `strnlen`; the memory fixture shifts within a buffer and prints `aabcd l 3`.
- Explicit template arguments on a function call are consumed by the parser, so `maximum<int>(3,4)` reaches the int-bound template body and returns 4; deduction and substitution remain deferred.
- `new` of a class with no user constructor applies default member initializers; `new Value()` observes `number == 5`.
- `new[]` applies those defaults to each element; a two-element `Value` array sums to 10.
- `new T(arguments)` passes constructor arguments with promotion; `new Counter(5)` stores 5.
- Default construction calls the zero-argument constructor of class-type members, so an `Outer` member `Inner` initializes to 5.
- `new Outer()` applies the same member construction, so its `Inner` member also holds 5.
- Namespace-scope objects without a user constructor construct their class-type members through `__krc_init_globals`; `Outer global` observes 5.
- Global destruction calls class-type member destructors when the outer class has no destructor; the fixture prints `main` then `inner`.
- Block-scope objects get a synthesized `__krc_member_dtor_<class>` cleanup, so a local `Outer` destroys its `Inner` member when the block exits.
- `delete` of an object whose class has no own destructor calls the synthesized member destructor before freeing; `delete new Outer()` prints `inner`.
- `delete[]` uses the synthesized member destructor for each element; a two-element `Outer` array prints `inner` twice.
- Floating compound assignments emit `fadd`/`fsub`/`fmul`/`fdiv` instead of integer operations, which unblocked the libc parser.
- The KR32 libc adds `strtod` with sign, fraction, and decimal exponent; it parses `-3.25e2tail` as `-325.00` with `end` at `tail`.
- User-defined constructors default-construct class-type members that have neither an explicit initializer nor a default member initializer; an `Outer(){}` with `Inner inner` observes 5.
- `emit_implicit_member_lvalue` prefers the IR record field type, so class-type member lvalues carry struct type instead of a fallback integer.
- A user-defined constructor calls the zero-argument constructor of its base class when the initializer list does not name it; `Derived(){}` observes `Base`'s 5.
- Synthesized member destructors also call the base-class destructor after members; a `Derived` object prints `base` at block exit.
- Global destruction calls the base-class destructor after the derived destructor and member destructors; a `Derived global` prints `main`, `derived`, `base`.
- The KR32 libc adds `atof`, `strtof`, and `llabs`; a fixture prints `2.50 1.25 7` and exits 0.
- Method bodies are generated before free-function definitions, so destructor bodies exist when block-scope cleanups are synthesized.
- A synthesized `__krc_member_dtor_<class>` now calls the class's own destructor first, then members, then the base; a local `Derived` prints `derived` then `base`.
- `delete` of a non-virtually-dispatched object calls that complete destructor, so `delete new Derived()` prints `derived` then `base`.
- `delete[]` uses the same complete destructor per element for non-virtual classes; a two-element `Derived` array prints `derived`, `base` twice.
- A virtual destructor slot points at the complete member-destructor wrapper, so `delete` through a `Base*` runs both `Derived` and `Base` destructors. Implicit default constructors are synthesized for classes with base/member constructors or polymorphism, so stack and `new` construction run base and member constructors exactly once. `delete` and `delete[]` on null are no-ops, and KR32 ICmp labels are unique per function so repeated comparisons cannot jump into an earlier block. In C++ mode, `struct` tags are class names for construction syntax, so `Box box(7)` uses the constructor. Default member initializers accept braces, so `Value member{7}` constructs the member with its constructor. Only virtual calls on `this` inside a constructor or destructor bind to the current class; calls on other objects still dispatch dynamically. A linked fixture calls an overridden method through a global base pointer from a destructor and gets the derived result. Base/member default initialization emission is restricted to constructors, so a destructor no longer calls the base constructor.
- Global objects whose base class has a zero-argument constructor run it through `__krc_init_globals`; `Derived global` observes `Base`'s 5.
- Block-scope default construction runs the base-class zero-argument constructor before member initialization; a local `Derived` observes `Base`'s 5.
- `new Derived()` also runs the base-class zero-argument constructor and starts member fields at the base offset.
- Global object initialization offsets derived member fields past the base fields; `Derived global` with a base value 5 and an extra member 7 sums to 12.
- C11 `_Generic` selects the matching association by the inferred control type, comparing pointer element types and record tags, and falls back to `default`; the fixture selects 3 for `int`, 7 for `double`, and 9 for `int *`.
- `_Generic` also infers a cast expression's type, so `_Generic((double)integer, double:11)` selects 11.
- `decltype(expression)` infers arithmetic types from locals, integer/float literals, casts, and binary operands; `decltype(integer + 1)` is `int` and `decltype(real * 2.0)` is `double`.
- `printf` `%f` now honors width, left alignment, `+`, space, and zero padding; the fixture prints `[    3.50][3.50    ][   +2.3][00001.50]`.
- `goto` and labels lower through pre-created label blocks; forward and backward jumps work, forward dead code is skipped, and jumping across a live C++ cleanup scope is rejected.
- `switch` lowers to an ordered comparison chain with case blocks, default, fallthrough, and `break`; the fixture checks all four forms.
- Direct `volatile` local and global loads, stores, scalar initialization stores, and object members accessed with `.` or a direct pointer identifier set the IR flag; qualifiers through complex pointer and reference expressions are not fully propagated.
- `krc_tests` has 101 cases / 615 assertions; all six configurations pass 460/460 CTest.

### M4 — C core + debug format (complete locally)

- [x] Structs, pointers, arrays, function pointers, bit-fields, varargs, preprocessor edge cases. Pointers, arrays, recursive named structs, byte-offset member access, struct layout/GEP/load/store, by-value aggregate parameters split across integer registers and copied through aggregate loads/stores, aggregate returns of up to 8 bytes in `R0`/`R1`, bit-field load/store and signed extraction, positional and nested designated aggregate initializers, function pointers/indirect calls, integer and floating-point varargs, `offsetof`, integer/string/float `printf`, and a free-list allocator/string library are implemented.
- [x] KR32 `printf` integer/string/float paths and allocator. Integer/string/`%c`/`%x`/`%o`/`%p`, `l`/`z` length modifiers, width/zero/left alignment, integer precision, `+`/space signs, string precision, `%f`/`%e`/`%g`, `puts`, and a reusable free-list allocator work; the G3 formatting fixtures pass.
- [x] `KDBG` writer/reader: sources, lines, functions, symbols, types, variables, and build records are implemented in `src/debug/kdbg.*`, emitted by `krld`, and checksum-validated.
- [x] Freeze `docs/symbols.md` before the KDBG writer. [ADR 0009](docs/decisions/0009-kdbg-v1-freeze.md) freezes the `.kdbg` v1 payload.
- [x] `krc --dump-symbols` text/JSON dumper and shared reader/writer. `krld` builds `.kdbg` from `.debug_info`; text and JSON dumps are available.
- [x] DAP/GUI debug session: breakpoints, call stack, locals/globals, aggregate expansion, memory. The ADR 0008 `.debug_info` bridge feeds the KR32 GUI/DAP debugger; the user source line, breakpoint, frames, variables, globals, and memory are visible.
- [x] Gate G3: Tier 1 C on KR32 (`binarytree.c`, `libc-test.c`, `varargs.c`). All three compile, link, and run on KR32; `libc-test` and `varargs` exit 0 with the expected formatted values.
- [x] Gate G4: KR32 debugger session on `binarytree.c`. DAP verifies a source breakpoint at line 183, stops at `main`, returns locals `root`/`height`, evaluates `root`, reads memory, and expands a struct fixture.
- The backend emits `.debug_info` line/function/local/global/type records through [ADR 0008](docs/decisions/0008-debug-info-bridge.md); `krld` merges and renumbers source IDs. The KR32 GUI renders source, disassembly, registers, memory, locals/globals, and the call stack.
- Bit-fields lower through Sema byte offsets and bit offsets: member access uses byte GEP, loads mask/sign-extend, and assignments do a shifted read-modify-write. The bit-field fixture returns 19.
- `goto` and labels lower through pre-created label blocks; forward and backward jumps work, forward dead code is skipped, and jumping across a live C++ cleanup scope is rejected.
- `switch` lowers to an ordered comparison chain with case blocks, default, fallthrough, and `break`; the fixture checks all four forms.
- Direct `volatile` local and global loads, stores, scalar initialization stores, and object members accessed with `.` or a direct pointer identifier set the IR flag; qualifiers through complex pointer and reference expressions are not fully propagated.
- `krc_tests` has 101 cases / 615 assertions; all six configurations pass 460/460 CTest.

### M5 — C++ core (8–10 weeks)

- [ ] Freeze the C++/EH ABI in `docs/abi.md` before codegen. The class data-layout subset is frozen in [ADR 0010](docs/decisions/0010-cxx-class-data-layout.md); polymorphic vptr layout is added by [ADR 0013](docs/decisions/0013-dynamic-dispatch.md); EH and function ABI remain.
- [ ] Classes, access control, constructors/destructors, copy/move, assignment. Data-only `class` declarations, access labels, inline methods, recursive calls, default/parameterized/overloaded constructors, user-defined copy constructors/assignment, rvalue references, move constructors and move assignment via explicit casts and `std::move`, member `operator[]` including reference-returning and inherited forms, `operator()` including inherited forms, unary operators, distinct prefix/postfix increment/decrement operators, and compound assignment operators, constructor member-initializer lists and delegating constructors, reference-returning assignment via `return *this`, chained assignment results, block/function-scope destructors, default copy, and scalar `new`/`delete` work; general rvalue overload ranking remains.
- [ ] Single inheritance, virtual functions, abstract classes, virtual destructors, vtables. Single inheritance, pure virtual declarations with null base slots and derived overrides, override methods, `override`/`final`/`noexcept` on destructor declarations, closed-world static devirtualization ([ADR 0011](docs/decisions/0011-m5-static-devirtualization.md)), vptr-based dynamic dispatch through a base pointer or reference, unqualified virtual calls on `this` from base methods, and virtual destructors called by `delete base_pointer` or per element by `delete[]` ([ADR 0013](docs/decisions/0013-dynamic-dispatch.md)) work; multiple inheritance and RTTI remain.
- [ ] Namespaces, references, default arguments, `explicit`, operator overloading. Flat namespaces with nested definitions, chained qualified `outer::inner::name` lookup, `using Alias = Type;`, trailing default arguments, explicit constructors, `noexcept`/`final`, reference variables/parameters, reference data members bound in constructor initializer lists, and member/free `operator+`/`operator-`/`operator==` work; the remaining operators and rvalue/move semantics remain.
- [x] `new`/`delete`, `new[]`/`delete[]`, placement new. Scalar and array `new`/`delete` lower through `malloc`/`free`; array allocations carry a length cookie, construct each element, and destroy each element on `delete[]`; placement `new (storage) T` constructs at a supplied address.
- [x] Gate G5: `hello.cpp` and `balanced-tree.cpp` run on KR32. `hello.cpp` returns 42; `balanced-tree.cpp` prints both sorted trees and exits 0 via ADR 0011 static devirtualization; ADR 0013 adds vptr-based dispatch through a base pointer.

**M5 evidence:** class data, inline methods, constructors, `new`/`delete`, recursive calls, single inheritance, pure virtual declarations, and override methods compile and run. The `hello.cpp` fixture returns 42; `balanced-tree.cpp` prints the sorted engineer and integer trees and exits 0. CTest is 87/87 across all six configurations; `krc_tests` remains 92 cases / 509 assertions.

### M6 — C++11/14/17 (8–10 weeks)

- [ ] Function/class templates, partial specialization, implicit instantiation, weak/COMDAT. `template <typename T>` function syntax is accepted by binding template parameters to `int`; `inline`/`constexpr` functions and in-class methods emit weak symbols through [ADR 0012](docs/decisions/0012-weak-linkage.md), so translation units may share definitions. Real deduction, substitution, specialization, class templates, COMDAT groups, and section GC remain.
- [ ] Lambda captures, move/rvalue references, `auto`/`decltype`, range-for, `nullptr`, `enum class`. `auto` locals and return types, `decltype` locals, `nullptr`, range-for over bounded arrays and braced initializer lists, scoped `enum class`, `Enum::Value` qualification, no-capture lambdas with parameters and deduced return types, lambda by-value, by-reference, init, and default `[=]`/`[&]` captures with closure objects, C++ `static_assert`, `static_cast`/`reinterpret_cast`/`const_cast`, braced class initialization, and braced aggregate initialization work; general rvalue references remain.
- [ ] C++11 `constexpr` plus C++14 relaxed `constexpr`, `initializer_list`, `noexcept`, `alignas`, static `thread_local`. `constexpr` variables and multi-statement functions with `if` branches, local declarations, assignment, bounded `for`/`while`/`do` loops, and recursive calls are evaluated at compile time; `initializer_list` and `thread_local` remain.
- [ ] `if constexpr`, structured bindings, inline variables; fold expressions with variadic templates in v1.1. `if constexpr` discards the unselected branch during analysis and lowering, structured bindings over struct fields and arrays and inline variables work; fold expressions and variadic templates remain. `true`/`false` lower as 1/0.
- [ ] Extracted C++ fixture suite and negative diagnostics.
- [ ] Gate G6: C++11/14/17 core fixtures pass on KR32.

### M7 — KR16 backend + C (6–8 weeks)

- [ ] KR16 target descriptor: IP16 data model, registers, encodings, relaxation, capabilities. The IP16 profile (16-bit `int`/`long`, 16-bit pointers, unsigned `char`, no `long long`/FP) drives `-fsyntax-only` and `--dump-ir`; register mapping, encoding, and relaxation remain.
- [ ] KR16 ABI appendix and calling-convention suite.
- [ ] Codegen, frame layout, image writer. Scalar codegen, SP-relative frame layout, the in-tree assembler, global arrays, and the minimal `.kex` writer work for single-unit integer/pointer code; relocatable objects and the full linker remain.
- [ ] Tier 0–1 C on KR16, including `hello.c` and `sum.c`. `sum.c` runs to exit 55, `hello.c` prints `Hello, KR16!` and exits 0, and a global-array fixture exits 6 through the single-unit `.kex` path; the relocatable object/linker and broader C runtime remain.
- [x] Gate G7: KR16 C is accepted, assembled, linked, and run. Linked `hello.c` prints `Hello, KR16!` and exits 0; `sum.c` exits 55; a two-object `lib`/`main` image returns 42, and local/nested/array struct copies return 81.
- A type the IP16 profile cannot represent is rejected where it is named, as `'<type>' is not supported by the kr16 profile` (plan.md §4.3), instead of being lowered to a zero-width integer or to `f32`/`f64`: `krc_kr16_long_long_rejected` covers `long long` and `krc_kr16_float_rejected` covers `float`, `double`, and `long double`, and the same sources still compile for KR32. `<stdlib.h>`, `<math.h>`, and `<time.h>` omit their 64-bit and floating-point declarations under `__kr16__`, so including them stays usable on the profile.

**M7 evidence (2026-11-10T10:36:06-08:00, in progress):**

- `krc --target=kr16 -std=c11 -fsyntax-only` accepts a fixture asserting 16-bit `int`/`long`/pointers and unsigned `char`.
- `krc --target=kr16 --dump-ir` emits `; target: kr16` with `i16` function signatures.
- `src/asm/kr16_encoder.*` encodes the R/I/M/M8/B/S/BIT/C/X formats, relative control transfers, shifts, bit operations, and range validation; golden tests cover the ISA examples and out-of-range rejection.
- `src/asm/kr16_assembler.*` assembles the backend subset: labels, `.text`/`.data`/`.global`/`.extern`, register and immediate ALU forms, loads/stores/LEA, shifts, bit operations, conditional branches, relative and register `JMP`/`CALL`, and the control instructions. An unresolved or unpaired label is rejected.
- KR16 GEP lowers constant array/struct indices and nonzero byte offsets, and zero-initialized globals are emitted as data; `kr16_data.kex` runs to exit 6 on the KR16 VM.
- KR16 string literals use byte loads/stores (`LDB`/`STB`), and extended `MOV` immediates are sized as two words; `kr16_string.kex` checks `"hi"` and exits 0 on the KR16 VM.
- KR16 dynamic GEP indices scale by the element size and add to the base pointer; `kr16_index.kex` indexes a global array with a runtime variable and exits 3.
- KR16 `switch` functions work after handling `ConstantUndef` return values; `kr16_switch.kex` returns 15 on the KR16 VM.
- KR16 per-function compare labels are unique, so a callee with `if`/`switch` can be called repeatedly from a loop; `kr16_call_loop.kex` returns 15 on the KR16 VM.
- KR16 assembles `MUL`/`DIV`/`DIVU`/`MOD`/`MODU`; `kr16_arith.kex` computes `(7*6)+(20/4)+(20%6)` and exits 49 on the KR16 VM.
- KR16 assembles register-count `SHL`/`SHR`/`SAR`; `kr16_shift.kex` exercises left, right, and complement operations and exits 32 on the KR16 VM.
- KR16 emits scalar global initializers as data; `kr16_pointer.kex` updates a global through a pointer and exits 7 on the KR16 VM.
- Multidimensional array subscripts decay intermediate arrays to pointers; `matrix2d.c` returns 3 on both KR32 and KR16.
- KR16 indirect calls use `CALL R0` and function values lower to `MOV R0, #name`; `kr16_fnptr.kex` calls through a local function pointer and exits 5.
- Pointer subtraction converts both operands to integers, subtracts, and divides by the element size; `kr16_ptrdiff.kex` returns 3.
- Bit-field aggregate initializers now use shifted read-modify-write stores; `bitfields_init.c` returns 22 on both KR32 and KR16.
- Flat designated bit-field initializers reuse the same read-modify-write path; `bitfields_designated.c` returns 22.
- Nested designated bit-field paths also reach the read-modify-write path; `bitfields_nested.c` returns 22.
- `krc -c --target=kr16 --emit-object` writes a KRO object carrying the text section and symbols; relocations and the KR16 linker path remain.
- The object path accepts `.s` input; `targets/kr16/runtime/crt0.s` assembles to an object with an external `main` relocation.
- KR16 calls lower through `MOV R0, #callee` and `CALL R0`; external absolute references become 16-bit relocations in the KRO object, covered by a unit test.
- `src/backend/kr16_backend.*` emits scalar integer/pointer functions: SP-relative frame slots, parameters in `R1`–`R3`, two-operand ALU forms, loads/stores, `LEA`, comparisons, branches, calls, and `ret`. `sum.c` assembles and runs locally to exit 55.
- [ADR 0016](docs/decisions/0016-kr16-scalar-abi.md) and D-040 freeze the M7 scalar calling convention: `R1`–`R3` arguments, `R0` return, `R4` FP, SP-relative frame.
- [ADR 0017](docs/decisions/0017-kr16-exec-image.md) and D-041 define the minimal `.kex` image: 64-byte header, `.text` at `0x0100`, `.debug_str`, CRC32, and a generated `_start` that calls `main` and halts. `krc -c --target=kr16` writes it.
- `sum.c` now compiles to assembly, assembles in-tree, writes a `.kex`, and runs on the KR16 VM to exit 55 without the external toolchain.
- `hello.c` prints `Hello, KR16!` and exits 0: string globals are emitted as `.byte` text data, `LEA` resolves their labels, and the generated image includes a minimal MMIO `puts` shim.
- Constant folding now applies integer cast width and signedness, so `(char)-1 > 0` is true on the unsigned-char KR16 profile.
- [ADR 0019](docs/decisions/0019-kr16-object-linker.md) extends D-041: `--emit-object` writes KRO objects, external absolute labels get `Abs16` relocations, calls use `MOV R0,#callee; CALL R0`, and `krld` writes a KR16 EXEC with a separate load base and entry.
- The linked path runs `hello.c` (`Hello, KR16!`, exit 0), `sum.c` (exit 55), a two-object `lib`/`main` image (exit 42), local/nested/array struct copies (exit 81), and a cross-object external variable (exit 7). KR16 definitions emit `.global` and external declarations emit `.extern`.
- CTest is 118/118 across all six configurations; `krc_tests` is 98 cases / 585 assertions.

### M8 — v1.0 hardening (6–8 weeks)

- [ ] Full Tier 0–2 corpus green on KR32 and Tier 0–1 on KR16.
- [ ] Debugger parity for C and C++. Deterministic outputs are covered for KR32 objects, KR16 `.kex`, KR16 KRO objects, and KR16 linked images.
- [ ] Finish `docs/abi.md`, `object.md`, `symbols.md`, `ir.md`, `attributes.md`, target profiles. The KR16 scalar ABI appendix is written in `docs/abi-kr16.md`; aggregate/varargs/extra-argument rules and the remaining documents are open.
- [ ] Release notes and tag `v1.0`.
- [ ] Gate G8.

**M8 evidence (2026-11-11, in progress):**

- Deterministic double-build checks cover KR32 objects, single-unit KR16 `.kex`, KR16 KRO objects, and linked KR16 images.
- Executed tests run a linked image under a KR32 VM and compare its exit status, and optionally its output, so a defect that only appears when generated code runs is caught by CTest. `krc_run_longlong_arith` and `krc_run_longlong_conditional` expect exit 0; `krc_run_printf_long_long` also expects the text `-123 456 1c8`, because a truncated conversion still exits 0. The VM path is the `KRC_KR32_VM` cache variable, adopted automatically on a Windows host when the sibling checkout is present; without it the run tests are not registered, so the suite still passes where `../kr32` is absent. Asking for a deliberately wrong status fails with `expected exit 3, got 0` and a wrong output with `expected output matching ...`, so the checks are not vacuous.
- [ADR 0020](docs/decisions/0020-kdbg-source-path-policy.md) keeps KDBG v1 and defines `krld --source-root=DIR`; the runtime link helper passes the project root, and `krc_debug_paths_relative` verifies hello records `tests/fixtures/c/hello.c` with no absolute root.
- Corpus measured under the VM (`../kr32/examples`, `krc -c` then `krld` with `libkrcrt.kro`, `fileio.s`, `crt0.s`): 13 of 15 examples compile, link, and run. Exit codes as each program returns them: `hello.c` 0, `strings.c` 0, `types.c` 0, `varargs.c` 0, `libc-test.c` 0, `floats.c` 0, `fileio.c` 0, `mainentry.c` 0, `cxx_libc.cpp` 0, `hello.cpp` 42 (`point.sum()`), `cxx_tls.cpp` 42 (the `long long` division's expected value). Two known blockers still fail to compile: `FileOpen.cpp` on `implicit declaration of function 'sys_open_dialog'`, and `matrix.cpp` on `expression is not an lvalue` at line 66.
- **Two corpus programs halt with a wrong status and no printed tree:** `binarytree.c` exits 115 and `balanced-tree.cpp` exits 36, although both `main`s end in `return 0` and the emitted assembly ends `MOV R0, #0` before `JMP .Lmain.ret`. Narrowed to the AVL balance path:
  - Ruled out: a synthesised `HALT` in the generated code (none), the return-value emission, the recursion plus `putchar` indentation plus `printf("%s\n")` shape (a reduced program prints correctly and exits 0), the exit status surviving `printf`/`malloc`/`strcmp` (it does), heap exhaustion (the arena is 0x00020000–0x08000000), and a hand-written 26-node AVL tree of the same shape (`malloc` + `strdup` + rotations + indented print), which prints correctly and exits 0.
  - Bisected on reduced copies of the real program: 20 inserts print correctly and exit 0; 22, 24, 26, 28, and 30 inserts print no tree and return garbage statuses (8, 176, 144, 0, 115).
  - With `return balance_node(root);` replaced by `return root;` in the same copies, all three behave correctly — a plain BST of that input is unbalanced, so the program prints `tree is not balanced` and exits 1.
  - What it is not: `print_tree` reads `node->name` correctly. Replacing that print with a string literal in a 22-insert copy still stops after four right descents and one literal, and `check_balanced` completes first (the header prints), so the tree pointers validate before printing begins. A stand-alone recursive printer of the same shape runs to depth 7 (131 lines of output) and exits 0.
  - **The halt PC is `putchar`, not padding.** With a `krld --map` file the failing image's halt address `0x3CB8C` is exactly the `putchar` symbol; `.text` runs to `0x3CBB8`. An earlier note here claimed the halt was in padding past the end of `.text` — that came from my own arithmetic slip (`.text` is 183224 bytes from `0x10000`, ending at `0x3CBB8`, not `0x3CB78`), and it is withdrawn.
  - **`kr32 dis` is not reliable evidence for these images.** Disassembling at `putchar` with `--base 0x10000` yields `0x00000000` words, which contradicts both the map and `crt0.s` (`PUSH R0; SYSCALL #33; POP R0; RET`), so the tool is misreading a non-flat image and must not be used to infer control flow.
  - **`putchar` itself is sound.** A stand-alone program calling `putchar` in a loop inside a recursive function to depth 10 prints correctly and exits 0, so the console write path is not broken in general; earlier probes used `printf(" ")` and simply missed this path.
  - **It is a krc code-generation defect, not a VM or program defect.** The same reduced source built with the sibling VM's own C compiler — `kr32.exe build bt22.c -o bt22_vmcc.k32` — prints the whole tree (28 lines) and exits 0. That makes `../kr32` a working oracle for diffing the two outputs.
  - **No frame access leaves its own frame.** A static scan of the generated assembly checks every `[R7 + N]` use against the enclosing function's `ADJSP #-frame` size and finds no offset at or beyond the frame boundary, so the corruption is not a simple out-of-frame store.
  - Recorded as a known defect with reduced reproducers (`bt20.c` passes, `bt22.c` fails, both derived from `binarytree.c` by trimming its insert list) and a reference build for comparison.

### M9 — v1.1 runtime/libc/host services (8–10 weeks)

- [ ] Port KR32 `libc.c` (allocator, stdio, string, `strto*`, `qsort`, file I/O). The allocator, stdio/string/conversion, `qsort`/`bsearch`, and KR32 host-service file wrappers are present; broader file APIs and error mapping remain.
- [ ] Standard `printf` with length modifiers and `%p`; float formatting on KR32. `%p`, `%ld`/`%lu`/`%zu`, unsigned values, `%o`, width/zero/left alignment, integer precision, `+`/space signs, `*` width/precision, and `%.Ns`/`%Ns` strings work; `%f`/`%e`/`%g` with explicit precision are covered by linked fixtures; the locale-aware set remains.
- `atexit` registers handlers that run at normal return and at `exit`; a linked fixture returns 7. `_Exit` terminates without running them; a linked fixture returns 7. `at_quick_exit` handlers run from `quick_exit`; a linked fixture returns 7. `getenv`/`setenv`/`unsetenv` maintain a runtime table; a linked fixture returns 0.
- [ ] Common host-service ABI and per-VM shims; kernel profile.
- [ ] Integrate KR16 `libc.s`/`resources/fileio.c`. A C string/memory runtime (`strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strchr`, `strrchr`, `strstr`, `memcpy`, `memset`, `memcmp`, `atoi`, `strtol`, `strtoul`, `abs`, `putchar`, `malloc`, `free`, `calloc`, `realloc`, `strdup`, `rand`, `srand`, `strtok`) builds to `libkrcrt.kro` and links with the startup object; file I/O and the existing assembly libc remain.
- [ ] Gate G9 partial: Tier 3 runtime baseline.

**M9 evidence (2026-11-11, in progress):**

- `printf` parses the `ll` length modifier and routes `d/i/u/o/x/X` through a widened formatter; the formatter now receives both words of a 64-bit argument instead of a truncated one.
- KR16 `puts` terminates its string with a newline, and the runtime provides `putchar`/`getchar` over the `0xF000`/`0xF004` console ports.
- `targets/kr16/runtime/libc.c` builds to `stage/sysroot/kr16/lib/libkrcrt.kro` through `krc --emit-object`.
- A startup + runtime + user-image link runs the string, conversion, allocation, random, and tokenizer functions and exits 3.
- KR16 unconditional branches now use `JMP`; conditional branches invert and jump through a short local branch, so large functions assemble without branch-range failures.
- KR16 aggregate/array allocas reserve their full type size in the frame; previously every local reserved one two-byte slot and overlapping locals could corrupt arrays.
- KR32 `sscanf` supports `d`/`i`/`u`/`x`, `s`, `c`, width limits, `*` suppression, and `l` modifiers.
- Character and string escapes include `\a`, `\b`, `\f`, `\v`, `\?`, octal, and `\xNN` hexadecimal forms.
- KR32 string coverage includes `strerror_r`, `memmem`, `strsep`, `strtok_r`, `strndup`, `strcoll`, `strxfrm`, and `memccpy`.
- KR32 math provides `fabs`, `fmin`, `fmax`, `sqrt`, `floor`, and `ceil`.
- KR32 ctype provides `isalnum`, `isalpha`, `isblank`, `iscntrl`, `isdigit`, `isgraph`, `islower`, `isprint`, `ispunct`, `isspace`, `isupper`, `isxdigit`, `tolower`, and `toupper`.
- KR32 stdlib provides `labs`, `atoll`, `strtoll`, `strtoull`, and `_Exit` alongside the existing conversion and allocation functions.
- `<locale.h>` provides the C locale through `setlocale` and `localeconv`.
- `<strings.h>` provides `strcasecmp` and `strncasecmp`.
- `<assert.h>` enables `assert` unless `NDEBUG`, routing failures through `__krc_assert_fail`.
- `<unistd.h>` exposes `open`, `close`, `read`, `write`, `seek`, `unlink`, `dup`, `dup2`, `truncate`, `stat`, `getcwd`, and `chdir` over the host services.
- KR32 time support provides `time`, `difftime`, `clock`, `gmtime`, `localtime`, `mktime`, `strftime` (civil date, named fields, and `%F`/`%T`/`%R`/`%x`/`%c` composites and `%Z`/`%z` UTC fields, and `%C`/`%D`/`%h`/`%n`/`%t`), `asctime`, and `ctime` over the host time service.
- KR32 `strerror` maps the runtime errno values, and `perror` writes an optional prefix plus the current error text to the console.
- KR32 stdio provides `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fflush`, `fgets`, `getline`/`getdelim`, `getc`/`fgetc`/`ungetc`/`fputc`/`putc`/`fputs`, `fprintf`/`vfprintf`/`dprintf`/`vdprintf`, `fscanf`/`vfscanf`/`vsscanf`/`scanf`, `setbuf`/`fgetpos`/`fsetpos`/`freopen`, `tmpfile`/`tmpnam`/`fdopen`/`fileno`, `remove`, and `stdin`/`stdout`/`stderr` stream access over the host file wrappers.
- [docs/host-services.md](docs/host-services.md) records the KR32 wrapper layers, vectors, flags, errno mapping, sandbox, and the KR16 `SYS_VERSION`-only status.
- `sysroot/include/krc/host.h` declares the KR32 host-service wrappers and open flags; C wrappers map negative host results to `-1` and set `errno` through `__krc_errno_location`.
- `targets/kr32/runtime/fileio.s` provides `getchar`, `open`, `close`, `read`, `write`, `seek`, `unlink`, `rename`, `stat`, `truncate`, `getcwd`, `chdir`, `dup`, `dup2`, `sys_time`, `sys_argc`, and `sys_arg` over the KR32 host-service vectors; the linked file I/O fixture writes, renames, and unlinks a file and exits 0.

### M10 — v1.1 language/runtime completion (8–10 weeks)

- [ ] Table-driven exceptions and unwinding; RTTI/`dynamic_cast`/`typeid`.
- [ ] Variadic templates and fold expressions.
- [ ] Dynamic `thread_local` initialization/destruction.
- [ ] `memory`, `tuple`, `functional`, `array`.

**M10 evidence (in progress):**

- Static data members: `static int S::value` definitions parse and mangle to a global, and `S::value` reads/writes resolve from function scope. A linked fixture sets 9 and returns 9.
- Out-of-line member definitions such as `int S::get() { ... }` attach to the class method table and are callable through an instance. Linked fixtures return 7 and 3 for an argument-taking method. Out-of-line constructors (`S::S(int) : value(input) {}`) register in the constructor table; a linked fixture returns 3. - Delegating constructors (`S() : S(7) {}`) run the target constructor; a linked fixture returns 7.
- Out-of-line method bodies with member access skip redundant Sema body analysis and use IRGen class context; a linked fixture returns 7.
- Out-of-line operators (`S &S::operator=(S const &)` and `S &S::operator+=(S const &)`) attach to the class; linked fixtures return 3 and 5.
- Out-of-line static methods with parameters (`int S::add(int)`) return through `S::add(2)`; a linked fixture returns 3.
- Out-of-line static methods (`int S::get()`) are callable as `S::get()`; a linked fixture returns 7.
- Nested out-of-line methods (`Outer::Inner::get`) attach to the nested record; a linked fixture returns 3.
- Nested static members (`Outer::Inner::value`) resolve through their qualified global; a linked fixture returns 3.
- Static member arrays (`static int values[3]` plus an out-of-class definition) index correctly; a linked fixture returns 2.
- Out-of-line virtual methods participate in the vtable and dispatch through a base pointer; a linked fixture returns 7. Out-of-line destructors (`S::~S()`) attach to the class and run at scope exit; a linked fixture observes one destruction.
- Nested class types such as `Outer::Inner` parse as declaration types and resolve to the nested record. A linked fixture sets a member to 3 and returns 3.
- Trailing `const`/`volatile`/`noexcept`/`override`/`final` method qualifiers parse, and `mutable` members are accepted. A linked fixture increments a mutable member in a `const` method and returns 2.
- `static constexpr` members, `decltype` copies, and `using` type aliases are covered by linked fixtures returning 7.
- In-class static initialization (`static const int value = 7` and `inline static int value = 7`) creates the class-qualified global and a Sema symbol. A linked fixture returns 12.
- Static member functions: `S::get()` resolves through the class method table and passes a null receiver. Unqualified references to class static data inside a static method resolve to the class-qualified global. Linked fixtures return 7.
- Range-for over arrays supports reference, `auto`, and `auto &` loop variables; linked fixtures return 6, 7, 7, and 9.
- `decltype(variable)` and `decltype(expression)` declarations infer their types; linked fixtures return 7 and 8.
- C++ `alignof` queries the alignment in expressions and `static_assert`; a linked fixture returns 0.
- Friend functions and friend classes parse and can read granting-class members; linked fixtures return 7.
- A `mutable` counter incremented in a `const` method is observable afterwards; a linked fixture returns 1.
- Move construction and move assignment selected by `static_cast<S &&>` transfer values; linked fixtures return 7.
- Pure virtual functions, pure virtual destructors, and dispatch through a base pointer work; linked fixtures return 7.
- `extern "C"` declarations and linkage blocks parse; linked fixtures return 7.
- `reinterpret_cast` round-trips pointers through `void *`, `switch` selects `enum class` cases, and `if constexpr (false)` takes the else branch; linked fixtures return 7.
- Returned temporaries materialize for member access, method calls, conversions, conditions, `&&`/`||`, `!`/`-`, arithmetic, comparisons, assignments, arguments, return values, `switch`, and reference binding; linked fixtures return 7 or 8.
- Braced `new` arrays initialize scalar and record elements, empty and partial initializers zero the remainder, deduced bounds are supported, scalar allocations initialize or value-initialize, and placement `new` selects a constructor; linked fixtures return 0, 1, 3, 4, 6, 7, or 8.
- `std::move`, `std::forward`, `std::swap`, `std::min`, and `std::max` work for the supported scalar and record types; linked fixtures return 7, 37, or 453-style sums.
- `static_cast<int &>` and `const_cast<int &>` yield mutable references to the underlying object; linked fixtures return 7.
- `sizeof(record)` is usable in `static_assert` and expressions; a linked fixture returns 7.
- Logical `&&`/`||` convert record operands through `operator bool`; linked fixtures return 7.
- A global `S const instance;` runs its constructor before `main`; a linked fixture returns 7.
- A braced return (`return {7};`) constructs the record result; a linked fixture returns 7.
- A functional braced cast (`S{7}`) calls the matching constructor; a linked fixture returns 7.
- Member access on a returned temporary (`make().value`) materializes the temporary; a linked fixture returns 7.
- A method call on a returned temporary (`make().get()`) materializes the receiver; a linked fixture returns 7.
- `new S{7}` constructs the allocation with a braced argument; a linked fixture returns 7.
- A conversion operator on a returned temporary (`static_cast<int>(make())`) materializes the temporary; a linked fixture returns 7.
- A record returned by a call is materialized for a condition (`if (make())`); a linked fixture returns 7.
- A record returned by a call is materialized for `&&`/`||`; a linked fixture returns 7.
- A returned temporary converts implicitly in an integer declaration (`int x = make()`); a linked fixture returns 7.
- A returned temporary converts implicitly in a return statement (`return make()`); a linked fixture returns 7.
- A returned temporary converts implicitly as a call argument (`take(make())`); a linked fixture returns 7.
- A returned temporary converts implicitly in an assignment (`result = make()`); a linked fixture returns 7.
- A deleted member function is rejected where it is named: `= delete` is recorded instead of being read as a pure specifier, and a member or qualified call reports `use of deleted function`. `krc_cxx_deleted_function` asserts both the rejection and the message. A deleted constructor or destructor used at a construction or destruction site is still accepted, because constructor selection happens in IR generation rather than Sema.
- Access control is enforced where Sema checks: a `class` member is private until an access specifier changes it, `struct` and `union` members are public, and touching a non-public member reports `'S::value' is private` or `is protected` unless the access comes from the declaring class, from a class derived from it (protected only), or from a declared friend. `krc_cxx_private_member_access`, `krc_cxx_protected_member_access`, `krc_cxx_out_of_line_private_access`, and `krc_cxx_in_class_private_access` assert the diagnostics; `krc_link_friend_access`, `krc_link_friend_class_access`, and `krc_link_out_of_line_protected_access` cover the allowed cases. A `friend` class declaration is recorded and grants access; it is also no longer mistaken for an anonymous member, which had been copying the friend class's fields into the declaring record (`sizeof` returned 12 for a class holding one `int`).
- In-class member bodies are analysed as well as free functions and out-of-line definitions. Three things were needed: bodies are visited after every member of the record is registered, so a body may name a member declared later in the class; a static data member is registered at class scope whether or not it has an in-class initializer, and counts as defined only when it has one; and an unqualified call inside a body resolves to a member function of the defining class or one of its bases.
- Out-of-line member bodies are analysed by Sema: the definition's class gives the body class scope, so an unqualified name resolves to a member of the defining class or one of its bases, and `this` is a local of pointer type. The nine `krc_link_out_of_line_*` fixtures that failed on `use of undeclared identifier` now pass, and the `../kr32` C++ examples compile unchanged (`FileOpen.cpp` still stops on the missing `sys_open_dialog`).
- 64-bit integers on KR32 are a low/high word pair: add, subtract, multiply, the bitwise operators, both shift directions, and comparisons lower inline; division and remainder call `__krc_udivdi3`/`__krc_sdivdi3`/`__krc_umoddi3`/`__krc_smoddi3` in `targets/kr32/runtime/libc.c`. A 64-bit argument consumes two argument words and a 64-bit return travels in R0/R1 ([ADR 0023](docs/decisions/0023-kr32-64-bit-integers.md)). Observed under the `../kr32` VM: `long long a = 210 / 5` returns 42 in R0, the case the previous handoff reported as 0, and `krc_link_longlong_arith` runs to exit 0 with a 1.6-million-step budget, so its eight checks hold at runtime. A 64-bit value also survives a fixed parameter and a return (`identity(5000000001)` compares equal), and `va_arg(ap, long long)` reads both words.
- A 64-bit value in a conditional expression keeps both words. The symptom found by the runtime run — `printf("%lld %llu", 5000000001LL, 5000000001ULL)` printing `705032705 705032705` — was not in `printf`: `printf("%llx", 0x100000001LL)` printed `100000001`, so the length modifier, `va_arg`, and `print_integer` already carried both words. The fault was the backend's `BitCast` lowering, which loaded only the low word of a wide operand and zeroed the high one; the conditional lowering inserts a `bitcast i64 ... to i64` per branch, so every 64-bit conditional truncated. `krc_link_longlong_conditional` links four branch shapes, and under the VM it exits 0; `longlong_arith` now prints `5000000001 714285714 1193046 34359738374`, all four values correct.
- A returned temporary converts for comparison against an integer (`make() == 7`) or a double (`make() > 1.0`), for `static_cast<double>`, and for arithmetic (`make() + 1`); linked fixtures return 7 or 8. `auto result = make()` infers the record type; a linked fixture returns 7.
- `!make()` applies `operator bool` to a materialized returned temporary; a linked fixture returns 7.
- `-make()` applies `operator int` to a materialized returned temporary; a linked fixture produces -7 (`4294967289`).
- A returned temporary binds to a const reference local with function-lifetime storage; a linked fixture returns 7.
- A returned temporary binds to a reference parameter (`take(make())`); a linked fixture returns 7.
- A returned temporary converts for a `switch` condition (`switch (make())`) or a `while` condition (`while (make())`); linked fixtures return 7.
- A two-argument braced record return (`return {3, 4}`) selects the matching constructor, and the same syntax initializes an aggregate; linked fixtures return 7.
- A braced array allocation (`new int[3]{1, 2, 3}`) initializes each element, nested braces initialize record arrays (`new P[2]{{1, 2}, {3, 4}}`), and an empty bound (`new int[]{1, 2, 3}`) takes its size from the initializer, including record arrays (`new P[]{{1, 2}, {3, 4}}`); linked fixtures return 6, 4, and 3.
- A scalar allocation with an initializer (`new int(7)`) stores the value, and value-initialization (`new int()`) zeroes it; linked fixtures return 7 and 0.
- An empty braced array allocation (`new int[3]{}`) zeroes every element, and a partial initializer (`new int[3]{1}`) zeroes the remainder; linked fixtures return 0 and 1.
- `std::move(first)` selects the move constructor and the move assignment operator, `std::forward<S>(first)` preserves the rvalue, `std::swap(left, right)` exchanges two scalars or two records, and `std::min`/`std::max` select an integer or a double; linked fixtures return 7 or 37.
- A braced array of class objects (`new S[2]{}`) runs each default constructor; a linked fixture returns 7.
- Placement construction (`new (storage) S(7)`) selects the matching constructor; a linked fixture returns 7.
- A call initializer is evaluated exactly once (`int x = bump()`); a linked fixture returns 7.
- A namespace-qualified type name (`ns::S object`, `new ns::S`) parses in declarations and allocations; a linked fixture returns 10.
- `thread_local` parses at namespace and block scope and uses static storage, matching the single-threaded VM; linked fixtures return 7. Per-thread dynamic TLS remains open.
- `snprintf(\"%.2f\", 3.5)` produces `3.50`, `%e` produces `1.234500e+03`, and `%g` produces `0.5`; linked fixtures return 0.
- `getenv`/`setenv`/`unsetenv` maintain a runtime environment table; a linked fixture returns 0.
- [ ] Full KR32 `libc.c`/`kr32os` and KR16 runtime integration.
- [ ] Tag `v1.1`; Gate G9.

## 5. Open items and conditions

Approval conditions: none.

Tracked open questions are in `plan.md` §17.2. The decision points that affect milestones are:

- Target-descriptor schema and C ABI frozen before M2/M5.
- KDBG portable source paths: [ADR 0020](docs/decisions/0020-kdbg-source-path-policy.md) resolves the policy; `krld --source-root=DIR` is available, the default remains the recorded path, and a v1.1 source-root table is deferred.
- KR16 ABI appendix before M7.
- EH record encoding ADR before M10.
- `libc.c` port strategy before M9.
- `printf` compatibility policy before M9.
- 64-bit VM/ISA specification before any 64-bit backend work.
- ARM64 host CI timing; third-party test-suite policy.

## Gate verification

Most recent first.

### 2026-11-13T00:49:06-08:00 — host gate (Windows)

```text
windows-msvc-debug    PASS 480/480
windows-msvc-release  PASS 480/480
linux-gcc-debug       PASS 477/477
linux-gcc-release     PASS 477/477
not verified: native Linux x64, Linux ARM64
```

The Linux rows register three tests fewer: the executed tests need a
runnable KR32 VM and are not registered where the default is a Windows
binary.

### 2026-11-13T00:36:22-08:00 — host gate (Windows)

```text
windows-msvc-debug    PASS 479/479
windows-msvc-release  PASS 479/479
linux-gcc-debug       PASS 476/476
linux-gcc-release     PASS 476/476
not verified: native Linux x64, Linux ARM64
```

The Linux rows register three tests fewer: the executed tests need a
runnable KR32 VM and are not registered where the default is a Windows
binary. The timestamp is the first Windows configure, from the CMake cache.

### 2026-11-13T00:22:24-08:00 — host gate (Windows)

```text
windows-msvc-debug    PASS 477/477
windows-msvc-release  PASS 477/477
linux-gcc-debug       PASS 474/474
linux-gcc-release     PASS 474/474
not verified: native Linux x64, Linux ARM64
```

The Linux rows register three tests fewer: the executed tests need a
runnable KR32 VM and are not registered where the default is a Windows
binary. The timestamp is the first Windows configure, from the CMake cache;
the WSL logs were lost when that VM restarted during the gate, so the Linux
rows are identified by their counts rather than their own log times.

### 2026-11-13T00:05:56-08:00 — host gate (Windows)

```text
windows-msvc-debug    PASS 475/475
windows-msvc-release  PASS 475/475
linux-gcc-debug       PASS 472/472
linux-gcc-release     PASS 472/472
not verified: native Linux x64, Linux ARM64
```

The Linux rows register three tests fewer: the executed tests need a
runnable KR32 VM and are not registered where the default is a Windows
binary.

### 2026-11-12T23:53:00-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 474/474
windows-msvc-release  PASS 474/474
linux-gcc-debug       PASS 472/472
linux-gcc-release     PASS 472/472
not verified: native Linux x64, Linux ARM64
``

The Linux rows register two tests fewer: the executed tests need a runnable
KR32 VM and are not registered where the default is a Windows binary.

### 2026-11-12T23:33:47-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 472/472
windows-msvc-release  PASS 472/472
linux-gcc-debug       PASS 472/472
linux-gcc-release     PASS 472/472
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T23:15:09-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 471/471
windows-msvc-release  PASS 471/471
linux-gcc-debug       PASS 471/471
linux-gcc-release     PASS 471/471
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T23:01:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 470/470
windows-msvc-release  PASS 470/470
linux-gcc-debug       PASS 470/470
linux-gcc-release     PASS 470/470
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T22:45:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 469/469
windows-msvc-release  PASS 469/469
linux-gcc-debug       PASS 469/469
linux-gcc-release     PASS 469/469
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T22:19:16-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 468/468
windows-msvc-release  PASS 468/468
linux-gcc-debug       PASS 468/468
linux-gcc-release     PASS 468/468
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T21:53:45-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 465/465
windows-msvc-release  PASS 465/465
linux-gcc-debug       PASS 465/465
linux-gcc-release     PASS 465/465
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T11:03:14-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 460/460
windows-msvc-release  PASS 460/460
linux-gcc-debug       PASS 460/460
linux-gcc-release     PASS 460/460
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T10:47:11-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 458/458
windows-msvc-release  PASS 458/458
linux-gcc-debug       PASS 458/458
linux-gcc-release     PASS 458/458
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T10:12:35-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 456/456
windows-msvc-release  PASS 456/456
linux-gcc-debug       PASS 456/456
linux-gcc-release     PASS 456/456
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T09:59:48-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 455/455
windows-msvc-release  PASS 455/455
linux-gcc-debug       PASS 455/455
linux-gcc-release     PASS 455/455
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T09:43:53-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 453/453
windows-msvc-release  PASS 453/453
linux-gcc-debug       PASS 453/453
linux-gcc-release     PASS 453/453
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T09:27:42-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 451/451
windows-msvc-release  PASS 451/451
linux-gcc-debug       PASS 451/451
linux-gcc-release     PASS 451/451
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T09:11:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 449/449
windows-msvc-release  PASS 449/449
linux-gcc-debug       PASS 449/449
linux-gcc-release     PASS 449/449
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T08:55:59-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 447/447
windows-msvc-release  PASS 447/447
linux-gcc-debug       PASS 447/447
linux-gcc-release     PASS 447/447
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T08:39:49-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 445/445
windows-msvc-release  PASS 445/445
linux-gcc-debug       PASS 445/445
linux-gcc-release     PASS 445/445
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T08:23:36-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 443/443
windows-msvc-release  PASS 443/443
linux-gcc-debug       PASS 443/443
linux-gcc-release     PASS 443/443
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T08:08:47-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 441/441
windows-msvc-release  PASS 441/441
linux-gcc-debug       PASS 441/441
linux-gcc-release     PASS 441/441
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T07:52:53-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 439/439
windows-msvc-release  PASS 439/439
linux-gcc-debug       PASS 439/439
linux-gcc-release     PASS 439/439
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T07:38:44-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 437/437
windows-msvc-release  PASS 437/437
linux-gcc-debug       PASS 437/437
linux-gcc-release     PASS 437/437
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T07:24:02-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 435/435
windows-msvc-release  PASS 435/435
linux-gcc-debug       PASS 435/435
linux-gcc-release     PASS 435/435
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T07:08:19-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 433/433
windows-msvc-release  PASS 433/433
linux-gcc-debug       PASS 433/433
linux-gcc-release     PASS 433/433
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T06:52:25-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 431/431
windows-msvc-release  PASS 431/431
linux-gcc-debug       PASS 431/431
linux-gcc-release     PASS 431/431
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T06:36:40-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 429/429
windows-msvc-release  PASS 429/429
linux-gcc-debug       PASS 429/429
linux-gcc-release     PASS 429/429
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T06:20:33-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 427/427
windows-msvc-release  PASS 427/427
linux-gcc-debug       PASS 427/427
linux-gcc-release     PASS 427/427
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T06:05:05-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 425/425
windows-msvc-release  PASS 425/425
linux-gcc-debug       PASS 425/425
linux-gcc-release     PASS 425/425
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T05:50:18-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 423/423
windows-msvc-release  PASS 423/423
linux-gcc-debug       PASS 423/423
linux-gcc-release     PASS 423/423
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T05:36:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 421/421
windows-msvc-release  PASS 421/421
linux-gcc-debug       PASS 421/421
linux-gcc-release     PASS 421/421
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T05:22:14-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 419/419
windows-msvc-release  PASS 419/419
linux-gcc-debug       PASS 419/419
linux-gcc-release     PASS 419/419
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T05:08:16-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 417/417
windows-msvc-release  PASS 417/417
linux-gcc-debug       PASS 417/417
linux-gcc-release     PASS 417/417
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T04:54:01-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 415/415
windows-msvc-release  PASS 415/415
linux-gcc-debug       PASS 415/415
linux-gcc-release     PASS 415/415
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T04:36:31-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 412/412
windows-msvc-release  PASS 412/412
linux-gcc-debug       PASS 412/412
linux-gcc-release     PASS 412/412
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T04:19:10-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 410/410
windows-msvc-release  PASS 410/410
linux-gcc-debug       PASS 410/410
linux-gcc-release     PASS 410/410
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T04:01:29-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 407/407
windows-msvc-release  PASS 407/407
linux-gcc-debug       PASS 407/407
linux-gcc-release     PASS 407/407
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T03:48:19-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 405/405
windows-msvc-release  PASS 405/405
linux-gcc-debug       PASS 405/405
linux-gcc-release     PASS 405/405
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T03:34:56-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 402/402
windows-msvc-release  PASS 402/402
linux-gcc-debug       PASS 402/402
linux-gcc-release     PASS 402/402
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T03:24:37-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 401/401
windows-msvc-release  PASS 401/401
linux-gcc-debug       PASS 401/401
linux-gcc-release     PASS 401/401
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T03:11:59-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 399/399
windows-msvc-release  PASS 399/399
linux-gcc-debug       PASS 399/399
linux-gcc-release     PASS 399/399
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T02:58:17-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 397/397
windows-msvc-release  PASS 397/397
linux-gcc-debug       PASS 397/397
linux-gcc-release     PASS 397/397
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T02:42:44-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 394/394
windows-msvc-release  PASS 394/394
linux-gcc-debug       PASS 394/394
linux-gcc-release     PASS 394/394
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T02:30:38-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 392/392
windows-msvc-release  PASS 392/392
linux-gcc-debug       PASS 392/392
linux-gcc-release     PASS 392/392
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T02:17:59-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 390/390
windows-msvc-release  PASS 390/390
linux-gcc-debug       PASS 390/390
linux-gcc-release     PASS 390/390
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T02:05:53-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 387/387
windows-msvc-release  PASS 387/387
linux-gcc-debug       PASS 387/387
linux-gcc-release     PASS 387/387
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T01:52:35-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 385/385
windows-msvc-release  PASS 385/385
linux-gcc-debug       PASS 385/385
linux-gcc-release     PASS 385/385
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T01:39:26-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 383/383
windows-msvc-release  PASS 383/383
linux-gcc-debug       PASS 383/383
linux-gcc-release     PASS 383/383
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T01:23:45-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 380/380
windows-msvc-release  PASS 380/380
linux-gcc-debug       PASS 380/380
linux-gcc-release     PASS 380/380
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T01:11:01-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 378/378
windows-msvc-release  PASS 378/378
linux-gcc-debug       PASS 378/378
linux-gcc-release     PASS 378/378
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T00:58:31-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 376/376
windows-msvc-release  PASS 376/376
linux-gcc-debug       PASS 376/376
linux-gcc-release     PASS 376/376
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T00:45:31-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 374/374
windows-msvc-release  PASS 374/374
linux-gcc-debug       PASS 374/374
linux-gcc-release     PASS 374/374
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T00:33:44-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 372/372
windows-msvc-release  PASS 372/372
linux-gcc-debug       PASS 372/372
linux-gcc-release     PASS 372/372
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T00:21:09-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 370/370
windows-msvc-release  PASS 370/370
linux-gcc-debug       PASS 370/370
linux-gcc-release     PASS 370/370
not verified: native Linux x64, Linux ARM64
``

### 2026-11-12T00:09:14-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 368/368
windows-msvc-release  PASS 368/368
linux-gcc-debug       PASS 368/368
linux-gcc-release     PASS 368/368
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T23:57:27-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 366/366
windows-msvc-release  PASS 366/366
linux-gcc-debug       PASS 366/366
linux-gcc-release     PASS 366/366
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T23:46:10-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 364/364
windows-msvc-release  PASS 364/364
linux-gcc-debug       PASS 364/364
linux-gcc-release     PASS 364/364
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T23:34:10-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 362/362
windows-msvc-release  PASS 362/362
linux-gcc-debug       PASS 362/362
linux-gcc-release     PASS 362/362
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T23:22:11-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 360/360
windows-msvc-release  PASS 360/360
linux-gcc-debug       PASS 360/360
linux-gcc-release     PASS 360/360
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T23:11:03-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 358/358
windows-msvc-release  PASS 358/358
linux-gcc-debug       PASS 358/358
linux-gcc-release     PASS 358/358
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T22:58:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 356/356
windows-msvc-release  PASS 356/356
linux-gcc-debug       PASS 356/356
linux-gcc-release     PASS 356/356
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T22:45:46-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 354/354
windows-msvc-release  PASS 354/354
linux-gcc-debug       PASS 354/354
linux-gcc-release     PASS 354/354
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T22:30:49-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 351/351
windows-msvc-release  PASS 351/351
linux-gcc-debug       PASS 351/351
linux-gcc-release     PASS 351/351
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T22:18:23-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 348/348
windows-msvc-release  PASS 348/348
linux-gcc-debug       PASS 348/348
linux-gcc-release     PASS 348/348
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T22:06:31-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 345/345
windows-msvc-release  PASS 345/345
linux-gcc-debug       PASS 345/345
linux-gcc-release     PASS 345/345
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T21:53:02-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 341/341
windows-msvc-release  PASS 341/341
linux-gcc-debug       PASS 341/341
linux-gcc-release     PASS 341/341
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T21:41:54-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 339/339
windows-msvc-release  PASS 339/339
linux-gcc-debug       PASS 339/339
linux-gcc-release     PASS 339/339
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T21:30:52-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 336/336
windows-msvc-release  PASS 336/336
linux-gcc-debug       PASS 336/336
linux-gcc-release     PASS 336/336
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T21:19:22-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 333/333
windows-msvc-release  PASS 333/333
linux-gcc-debug       PASS 333/333
linux-gcc-release     PASS 333/333
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T21:07:40-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 331/331
windows-msvc-release  PASS 331/331
linux-gcc-debug       PASS 331/331
linux-gcc-release     PASS 331/331
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T20:56:30-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 329/329
windows-msvc-release  PASS 329/329
linux-gcc-debug       PASS 329/329
linux-gcc-release     PASS 329/329
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T20:45:22-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 327/327
windows-msvc-release  PASS 327/327
linux-gcc-debug       PASS 327/327
linux-gcc-release     PASS 327/327
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T20:33:57-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 325/325
windows-msvc-release  PASS 325/325
linux-gcc-debug       PASS 325/325
linux-gcc-release     PASS 325/325
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T20:22:53-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 323/323
windows-msvc-release  PASS 323/323
linux-gcc-debug       PASS 323/323
linux-gcc-release     PASS 323/323
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T20:11:47-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 321/321
windows-msvc-release  PASS 321/321
linux-gcc-debug       PASS 321/321
linux-gcc-release     PASS 321/321
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:57:26-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 319/319
windows-msvc-release  PASS 319/319
linux-gcc-debug       PASS 319/319
linux-gcc-release     PASS 319/319
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:46:41-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 317/317
windows-msvc-release  PASS 317/317
linux-gcc-debug       PASS 317/317
linux-gcc-release     PASS 317/317
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:35:29-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 316/316
windows-msvc-release  PASS 316/316
linux-gcc-debug       PASS 316/316
linux-gcc-release     PASS 316/316
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:21:25-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 314/314
windows-msvc-release  PASS 314/314
linux-gcc-debug       PASS 314/314
linux-gcc-release     PASS 314/314
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:10:45-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 312/312
windows-msvc-release  PASS 312/312
linux-gcc-debug       PASS 312/312
linux-gcc-release     PASS 312/312
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T19:01:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 310/310
windows-msvc-release  PASS 310/310
linux-gcc-debug       PASS 310/310
linux-gcc-release     PASS 310/310
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T18:52:00-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 308/308
windows-msvc-release  PASS 308/308
linux-gcc-debug       PASS 308/308
linux-gcc-release     PASS 308/308
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T18:38:56-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 304/304
windows-msvc-release  PASS 304/304
linux-gcc-debug       PASS 304/304
linux-gcc-release     PASS 304/304
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T18:29:28-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 302/302
windows-msvc-release  PASS 302/302
linux-gcc-debug       PASS 302/302
linux-gcc-release     PASS 302/302
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T18:17:14-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 299/299
windows-msvc-release  PASS 299/299
linux-gcc-debug       PASS 299/299
linux-gcc-release     PASS 299/299
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T18:03:40-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 296/296
windows-msvc-release  PASS 296/296
linux-gcc-debug       PASS 296/296
linux-gcc-release     PASS 296/296
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T17:53:38-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 294/294
windows-msvc-release  PASS 294/294
linux-gcc-debug       PASS 294/294
linux-gcc-release     PASS 294/294
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T17:44:09-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 292/292
windows-msvc-release  PASS 292/292
linux-gcc-debug       PASS 292/292
linux-gcc-release     PASS 292/292
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T17:34:11-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 288/288
windows-msvc-release  PASS 288/288
linux-gcc-debug       PASS 288/288
linux-gcc-release     PASS 288/288
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T17:17:04-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 285/285
windows-msvc-release  PASS 285/285
linux-gcc-debug       PASS 285/285
linux-gcc-release     PASS 285/285
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T17:03:33-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 282/282
windows-msvc-release  PASS 282/282
linux-gcc-debug       PASS 282/282
linux-gcc-release     PASS 282/282
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T16:48:33-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 279/279
windows-msvc-release  PASS 279/279
linux-gcc-debug       PASS 279/279
linux-gcc-release     PASS 279/279
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T16:34:39-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 276/276
windows-msvc-release  PASS 276/276
linux-gcc-debug       PASS 276/276
linux-gcc-release     PASS 276/276
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T16:23:26-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 273/273
windows-msvc-release  PASS 273/273
linux-gcc-debug       PASS 273/273
linux-gcc-release     PASS 273/273
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T16:10:27-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 270/270
windows-msvc-release  PASS 270/270
linux-gcc-debug       PASS 270/270
linux-gcc-release     PASS 270/270
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T15:58:52-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 268/268
windows-msvc-release  PASS 268/268
linux-gcc-debug       PASS 268/268
linux-gcc-release     PASS 268/268
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T15:41:13-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 264/264
windows-msvc-release  PASS 264/264
linux-gcc-debug       PASS 264/264
linux-gcc-release     PASS 264/264
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T15:26:20-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 261/261
windows-msvc-release  PASS 261/261
linux-gcc-debug       PASS 261/261
linux-gcc-release     PASS 261/261
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T15:12:44-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 256/256
windows-msvc-release  PASS 256/256
linux-gcc-debug       PASS 256/256
linux-gcc-release     PASS 256/256
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T14:53:09-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 250/250
windows-msvc-release  PASS 250/250
linux-gcc-debug       PASS 250/250
linux-gcc-release     PASS 250/250
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T14:41:16-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 248/248
windows-msvc-release  PASS 248/248
linux-gcc-debug       PASS 248/248
linux-gcc-release     PASS 248/248
not verified: native Linux x64, Linux ARM64
``

### 2026-11-11T14:17:51-08:00 — host gate (Windows)

``text
windows-msvc-debug    PASS 247/247
windows-msvc-release  PASS 247/247
linux-gcc-debug       PASS 247/247
linux-gcc-release     PASS 247/247
not verified: native Linux x64, Linux ARM64
``

## 6. Running log

- **2026-11-09** — Plan approved. §18.1 checklist signed off with no conditions; `plan.md` status set to Approved; `implementation.md` and the draft docs created. M0 not started.
- **2026-11-09** — M0 implemented locally: CMake/presets, doctest tests, source manager/diagnostics, target profiles, CLI skeletons, fixture extractor with 10 seed fixtures, CI workflow, and CLI determinism test. Windows MSVC Debug/Release, WSL GCC Debug/Release, WSL Clang Debug, and WSL Clang ASan/UBSan all build and pass 8/8 CTest. Remote CI is pending because no GitHub remote is configured.
- **2026-11-09 21:04:25 -08:00** — M2 C IRGen slice: added byte-array constants and declaration-only functions ([ADR 0004](docs/decisions/0004-ir-data-array-and-declarations.md)), then lowered C through Sema into typed SSA. `krc --dump-ir` now produces verified IR for `hello.c` and `mainentry.c`, including string globals, calls, allocas/loads/stores, pointer arithmetic, and `if`/`for`/`while` control flow. All six configurations pass 16/16 CTest; `krc_tests` has 70 doctest cases. Next is the KR32 descriptor and assembly path.
- **2026-11-09 21:16:52 -08:00** — M2 KR32 assembly slice: added `src/backend/kr32_backend.*` and wired `krc -S`. The backend assigns frame slots to SSA values and alloca objects, lowers arithmetic/comparisons/loads/stores/GEP/calls/branches, stages R0–R3 arguments and 8-byte varargs slots, and emits prologue/epilogue plus `.text`/`.data`. `hello.c` and `mainentry.c` emit assembly with `CALL putchar`/`CALL printf`; the assembly is not yet encoded or executed. All six configurations pass 18/18 CTest.
- **2026-11-09 21:20:50 -08:00** — G2a assembly validation: the emitted `hello.s` and `mainentry.s` assemble as `.k32` objects with the sibling KR32 assembler. `hello` linked with a stub `_start`/`putchar`/`exit` runs under the KR32 VM and halts with `exit=0`; `mainentry` contains the expected two `printf` varargs calls. G2a has local evidence. The in-tree encoder, descriptor tables, and real register allocation remain M2 hardening work; all six configurations still pass 18/18 CTest.
- **2026-11-09 21:25:09 -08:00** — Added the in-tree KR32 encoding module (`src/asm/kr32_encoder.*`) and target descriptor tables (`src/backend/kr32_descriptor.*`). Golden ISA encodings, extension words, immediate/register validation, and ABI register roles are covered by new tests; `krc_tests` now has 73 cases / 334 assertions and all six configurations pass 18/18 CTest. Assembly parsing/framework, pattern selection, graph-colouring allocation, and peepholes remain before M2 closes.
- **2026-11-09 21:33:13 -08:00** — Added the in-tree KR32 assembler (`src/asm/kr32_assembler.*`): label/section layout, directives, frame-memory operands, instruction parsing, local-symbol resolution, external relocations, and section byte emission. The assembler accepts the backend's `hello` and `mainentry` output; tests cover encodings, data layout, relocation offsets, and undefined symbols. `krc_tests` now has 76 cases / 378 assertions; all six configurations pass 18/18 CTest. Pattern selection, relaxation, graph-colouring allocation, and peepholes remain.
- **2026-11-09 21:38:37 -08:00** — Added graph-colouring register allocation (`src/backend/register_allocator.*`) with per-instruction liveness, interference construction, degree-ordered colouring, and spill reporting, plus a peephole pass wired into assembly emission. Tests cover a two-colourable function, spill pressure, and identity/zero-strength removal. `krc_tests` now has 79 cases / 395 assertions; all six configurations pass 18/18 CTest. Pattern selection and relocation relaxation remain; allocation is not yet the emission path.
- **2026-11-09 21:49:58 -08:00** — Started M3: froze KRO v1 in [ADR 0005](docs/decisions/0005-kro-object-format-freeze.md), implemented the KRO writer/reader and assembler-to-object bridge, added `krc -c`, implemented section merging/symbol resolution/relocation patching in `linker.*`, added the KR32 `.k32` executable writer, and replaced the `krld` stub with a working object linker. Object/CRC/determinism/linker/K32 tests pass; `krc_tests` has 85 cases / 449 assertions and all six configurations pass 19/19 CTest. G2b remains open pending crt0/minimal libc and correct image execution.
- **2026-11-09T22:03:10-08:00** — Closed G2b locally: added `targets/kr32/runtime/crt0.s` and `.s` input support in `krld`. `krc -c hello.c` → `krld hello.kro crt0.s` → `hello.k32` prints `Hello, KR32!` and exits 0; the same path links/runs `mainentry` to exit 0 with the printf bring-up stub. Local-label/data references now emit relocations so linked absolute addresses are patched. Added the `krc_link_hello` CTest. `krc_tests` has 86 cases / 459 assertions; all six configurations pass 20/20 CTest.
- **2026-11-09T22:06:08-08:00** — Extended the KR32 runtime with a C `puts` (`targets/kr32/runtime/libc.c`), pointer-increment IRGen support, and linker-defined heap/stack boundary symbols. `krc -c puts.c` + `krc -c libc.c` + `krld` + crt0 prints `hello from puts` and exits 0. Added `krc_link_puts`; `krc_tests` now has 86 cases / 462 assertions and all six configurations pass 21/21 CTest.
- **2026-11-09T22:19:21-08:00** — Started M4 C core: froze recursive named aggregate construction in [ADR 0006](docs/decisions/0006-recursive-aggregate-types.md); added `struct_forward`/`complete_struct`, frontend-record mapping, `.`/`->` member access, null-pointer coercion, and struct layout/GEP backend support. A recursive `struct Node` fixture links/runs and exits 5. `krc_tests` has 87 cases / 471 assertions; all six configurations pass 22/22 CTest.
- **2026-11-09T22:29:19-08:00** — Added integer varargs and real `printf`: [ADR 0007](docs/decisions/0007-varargs-base-opcode.md) introduces `vastart`, `stdarg.h` now expands `va_start`/`va_arg`, the backend lowers the varargs base, and `libc.c` prints `%d`/`%i`/`%u`/`%s`/`%c`/`%%`. `mainentry` now prints argc/argv, the varargs fixture prints `6`, and basic-block names are uniquified so generated control-flow labels cannot collide. All six configurations pass 24/24 CTest.
- **2026-11-09T22:31:33-08:00** — Added `offsetof` lowering and a larger C runtime: bump allocator, `free`/`calloc`, `strlen`/`strcmp`/`strncmp`/`strcpy`/`strdup`, and `memcpy`/`memset`/`memcmp`. The extracted `binarytree.c` now compiles, links, prints its AVL tree, and exits 0. `libc-test.c` compiles but still needs `_heapwalk`, heap reuse, and floating-point formatting; G3 remains open. `krc_tests` is 87 cases / 471 assertions; all six configurations pass 24/24 CTest.
- **2026-11-09T22:40:17-08:00** — Added function pointers/indirect calls (`CALL Rn` support in the assembler), a free-list allocator with reuse and `_heapwalk`, and a function-pointer fixture that exits 5. `binarytree.c` still runs and exits 0; `libc-test.c` now links and reaches the formatting phase, leaving floating-point `printf`/bit-field/aggregate-initialization work for G3. All six configurations pass 25/25 CTest.
- **2026-11-09T22:42:13-08:00** — Added positional aggregate initialization for structs and arrays, including zero-filling omitted fields, plus `offsetof` lowering. The aggregate fixture links/runs and exits 3; the function-pointer fixture exits 5, and binarytree still exits 0. All six configurations pass 26/26 CTest. Floating-point `printf`, bit-fields, and KDBG remain.
- **2026-11-09T22:55:00-08:00** — Added FP code generation and formatting: FPR parameters/returns/arguments, FP loads/stores and arithmetic, FP conversions/comparisons, floating-point varargs, and `%f`/`%e`/`%g`/`%x`/`%h` output. `libc-test.c` now exits 0; the floats fixture returns 4; full `varargs.c` prints integer/double/mixed cases and exits 0 with one rounding discrepancy. All six configurations pass 27/27 CTest. Bit-fields, exact general-format trimming, and KDBG remain.
- **2026-11-09T22:58:22-08:00** — Closed G3 locally: corrected 32-bit float constant materialization and `.S`/`.D` conversion selection, trimmed `%g` output, and fixed float varargs promotion. `binarytree.c`, `libc-test.c`, and `varargs.c` all compile, link, and run on KR32; `libc-test` and `varargs` exit 0 with the expected values. All six configurations pass 27/27 CTest. G4/KDBG and bit-fields remain.
- **2026-11-09T23:18:20-08:00** — Closed G4 locally through the ADR 0008 `.debug_info` bridge: IR debug locations, backend line/function/local/global/type records, linker source-ID merging, and K32 debug sections. DAP verifies a `binarytree.c` breakpoint at line 183, stops at `main`, reports `root`/`height`, evaluates `root`, reads memory, and expands a `Pair` fixture; the GUI renders the same session. All six configurations pass 28/28 CTest. KDBG, `libkrcdebug`, and bit-fields remain.
- **2026-11-09T23:32:30-08:00** — Added bit-field lowering: member access now uses Sema byte offsets, bit-field reads shift/mask/sign-extend, and assignments do a shifted read-modify-write. The bit-field fixture returns 19 and joins CTest. All six configurations pass 29/29 CTest. KDBG/`libkrcdebug` and designated initializers remain.
- **2026-11-09T23:41:36-08:00** — Froze KDBG v1 in ADR 0009 and implemented the shared `.kdbg` reader/writer. `krld` builds the image section from `.debug_info`; `krc --dump-symbols [--json]` decodes it; `docs/symbols.md` now specifies the header, section table, records, and CRC. M4 is complete locally: G3/G4, bit-fields, KDBG, and dump tooling are in place. All six configurations pass 31/31 CTest; `krc_tests` is 89 cases / 497 assertions.
- **2026-11-09T23:49:43-08:00** — Added KRO archive linking and deterministic linker reports: `krld` parses GNU/BSD `.a` archives, links their KRO members, and writes `--map`/`--print-size` output. The archive unit tests and full suite pass; `krc_tests` is 91 cases / 505 assertions, all six configurations 31/31 CTest. Weak/COMDAT, GC, relaxation, and layout scripts remain.
- **2026-11-09T23:54:30-08:00** — Added layout scripts: `krld --layout=FILE` reads `NAME ADDRESS` placements, aligns and honors them, and reports the resulting addresses; unit coverage checks explicit `.text` placement. `krc_tests` is 92 cases / 509 assertions, all six configurations 31/31 CTest. Weak/COMDAT, GC, and relaxation remain.
- **2026-11-10T00:00:10-08:00** — Started M5: the lexer/parser accept `class`, access labels, and class-as-type-name declarations; class records use the C struct layout frozen in ADR 0010. The `class.cpp` fixture compiles, links, and returns 42 on KR32, adding a 31st CTest. `krc_tests` remains 92 cases / 509 assertions; all six configurations pass 31/31 CTest.
- **2026-11-10T00:08:04-08:00** — Added inline C++ member functions: methods are collected from class bodies, emitted as `Class_method` with a hidden `this` pointer, unqualified fields resolve through `this`, and `object.method()` calls pass the object address. `../kr32/examples/hello.cpp` and the `method.cpp` fixture return 42 on KR32; 32/32 CTest pass. Constructors, inheritance, virtuals, and `new`/`delete` remain.
- **2026-11-10T00:17:00-08:00** — Added default constructors: constructor definitions are parsed as `Class_Class`, called with the object address for local class declarations, and can initialize fields through implicit `this`. The constructor fixture returns 7 and joins CTest; 33/33 tests pass across all six configurations. Destructors, parameterized constructors, inheritance, virtuals, and `new`/`delete` remain.
- **2026-11-10T00:23:09-08:00** — Added scalar `new`/`delete`, implicit recursive method calls, and constructor calls for allocated objects. `new T` lowers to `malloc` + constructor; `delete` lowers to `free`; unqualified calls inside a method resolve through `this`. New fixtures return 9 and 120, bringing CTest to 35/35; `krc_tests` remains 92 cases / 509 assertions. Destructors, inheritance, virtual dispatch, and `balanced-tree.cpp` remain.
- **2026-11-10T00:30:00-08:00** — Added parameterized constructors and direct-initialization syntax (`Pair pair(3, 4)`). Constructor arguments are promoted to the constructor's parameters; the fixture returns 7. CTest is 36/36; `krc_tests` remains 92 cases / 509 assertions. Destructors, inheritance, virtual dispatch, and `balanced-tree.cpp` remain.
- **2026-11-10T00:57:30-08:00** — Closed G5 locally: C++ mode accepts `.cpp`/`-std=c++11`, struct tags as type names, conditional expressions, single inheritance, pure virtual declarations, override methods, and base-constructor calls. ADR 0011 introduces per-concrete-class static devirtualization of inherited methods. `hello.cpp` returns 42 and `balanced-tree.cpp` prints both sorted trees and exits 0; CTest is 38/38 across all six configurations.
- **2026-11-10T01:03:14-08:00** — Added destructors: `~Class` parses as a method, derived destructor clones are selected for inherited objects, and local objects run destructors on function return. The destructor fixture prints `destructor`; `balanced-tree.cpp` still exits 0 with cleanup. CTest is 39/39; block-scope destruction and copy/move remain.
- **2026-11-10T01:08:35-08:00** — Added block-scope destruction and array `new`/`delete`: compound statements run destructors in reverse order at their closing brace, and `new T[n]`/`delete[]` size the allocation. The nested-destructor fixture prints `done` before `after`, and `new_array` returns 7. CTest is 41/41; copy/move and general vtables remain.
- **2026-11-10T01:14:32-08:00** — Added default copy construction and assignment: class lvalues copy field by field, including arrays, without a user copy constructor. The copy fixture returns 7. CTest is 42/42; move semantics, user-defined copy, and general vtables remain.
- **2026-11-10T01:22:35-08:00** — Started M6: `nullptr` lowers as a null pointer constant, `auto` local declarations infer from their initializer, and range-for over bounded local arrays lowers to an index loop. The auto/nullptr fixture returns 4 and the range-for fixture returns 6; CTest is 44/44. Templates, lambdas, move semantics, and `constexpr` remain.
- **2026-11-10T01:27:45-08:00** — Added `constexpr` declarations and functions as a declaration specifier; they lower and run at runtime, including constexpr locals. The constexpr fixture returns 12; CTest is 45/45. Compile-time evaluation, templates, lambdas, and move semantics remain.
- **2026-11-10T01:32:17-08:00** — Added scoped enums and `::` qualification: `enum class Color`, `Color::Green`, and scoped enumerator constants work alongside unscoped enums. The enum-class fixture returns 1; CTest is 46/46. Templates, lambdas, `decltype`, and move semantics remain.
- **2026-11-10T01:38:59-08:00** — Added `decltype`: `decltype(expression)` parses as a type specifier and resolves to the operand's type, including local `auto` variables. The decltype fixture returns 4; CTest is 47/47. Templates, lambdas, and move semantics remain.
- **2026-11-10T01:43:28-08:00** — Added designated aggregate initializers: `.field = value` and `[index] = value` select struct/array members and unspecified elements zero-initialize. The designated fixture returns 16; CTest is 48/48. Nested designators, templates, lambdas, and move semantics remain.
- **2026-11-10T01:47:24-08:00** — Added nested designated initializers: `.inner.field = value` recurses into nested structs with zero-initialized unspecified members. The nested-designated fixture returns 12; CTest is 49/49. Templates, lambdas, and move semantics remain.
- **2026-11-10T01:51:44-08:00** — Added flat C++ namespaces and `using Alias = Type;` aliases. Namespace members flatten into the translation unit; aliases register as typedefs. The namespace fixture returns 7; CTest is 50/50. Qualified names, templates, lambdas, and move semantics remain.
- **2026-11-10T01:56:02-08:00** — Added qualified namespace lookup: `ns::function` and `ns::global` fall back from the qualified name to its leaf member. The qualified-namespace fixture returns 7; CTest is 51/51. Templates, lambdas, and move semantics remain.
- **2026-11-10T02:01:03-08:00** — Added default function arguments: trailing parameters may declare `= value`, calls fill omitted arguments from the defaults, and Sema accepts the reduced arity. The default-args fixture returns 7; CTest is 52/52. Templates, lambdas, and move semantics remain.
- **2026-11-10T02:05:40-08:00** — Extended default arguments to member functions; method calls fill omitted parameters after the implicit `this`. The method-default fixture returns 7; CTest is 53/53. Templates, lambdas, and move semantics remain.
- **2026-11-10T02:10:18-08:00** — Added `explicit` constructors and `noexcept`/`final` method specifiers. Explicit direct initialization and const/noexcept accessors compile; the explicit fixture returns 7. CTest is 54/54; templates, lambdas, and move semantics remain.
- **2026-11-10T02:14:23-08:00** — Added reference declarators and lvalue-bound reference variables: `int& ref = value`, reads, and writes alias the original object. The reference fixture returns 4; CTest is 55/55. Reference parameters, templates, lambdas, and move semantics remain.
- **2026-11-10T02:20:18-08:00** — Added lvalue reference parameters: calls pass the argument address, and writes through the parameter update the caller's object. The reference-param fixture returns 4; CTest is 56/56. Operator overloading, move semantics, templates, and lambdas remain.
- **2026-11-10T02:24:16-08:00** — Extended reference parameters to member functions; member calls pass the argument address after the implicit `this`. The method-reference fixture returns 4; CTest is 57/57. Operator overloading, move semantics, templates, and lambdas remain.
- **2026-11-10T02:29:37-08:00** — Added member `operator+`: `first + second` resolves the member operator, passes `this` and the reference argument, and calls the mangled method. The operator fixture returns 7; CTest is 58/58. Remaining operators, move semantics, templates, and lambdas remain.
- **2026-11-10T02:39:45-08:00** — Added free operators and extended member operator lookup to `-` and `==`; free `operator+` passes reference arguments and mangled symbols, and reference parameters now support member access. The free-operator fixture returns 7; CTest is 59/59. Remaining operators, move semantics, templates, and lambdas remain.
- **2026-11-10T02:46:05-08:00** — Added no-capture lambdas with parameters: `[](int value) { return value + 1; }` emits an internal function and an indirect call through the function pointer. The lambda fixture returns 4; CTest is 60/60. Capturing lambdas, remaining operators, move semantics, and templates remain.
- **2026-11-10T02:56:56-08:00** — Added structured bindings over struct fields: `auto [left, right] = pair;` binds each name to the corresponding field address with the field type. The structured fixture returns 7; CTest is 61/61. `if constexpr`, inline variables, templates, and move semantics remain.
- **2026-11-10T03:02:05-08:00** — Added `true`/`false` literals, `if constexpr` syntax, and inline variables. The fixture reads an `inline int` inside `if constexpr (true)` and returns 3; CTest is 62/62. Fold expressions, templates, and move semantics remain.
- **2026-11-10T03:06:15-08:00** — Accepted `template <typename T>` function syntax by binding template parameters to `int`, so the template fixture returns 7. This is a placeholder specialization, not real template deduction; CTest is 63/63.
- **2026-11-10T03:10:35-08:00** — Accepted scoped-enum underlying types (`enum class Color : int`) and `alignas(...)` declaration syntax. The fixture returns 4; CTest is 64/64. `alignas` alignment is currently accepted but not applied.
- **2026-11-10T03:14:21-08:00** — Applied `alignas` to global and local storage alignment: `alignas(16) int value` emits `@value = global i32 3, align 16`. The fixture still returns 4; CTest is 64/64.
- **2026-11-10T03:18:18-08:00** — Gave `inline` and `constexpr` functions internal linkage so two translation units can each define the same inline function without a duplicate symbol. The two-object inline fixture links and returns 6; CTest is 65/65.
- **2026-11-10T03:23:06-08:00** — Replaced the internal-copy workaround with true weak linkage: IR `Linkage::Weak`, `.weak` assembly, and KRO weak symbols per [ADR 0012](docs/decisions/0012-weak-linkage.md). The two-object inline fixture still links and returns 6; CTest is 65/65.
- **2026-11-10T03:27:56-08:00** — In-class methods now emit weak symbols, so a class defined in two translation units links without duplicate-method errors. The two-object inline-class fixture links and returns 6; CTest is 66/66.
- **2026-11-10T03:33:20-08:00** — Made `if constexpr` discard the unselected branch during Sema and IRGen: an undeclared identifier in `if constexpr (false)` no longer errors, and the fixture returns 3. CTest is 67/67.
- **2026-11-10T03:39:45-08:00** — Added constructor overload selection by argument count: `Value()` and `Value(int)` coexist, are mangled by arity, and `Value first; Value second(3);` both call the right constructor. The fixture returns 4; CTest is 68/68.
- **2026-11-10T03:44:09-08:00** — Added user-defined copy constructors: `Value(const Value&)` is selected for `Value second = first;`, receives the source address, and runs instead of the default field copy. The fixture returns 2; CTest is 69/69.
- **2026-11-10T03:48:23-08:00** — Added user-defined copy assignment: `Value& operator=` is selected for class assignment when present and receives the source address. The fixture returns 3; CTest is 70/70.
- **2026-11-10T03:52:37-08:00** — Added compile-time evaluation for `constexpr` variables: constant initializers record a constant value that array bounds and other constant expressions consume. `constexpr int count = 3; int values[count];` compiles and the fixture returns 7; CTest is 71/71.
- **2026-11-10T03:57:57-08:00** — Added compile-time evaluation of simple single-return `constexpr` functions: arguments and the return expression are interpreted during constant evaluation. `constexpr int count = triple(2);` drives an array bound and the fixture returns 7; CTest is 72/72.
- **2026-11-10T04:03:47-08:00** — Added array object lifetime support: `new T[n]` stores a length cookie, constructs each element, and returns an element pointer; `delete[]` reads the cookie, destroys each element, and frees the base. The fixture destroys three items and returns 3; CTest is 73/73.
- **2026-11-10T04:09:13-08:00** — Added rvalue-reference declarators and move-constructor selection through an explicit rvalue cast: `Value second((Value&&)first);` calls `Value(Value&&)`. The fixture returns 4; CTest is 74/74. `std::move` and general rvalue overload ranking remain.
- **2026-11-10T04:14:59-08:00** — Recognized `std::move(x)` as an rvalue-reference expression: Sema gives it a reference type and constructor selection prefers the move constructor for `Value second = std::move(first);`. The fixture returns 4; CTest is 75/75.
- **2026-11-10T04:19:29-08:00** — Added copy and move assignment overloads selected by value category: `second = first` calls `operator=(const Value&)` and `second = std::move(first)` calls `operator=(Value&&)`. The fixture returns 23; CTest is 76/76.
- **2026-11-10T04:25:16-08:00** — Added placement new: `new (storage) Item()` constructs at the supplied address without allocating. `alignas` is now recognized as a declaration start in block scope. The fixture returns 3; CTest is 77/77.
- **2026-11-10T04:30:41-08:00** — Added braced class initialization: `Value first{3};` and `Value second{};` select the matching constructor overloads. The fixture returns 4; CTest is 78/78.
- **2026-11-10T04:34:45-08:00** — Added braced aggregate initialization for non-constructor structs: `Pair pair{3, 4};` initializes fields in order. The fixture returns 7; CTest is 79/79.
- **2026-11-10T04:43:11-08:00** — Added range-for over a braced initializer list: `for (int value : {1, 2, 3})` builds a temporary bounded array and iterates it. The fixture returns 6; CTest is 80/80.
- **2026-11-10T04:47:52-08:00** — Extended structured bindings to arrays: `auto [first, second] = values;` binds elements by index. The fixture returns 7; CTest is 81/81.
- **2026-11-10T04:53:00-08:00** — Added member `operator[]`: `values[0]` calls the user-defined subscript operator with the object address. The fixture returns 7; CTest is 82/82.
- **2026-11-10T04:58:30-08:00** — Fixed implicit array-member access inside methods: `values_[index]` now decays the member array to an element pointer before subscripting. The `operator[]` fixture now indexes a real array member and still returns 7; CTest is 82/82.
- **2026-11-10T05:03:18-08:00** — Added member `operator()`: calling a class object runs the user-defined call operator with the object address. The fixture returns 7; CTest is 83/83.
- **2026-11-10T05:10:04-08:00** — Added user-defined compound assignment: `total += increment` calls the member `operator+=` with the object address and source. The fixture returns 7; CTest is 84/84.
- **2026-11-10T05:16:56-08:00** — Added constructor member-initializer lists: `Point(int x, int y) : x_(x), y_(y) {}` initializes fields before the body runs. The fixture returns 7; CTest is 85/85.
- **2026-11-10T05:21:49-08:00** — Added delegating constructors: `Point() : Point(3, 4) {}` forwards to another constructor on the same object. The fixture returns 7; CTest is 86/86.
- **2026-11-10T05:26:47-08:00** — Accepted C++ `static_assert` alongside `_Static_assert`: the condition is evaluated during analysis and a false condition reports "static assertion failed". The `sizeof` layout fixture compiles and returns 0; CTest is 87/87.
- **2026-11-10T05:31:32-08:00** — Added `auto` return-type deduction: the first return expression's type becomes the function return type, so `auto half(double v)` returns `double`. The fixture returns 4; CTest is 88/88.
- **2026-11-10T05:39:40-08:00** — Added member unary operators: `-value` and the `!`/`~`/`++`/`--` forms dispatch to `operator-`, `operator!`, and so on with the object address. The fixture returns 4; CTest is 89/89.
- **2026-11-10T05:43:58-08:00** — Added member increment/decrement dispatch for both prefix and postfix forms; `operator++` is called for `++counter` and `counter++`. The fixture returns 5; CTest is 90/90.
- **2026-11-10T05:51:51-08:00** — Separated prefix and postfix increment/decrement overloads: `operator++()` serves `++counter`, and `operator++(int)` serves `counter++` with a zero dummy argument. The fixture distinguishes the two calls and returns 45; CTest is 90/90.
- **2026-11-10T05:56:34-08:00** — Extended compile-time `constexpr` evaluation to multi-statement function bodies with `if` branches and local declarations. `constexpr int count = magnitude(-3);` drives an array bound and the fixture returns 7; CTest is 91/91.
- **2026-11-10T06:01:46-08:00** — Extended compile-time `constexpr` evaluation to bounded `for` loops, assignment, and `++`/`--` on constant locals. `constexpr int count = sumTo(3);` drives an array bound and the fixture returns 7; CTest is 92/92.
- **2026-11-10T06:05:58-08:00** — Extended compile-time `constexpr` evaluation to `while` and `do` loops. `constexpr int count = sumWhile(3);` drives an array bound and the fixture returns 7; CTest is 93/93.
- **2026-11-10T06:10:10-08:00** — Supported `return *this;` in methods returning a reference; the copy-assignment fixture now returns `Value&` and still returns 3. CTest is 93/93.
- **2026-11-10T06:14:51-08:00** — Supported chained assignment: `third = second = first` passes the reference result of the inner assignment to the outer one. The fixture returns 5; CTest is 93/93.
- **2026-11-10T06:18:48-08:00** — Added compile-time `constexpr` recursion with conditional expressions. `constexpr int count = factorial(3);` drives an array bound and the fixture returns 7; CTest is 94/94.
- **2026-11-10T06:47:23-08:00** — Added vptr layout and per-object virtual slot tables (ADR 0013 / D-037). Polymorphic classes place the vptr at offset 0; construction builds the slot table; `base->value()` loads the derived slot and returns 2. The fixture returns 2; CTest is 95/95.
- **2026-11-10T06:57:58-08:00** — Added virtual destructor dispatch: a virtual destructor occupies one vtable slot, a derived destructor replaces it, and `delete base_pointer` calls the derived destructor before freeing. The fixture returns 11; CTest is 96/96.
- **2026-11-10T07:21:36-08:00** — Added dynamic dispatch through a base reference: reference initialization now bitcasts the object address to the base type, and `base.value()` on a `Base&` loads the derived vtable slot. The fixture returns 2; CTest is 97/97.
- **2026-11-10T07:30:48-08:00** — Added lambda return-type deduction: a captureless lambda's first return expression determines its return type, so `[](double value) { return value / 2; }` returns `double`. The fixture returns 4; CTest is 98/98.
- **2026-11-10T07:47:52-08:00** — Added by-value lambda captures: a capturing lambda lowers to a closure struct holding the function pointer and copied captures; calls load the function pointer and pass the closure. `[base](int value) { return base + value; }` returns 7; CTest is 99/99.
- **2026-11-10T07:58:29-08:00** — Added by-reference lambda captures: `[&base]` stores the variable's address in the closure, so later outer mutations are visible. The fixture sets `base = 4` and returns 7; CTest is 100/100.
- **2026-11-10T08:11:30-08:00** — Added init captures: `[value = base + 1]` evaluates the initializer at lambda creation and stores the result in the closure. Changing `base` afterward does not affect the captured value; the fixture returns 7; CTest is 101/101.
- **2026-11-10T08:23:02-08:00** — Added default lambda captures: `[=]` copies every visible local into the closure and `[&]` stores their addresses. The copy fixture returns 7 after an outer mutation; the reference fixture returns 7 after mutating through the lambda; CTest is 103/103.
- **2026-11-10T08:32:43-08:00** — Added dynamic dispatch for unqualified virtual calls on `this`: a base method's `value()` now loads the derived vtable slot. `Base::twice()` called through a base pointer returns 4 for a derived override; CTest is 104/104.
- **2026-11-10T08:43:16-08:00** — Added reference-returning `operator[]` and reference-return lowering: `int& operator[](int)` returns the element address, and `values[0] = 3` stores through it. The fixture returns 5; CTest is 105/105.
- **2026-11-10T08:52:05-08:00** — Added named casts: `static_cast`, `reinterpret_cast`, and `const_cast` parse into the existing cast expression. `static_cast<int>(3.9)` returns 3; CTest is 106/106.
- **2026-11-10T09:05:17-08:00** — Added reference data members: `int& value_` occupies a pointer field, a constructor initializer binds it to the argument address, and reads load through the stored pointer. The fixture observes an outer mutation and returns 4; CTest is 107/107.
- **2026-11-10T09:17:34-08:00** — Added virtual destructor dispatch for `delete[]`: array elements receive vtables even without a constructor, and the derived destructor occupies the base destructor slot. Deleting two derived objects through a base array destroys both; CTest is 108/108.
- **2026-11-10T09:26:58-08:00** — Accepted `override`, `final`, and `noexcept` after a destructor declarator; `override` also marks a destructor virtual. The array-destructor fixture now declares `~Derived() override`; CTest remains 108/108.
- **2026-11-10T09:37:29-08:00** — Completed empty class definitions (`class Derived : public Base {};`) and cast the receiver to the declaring base when calling an inherited `operator[]`. The fixture calls the base subscript through an empty derived object and returns 5; CTest is 109/109.
- **2026-11-10T09:46:50-08:00** — Applied the same receiver conversion to inherited `operator()` calls. Calling `value(2)` through an empty derived object reaches `Base::operator()` and returns 5; CTest is 110/110.
- **2026-11-10T09:56:13-08:00** — Added pure virtual dispatch: a bodyless `virtual int value() = 0` now gets a declaration entry, its base vtable slot is null, and a derived override fills the slot. Calling through `Base*` returns 7; CTest is 111/111.
- **2026-11-10T10:05:06-08:00** — Added chained namespace qualification: the parser accumulates `outer::inner::name` segments and Sema resolves the final segment. The nested-namespace fixture returns 5; CTest is 112/112.
- **2026-11-10T10:15:01-08:00** — Added by-value aggregate parameters on KR32: aggregate loads/stores copy every 4-byte word, call sites place words in consecutive integer registers, and the callee prologue stores them into its frame slot. Passing an 8-byte `Point` by value returns 7; CTest is 113/113.
- **2026-11-10T10:26:13-08:00** — Added small aggregate returns: `ret` loads up to two words into `R0`/`R1`, call sites store them into the result slot, and a local record can be initialized directly from a factory call. `make_point()` returns 3/4 and the fixture returns 7; CTest is 114/114.
- **2026-11-10T10:36:06-08:00** — Started M7: `--target=kr16` now parses and lowers to IR on the IP16 data model, and codegen requests fail explicitly instead of producing KR32 output. Integer cast folding honors width and signedness so `(char)-1 > 0` holds for unsigned KR16 `char`. CTest is 116/116.
- **2026-11-10T10:45:08-08:00** — Added the KR16 encoder: R/I/M/M8/B/S/BIT/C/X formats, relative control transfers, branch conditions, and range validation, with golden ISA encodings and rejection tests. `krc_tests` is 95 cases / 552 assertions; CTest remains 116/116.
- **2026-11-10T10:55:15-08:00** — Added the KR16 assembler: labels, text/data sections, global/extern declarations, register/immediate ALU forms, loads/stores/LEA, shifts, bit operations, conditional branches, relative and register `JMP`/`CALL`, and control instructions. `krc_tests` is 97 cases / 574 assertions; CTest remains 116/116.
- **2026-11-10T11:16:06-08:00** — Added the scalar KR16 backend: SP-relative frames, `R1`–`R3` parameters, two-operand ALU, loads/stores/LEA, comparison materialization, branches, calls, and returns. `sum.c` emits assembly, assembles with the external KR16 assembler, and runs to exit 55; CTest is 117/117.
- **2026-11-10T11:30:14-08:00** — Added the minimal KR16 EXEC image: `krc -c --target=kr16` emits a 64-byte-header `.kex` with `.text`, `.debug_str`, CRC32, and a generated `_start` calling `main`. The in-tree assembler now accepts `SP`/`FP`, negative displacements, and keeps offsets across repeated `.text`. `sum.c` runs to exit 55; CTest is 118/118.
- **2026-11-10T11:44:36-08:00** — Added string globals, label-resolving `LEA`, `.byte` emission, and an MMIO `puts` shim to the KR16 image path. `hello.c` prints `Hello, KR16!` and exits 0; CTest is 119/119.
- **2026-11-10T11:53:09-08:00** — Added byte-exact determinism tests for a KR32 object and a KR16 `.kex`; both compile the same input twice and compare SHA-256. CTest is 121/121.
- **2026-11-10T12:02:34-08:00** — Added local `static` variables: each becomes an internal global with zero or constant initialization and persists across calls. `counter()` returns 1 then 2, so the fixture exits 3; CTest is 122/122.
- **2026-11-10T12:12:01-08:00** — Added guarded runtime initialization for non-constant static locals: a guard flag runs the initializer once on first entry. `seed(3)` then `seed(5)` returns 6; CTest is 123/123.
- **2026-11-10T12:21:05-08:00** — Added guarded construction for zero-argument static class objects: the constructor runs once and later calls observe the initialized state. The `Counter` fixture returns 3; CTest is 124/124.
- **2026-11-10T12:30:13-08:00** — Added constructor arguments for static class objects: `static Counter counter(5)` selects the matching constructor and promotes its argument. The fixture returns 5; CTest is 125/125.
- **2026-11-10T12:40:52-08:00** — Added global object construction: namespace-scope objects with constructors run through `__krc_init_globals`, called by a generated `main` wrapper. `Counter global(5)` returns 5; CTest is 126/126.
- **2026-11-10T12:51:56-08:00** — Added global destruction: per-object destructor functions run through `__krc_destroy_globals` in reverse order after user `main`, before the wrapper returns. A destructor printing `destroyed` was confirmed on the KR32 VM; CTest is 128/128.
- **2026-11-10T13:01:06-08:00** — Added non-constant scalar and pointer global initializers: `__krc_init_globals` evaluates them and stores the result before user `main`. `int global = square(3)` returns 9; CTest is 129/129.
- **2026-11-10T13:09:48-08:00** — Added `%p`, `l`/`z` length modifiers, and unsigned formatting to the KR32 `printf`. The fixture prints `0x800ffd0 123456 4 4000000000`; CTest is 130/130.
- **2026-11-10T13:14:51-08:00** — Added integer field width, zero/left padding, and `%o` to the KR32 `printf`. `%05d %-5d %8x %o` prints `00042 7           ff 10`; CTest is 131/131.
- **2026-11-10T13:20:04-08:00** — Added string precision and field width to the KR32 `printf`: `%.3s`, `%8s`, and `%-8s` print `[abc][      hi][hi      ]`. CTest is 132/132.
- **2026-11-10T13:25:17-08:00** — Added integer precision and sign flags to the KR32 `printf`: `%.5d`, `%+.2d`, and `% 4d` print `[00042][+07][   9]`. CTest is 133/133.
- **2026-11-10T13:30:46-08:00** — Added `*` width and precision to the KR32 `printf`: `%*d`, `%-*d`, and `%.*s` print `[   42][7    ][abc]`. CTest is 134/134.
- **2026-11-10T13:38:24-08:00** — Fixed `&&`/`||` truthiness: operands now compare against zero instead of truncating to one bit, so nonzero even values are true. Added `strcat`, `strncpy`, `strchr`, `strstr`, `atoi`, `atol`, and `abs` to the KR32 libc. The string fixture builds `abcdef`; CTest is 136/136.
- **2026-11-10T13:48:20-08:00** — Added `strrchr`, `strncat`, and qsort to the KR32 libc. `qsort` works with its four fixed arguments; `bsearch` is deferred because five fixed arguments still mispassed through the KR32 call path. The sort fixture prints `alpha/beta 1 1`; CTest is 137/137.
- **2026-11-10T13:59:00-08:00** — Implemented ADR 0001's stack passing for fixed integer arguments after `R0`–`R3`; outgoing slots are reserved by the caller and extra arguments are staged without clobbering register arguments. `sum5(1,2,3,4,5)` returns 15, `bsearch` works, and CTest is 138/138.
- **2026-11-10T14:08:53-08:00** — Added `sprintf` by routing the existing formatter through a buffer sink, so `printf` and `sprintf` share one implementation. The fixture writes `7:ok:005` and returns 8; CTest is 139/139.
- **2026-11-10T14:17:22-08:00** — Added bounded `snprintf`: it writes at most `size - 1` characters, always terminates, and returns the full formatted length. A five-byte buffer receives `abcd` from `abcdef` and returns 6; CTest is 140/140.
- **2026-11-10T14:26:35-08:00** — Added `strtol`, `strtoul`, `srand`, and `rand` to the KR32 libc. The fixture parses `0x1f` as 31, `-42`, and octal `17` as 15, then draws two in-range random values; CTest is 141/141.
- **2026-11-10T14:37:30-08:00** — Added destructors for function-local static objects: each destructible static registers with `__krc_destroy_globals` and runs after user `main`. The fixture prints `ctor main dtor`; CTest is 142/142.
- **2026-11-10T14:53:32-08:00** — Added the C99 `__func__` predefined identifier for free functions; it lowers to the current function-name string. The fixture checks that `__func__` is `"main"`; CTest is 143/143.
- **2026-11-10T15:08:00-08:00** — Extended `__func__` to class methods by recording each function's source-level name separately from its mangled IR symbol. The method fixture compares `__func__` with `"method_name"`; CTest is 144/144.
- **2026-11-10T15:23:44-08:00** — Fixed adjacent string-literal concatenation: the parser now merges literal contents rather than raw token text, so `"hello" " world"` compares equal to `"hello world"`. CTest is 145/145.
- **2026-11-10T15:44:11-08:00** — Added C++11 default member initializers: Sema records `int value = 5` on the member, constructors apply it unless the member is in the initializer list, and objects without a user constructor apply it during default construction. The fixtures return 5 and 7; CTest is 147/147.
- **2026-11-10T16:00:31-08:00** — Extended default-construction member initialization past the vptr and inherited base fields, so polymorphic and derived classes apply their defaults. The derived/base fixture returns 12; CTest is 148/148.
- **2026-11-10T16:28:11-08:00** — Resolved inherited data-member lvalues through the base record and applied base-level default member initializers before derived ones. `object.value + object.base_value` returns 12; CTest is 148/148.
- **2026-11-10T16:31:31-08:00** — Added `strspn`, `strcspn`, `strpbrk`, and `strtok` to the KR32 libc. The fixture splits `alpha,beta;gamma` into three tokens and returns 0; CTest is 149/149.
- **2026-11-10T16:50:32-08:00** — Implemented unsized `char text[] = "..."`: CIRGen infers the array length from the literal and stores its bytes plus the terminator. The fixture measures length 3; CTest is 150/150.
- **2026-11-10T17:08:42-08:00** — Added global string-array initializers: the global's length is inferred from the literal and emitted as constant data. `char text[] = "hello"` measures 5; CTest is 151/151.
- **2026-11-10T17:26:55-08:00** — Added braced global aggregate initializers: they run from `__krc_init_globals` before user `main`. `struct Point point = {3, 4}` returns 7; CTest is 152/152.
- **2026-11-10T17:42:58-08:00** — Added global array initializers, including unsized `int values[] = {1,2,3}` length inference. The fixture sums an inferred and a fixed array to 15; CTest is 153/153.
- **2026-11-10T17:57:53-08:00** — Verified and covered global pointer scalars and pointer arrays initialized from string literals; they run from `__krc_init_globals`. CTest is 154/154.
- **2026-11-10T18:06:34-08:00** — Added function-local static aggregate initialization: bounded arrays and structs initialize once behind a guard, with unsized array length inferred. The fixture sums to 15; CTest is 155/155.
- **2026-11-10T18:16:21-08:00** — Added C99 compound literals: the literal allocates a temporary and initializes it in place. `(struct Point){3,4}` copies to a local and returns 7; CTest is 156/156.
- **2026-11-10T18:27:37-08:00** — Fixed conditional expressions to allocate their inferred result type rather than always `int`, preserving double and pointer results. Added `fabs`, `floor`, `ceil`, and `sqrt`; the math fixture prints `4.0 -2.0 2.0 3.5`; CTest is 157/157.
- **2026-11-10T18:37:20-08:00** — Improved conditional result typing to prefer a double/float/pointer branch from either side, so `condition ? 0 : 3.5` and `condition ? 0 : "text"` keep their types. The fixture returns 0; CTest is 158/158.
- **2026-11-10T18:46:35-08:00** — Added `memmove`, `memchr`, and `strnlen` to the KR32 libc. The memory fixture shifts `abcde` to `aabcd` and checks a bounded length; CTest is 159/159.
- **2026-11-10T18:56:15-08:00** — Consumed explicit template-argument lists in function calls, so `maximum<int>(3,4)` reaches the int-bound template body. The fixture returns 4; CTest is 160/160.
- **2026-11-10T19:06:26-08:00** — Applied default member initializers to `new` allocations when the class has no user constructor. The fixture observes 5 from `new Value()`; CTest is 161/161.
- **2026-11-10T19:16:59-08:00** — Applied default members to each `new[]` element. A two-element `Value` array sums to 10; CTest is 162/162.
- **2026-11-10T19:29:27-08:00** — Parsed `new T(arguments)` and passed promoted constructor arguments; `new Counter(5)` returns 5. CTest is 163/163.
- **2026-11-10T19:39:58-08:00** — Default construction now calls zero-argument constructors for class-type members. The `Outer`/`Inner` fixture observes 5; CTest is 164/164.
- **2026-11-10T19:51:18-08:00** — Applied member construction to `new` allocations, so `new Outer()` initializes its `Inner` member. The fixture returns 5; CTest is 165/165.
- **2026-11-10T20:01:57-08:00** — Global objects without a user constructor now construct class-type members from `__krc_init_globals`. The `Outer global` fixture returns 5; CTest is 166/166.
- **2026-11-10T20:13:53-08:00** — Global destruction now calls class-type member destructors when the outer class has none. The fixture prints `main` then `inner`; a matching IR check makes CTest 168/168.
- **2026-11-10T20:25:13-08:00** — Synthesized `__krc_member_dtor_<class>` cleanups for block-scope objects without an outer destructor. A local `Outer` prints `inner` at scope exit; the link and IR checks make CTest 170/170.
- **2026-11-10T20:36:26-08:00** — `delete` now calls the synthesized member destructor when the class has no own destructor. `delete new Outer()` prints `inner`; the link and IR checks make CTest 172/172.
- **2026-11-10T20:47:14-08:00** — `delete[]` now uses the synthesized member destructor for every element. A two-element `Outer` array prints `inner` twice; the link and IR checks make CTest 174/174.
- **2026-11-10T21:02:55-08:00** — Fixed floating compound assignments to emit `fadd`/`fsub`/`fmul`/`fdiv`, then added `strtod` with sign, fraction, and exponent handling. The parser reads `-3.25e2tail` as `-325.00`; CTest is 175/175.
- **2026-11-10T21:21:03-08:00** — User-defined constructors now default-construct class-type members without explicit or default initializers. The `Outer(){}`/`Inner` fixture returns 5; the link and IR checks make CTest 177/177.
- **2026-11-10T21:37:14-08:00** — User-defined constructors now call an unnamed base-class zero-argument constructor first. The `Derived`/`Base` fixture returns 5; the link and IR checks make CTest 179/179.
- **2026-11-10T21:50:46-08:00** — Synthesized member destructors now call the base-class destructor after members. The `Derived`/`Base` fixture prints `base` at scope exit; the link and IR checks make CTest 181/181.
- **2026-11-10T22:03:03-08:00** — Global destruction now calls the base-class destructor after the derived destructor and member destructors. The fixture prints `main`, `derived`, `base`; the link and IR checks make CTest 183/183.
- **2026-11-10T22:28:52-08:00** — Added `atof`, `strtof`, and `llabs` to the KR32 libc. The fixture prints `2.50 1.25 7`; CTest is 184/184.
- **2026-11-10T22:43:55-08:00** — Generated method bodies before free functions so synthesized cleanups can see destructor bodies, and made block-scope destructors call the class's own destructor, members, then base. A local `Derived` prints `derived` then `base`; the link and IR checks make CTest 186/186.
- **2026-11-10T22:58:06-08:00** — `delete` now uses the complete destructor for non-virtually-dispatched objects. `delete new Derived()` prints `derived` then `base`; the link and IR checks make CTest 188/188.
- **2026-11-10T23:10:48-08:00** — `delete[]` uses the same complete destructor per element for non-virtual classes. A two-element `Derived` array prints `derived`, `base` twice; the link and IR checks make CTest 190/190.
- **2026-11-10T23:24:28-08:00** — Global objects now run an unnamed base-class zero-argument constructor from `__krc_init_globals`. The `Derived global` fixture observes `Base`'s 5; the link and IR checks make CTest 192/192.
- **2026-11-10T23:38:52-08:00** — Block-scope default construction now runs an unnamed base-class zero-argument constructor before members. A local `Derived` observes `Base`'s 5; the link and IR checks make CTest 194/194.
- **2026-11-10T23:54:25-08:00** — `new Derived()` now runs an unnamed base-class zero-argument constructor and offsets member initialization past the base fields. The fixture returns 5; the link and IR checks make CTest 196/196.
- **2026-11-11T00:07:25-08:00** — Global object initialization now starts derived member fields after the base fields. A base value 5 plus an extra member 7 sums to 12; the link and IR checks make CTest 198/198.
- **2026-11-11T00:20:43-08:00** — Implemented C11 `_Generic` selection by inferred control type with a `default` fallback. The fixture selects 3 for `int` and 7 for `double`; CTest is 199/199.
- **2026-11-11T00:33:40-08:00** — `_Generic` now compares pointer element types and record tags, and the fixture also selects 9 for `int *`; CTest remains 199/199.
- **2026-11-11T00:46:56-08:00** — `_Generic` now infers a cast expression's type, so a `(double)` control selects the `double` association. CTest is 200/200.
- **2026-11-11T01:01:15-08:00** — `decltype(expression)` now infers locals, literals, casts, and binary arithmetic operands in Sema and CIRGen. The fixture declares an `int` from `decltype(integer + 1)` and returns 0; CTest is 201/201.
- **2026-11-11T01:14:59-08:00** — `decltype` now returns the built-in `double`/`float` types for floating binary operands, so `decltype(real * 2.0)` works. The fixture also asserts a double result; CTest remains 201/201.
- **2026-11-11T01:27:43-08:00** — `printf` `%f` now applies width, left alignment, `+`, space, and zero padding through a buffered formatter. The fixture prints four padded values; CTest is 202/202.
- **2026-11-11T02:25:26-08:00** — Completed approved phases 1–2 of the tree plan: moved KR32 runtime source to `targets/kr32/runtime`, added `targets/common/runtime`, staged the sysroot at `build/<preset>/stage/sysroot` with generated bits, moved host binaries to `host/bin`, moved test images to `stage/tests`, staged `libkrcrt.kro` and `crt0.s`, linked fixtures from the staged runtime, and added `--sysroot`, `krc_sysroot_override`, and the `krc_tree_boundary` guard. All six configurations pass 204/204 CTest; `krc_tests` remains 98 cases / 585 assertions. KDBG portable source paths remain an open item.
- **2026-11-11T02:37:53-08:00** — Implemented C `goto` and labels: labels are pre-created as blocks, forward and backward jumps branch correctly, dead statements between a jump and a label are skipped, and jumps across a live cleanup scope are rejected. The fixture returns 6; all six configurations pass 205/205 CTest.
- **2026-11-11T02:49:41-08:00** — Implemented C `switch` by lowering cases to an ordered comparison chain with case blocks, default, fallthrough, and `break`. The fixture checks all four forms; all six configurations pass 206/206 CTest.
- **2026-11-11T03:02:43-08:00** — Direct `volatile` locals and globals now mark their IR loads and stores; the fixture checks the IR flag and links/runs. All six configurations pass 208/208 CTest.
- **2026-11-11T03:13:20-08:00** — Scalar `volatile` initialization stores now carry the IR flag too, and a second IR test asserts the initial store. All six configurations pass 209/209 CTest.
- **2026-11-11T03:24:40-08:00** — Volatile object members accessed with `.` now inherit the flag on loads, stores, and bit-field read-modify-write; the fixture checks a volatile struct member and a third IR test asserts `member.load`. All six configurations pass 210/210 CTest.
- **2026-11-11T03:34:37-08:00** — Volatile pointer-to-struct members accessed through a direct local/global pointer identifier now inherit the flag too; the fixture checks both `.` and `->` updates. All six configurations pass 210/210 CTest.
- **2026-11-11T03:45:24-08:00** — KR16 GEP now handles constant array/struct indices and nonzero offsets, and zero-initialized globals are emitted as data. The new `kr16_data.c` image runs to exit 6 on the KR16 VM; all six configurations pass 211/211 CTest.
- **2026-11-11T04:00:59-08:00** — Fixed KR16 assembler sizing for extended `MOV` immediates and added byte `LDB`/`STB` for i8 accesses. String literals now work: `kr16_string.c` exits 0 on the KR16 VM, with a new assembler unit test; all six configurations pass 212/212 CTest.
- **2026-11-11T04:10:22-08:00** — KR16 dynamic GEP indices now scale by element size; `values[index]` with a runtime index works, the new `kr16_index.c` image exits 3 on the KR16 VM, and all six configurations pass 213/213 CTest.
- **2026-11-11T07:56:55-08:00** — KR16 now handles `ConstantUndef` return values, so `switch` functions lower and run; `kr16_switch.kex` returns 15 on the KR16 VM, and all six configurations pass 214/214 CTest.
- **2026-11-11T08:10:22-08:00** — KR16 compare labels are now unique per function, fixing repeated loop calls to branchy callees; `kr16_call_loop.kex` returns 15 on the KR16 VM, and all six configurations pass 215/215 CTest.
- **2026-11-11T08:21:16-08:00** — Added KR16 `MUL`/`DIV`/`DIVU`/`MOD`/`MODU` encoding and codegen; `kr16_arith.kex` exercises all three C operators and exits 49 on the KR16 VM, and all six configurations pass 216/216 CTest.
- **2026-11-11T08:30:37-08:00** — Added KR16 register-count `SHL`/`SHR`/`SAR`; `kr16_shift.kex` exercises left, right, and complement operations and exits 32 on the KR16 VM, and all six configurations pass 217/217 CTest.
- **2026-11-11T08:40:16-08:00** — KR16 now emits scalar global initializers as data; `kr16_pointer.kex` updates a global through a pointer and exits 7 on the KR16 VM, and all six configurations pass 218/218 CTest.
- **2026-11-11T08:51:51-08:00** — Intermediate array subscripts now decay to element pointers, fixing multidimensional indexing on both targets; corrected KR16 scaling for 4- and 8-byte GEP elements. `matrix2d.c` returns 3 on both VMs, and all six configurations pass 220/220 CTest.
- **2026-11-11T09:01:24-08:00** — KR16 indirect calls now load the callee into `R0` and use `CALL R0`, and function values get their real address; `kr16_fnptr.kex` calls through a pointer and exits 5, and all six configurations pass 221/221 CTest.
- **2026-11-11T09:11:17-08:00** — Pointer subtraction now converts both pointers to integers and divides by the pointee size; `kr16_ptrdiff.kex` returns 3, and all six configurations pass 222/222 CTest.
- **2026-11-11T09:21:42-08:00** — Bit-field aggregate initializers now use shifted read-modify-write stores; `bitfields_init.c` returns 22 on both VMs, and all six configurations pass 224/224 CTest.
- **2026-11-11T09:31:35-08:00** — Flat designated bit-field initializers now share the read-modify-write path; `bitfields_designated.c` returns 22, and all six configurations pass 225/225 CTest.
- **2026-11-11T09:41:49-08:00** — Nested designated bit-field paths now route through the same read-modify-write store; `bitfields_nested.c` returns 22, and all six configurations pass 226/226 CTest.
- **2026-11-11T09:53:26-08:00** — Added `krc -c --target=kr16 --emit-object` and a KR16 KRO object writer with text and symbols, covered by a round-trip unit test. Relocations and the KR16 linker remain; all six configurations pass 227/227 CTest with 100 doctest cases / 606 assertions.
- **2026-11-11T10:01:08-08:00** — The object path now accepts `.s` input, with a test object from `entry.s`; `targets/kr16/runtime/crt0.s` is present but still needs external `main` relocations. All six configurations pass 228/228 CTest.
- **2026-11-11T10:13:08-08:00** — KR16 external absolute references now emit 16-bit KRO relocations, calls lower through `MOV R0,#callee; CALL R0`, and `crt0.s` builds to an object. All six configurations pass 229/229 CTest with 101 doctest cases / 615 assertions.
- **2026-11-11T10:26:43-08:00** — Completed the KR16 relocatable link path under ADR 0019/D-041: every label-based MOV/LEA is relocated, `krld` links target ID 2 at `0x0100`, and the KEX writer separates load base from entry. Linked `hello.c` prints and exits 0, `sum.c` exits 55, and a two-object image exits 42. All six configurations pass 231/231 CTest.
- **2026-11-11T10:32:54-08:00** — Added deterministic double-build checks for KR16 KRO objects and linked KR16 images. All four determinism checks pass; all six configurations pass 460/460 CTest with 101 doctest cases / 615 assertions.
- **2026-11-11T10:34:07-08:00** — Wrote [docs/abi-kr16.md](docs/abi-kr16.md), covering the IP16 data model, registers, frame layout, indirect calls, globals, startup, relocations, and deferred features; linked it from the docs index, README, and ABI overview.
- **2026-11-11T10:44:52-08:00** — Added a compilable KR16 string/memory runtime, a staged `libkrcrt.kro` target, and a startup+runtime+user link fixture. Long KR16 functions now use `JMP` plus short inverted conditional branches. All six configurations pass 234/234 CTest.
- **2026-11-11T10:54:05-08:00** — Expanded the KR16 runtime with `strncpy`, `strncat`, `strchr`, `strrchr`, `strstr`, `atoi`, `strtol`, `strtoul`, `abs`, and `putchar`; fixed KR16 frame allocation so array/record locals reserve their full size. The linked runtime fixture exits 3; all six configurations pass 460/460 CTest.
- **2026-11-11T10:59:33-08:00** — Added a bump allocator (`malloc`, `free`, `calloc`, `realloc`), `strdup`, `rand`/`srand`, and `strtok` to the KR16 runtime; the linked fixture exercises them and exits 3. All six configurations pass 234/234 CTest.
- **2026-11-11T11:06:19-08:00** — Added KR32 host-service wrappers for console input and file I/O, with a linked fixture that creates and writes a file. All six configurations pass 235/235 CTest.
- **2026-11-11T11:11:59-08:00** — Extended the KR32 host-service wrappers with `unlink`, `rename`, `stat`, `truncate`, `getcwd`, `chdir`, `dup`, and `dup2`; the file I/O fixture now writes, renames, and unlinks. All six configurations pass 235/235 CTest.
- **2026-11-11T11:17:45-08:00** — Added `sysroot/include/krc/host.h` with the KR32 host-service prototypes and open flags; the linked file I/O fixture now uses the header. All six configurations pass 235/235 CTest.
- **2026-11-11T11:25:49-08:00** — Separated the KR32 syscall stubs from their C wrappers, mapped negative host results to `-1` plus `errno`, gave every runtime link the host stubs by default, and tested a failed open. All six configurations pass 235/235 CTest.
- **2026-11-11T11:31:38-08:00** — Added a linked KR16 fixture for local, copied, nested, and array-of-struct values; it runs to exit 81. All six configurations pass 236/236 CTest.
- **2026-11-11T11:32:42-08:00** — Documented the KR32 host-service layers, vectors, flags, errno mapping, sandbox, and KR16 status in [docs/host-services.md](docs/host-services.md), and refreshed the KR16 ABI summary and freeze checklist.
- **2026-11-11T11:38:39-08:00** — Made KR16 `puts` emit a trailing newline and added `getchar` alongside `putchar`; linked hello now prints a newline. All six configurations pass 236/236 CTest.
- **2026-11-11T11:48:15-08:00** — Added `krld --source-root=DIR`, which rewrites KDBG source paths under the root to `/`-separated relative paths, with a CTest verifying hello records no absolute root. All six configurations pass 237/237 CTest.
- **2026-11-11T11:49:57-08:00** — Recorded the KDBG path decision in ADR 0020, marked the plan open item resolved, and made the normal runtime link helper emit project-relative source paths. All six configurations pass 237/237 CTest.
- **2026-11-11T12:02:05-08:00** — Added `ll` length parsing to `printf` and widened integer formatting to 64 bits; a linked fixture prints `-123 456 1c8`. All six configurations pass 238/238 CTest.
- **2026-11-11T12:13:10-08:00** — Fixed virtual destruction through a base pointer: the vtable destructor slot now targets the complete member-destructor wrapper, so `delete Base*` prints `derived` then `base`. Added a linked fixture; all six configurations pass 460/460 CTest.
- **2026-11-11T12:26:57-08:00** — Virtual calls inside a destructor now bind statically to the current class; a linked fixture confirms `Base::~Base` calling `report()` prints the base implementation. All six configurations pass 240/240 CTest.
- **2026-11-11T12:40:30-08:00** — Restricted base/member default initialization emission to constructors, fixing duplicate base construction from destructors and ordinary methods, and bound virtual calls in constructors to the current class. A linked fixture prints `base ctor` then `base f`; all six configurations pass 460/460 CTest.
- **2026-11-11T12:52:35-08:00** — Narrowed constructor/destructor virtual-call binding to receivers that are `this`, so calls on other objects still dispatch dynamically. A destructor fixture through a global base pointer now prints `value=9`; all six configurations pass 460/460 CTest.
- **2026-11-11T13:32:04-08:00** — Synthesized implicit default constructors for classes with base/member constructors or polymorphism, so stack and `new` construction call base and member constructors exactly once. Added a linked counter fixture; all six configurations pass 460/460 CTest.
- **2026-11-11T13:40:20-08:00** — Registered C++ `struct` tags as class names for construction syntax, so `Box box(7)` parses as a constructor-initialized declaration. Added a linked fixture returning 7; tier-2 builds pass 244/244.
- **2026-11-11T14:03:28-08:00** — Made `delete`/`delete[]` on null skip destruction and freeing, and replaced KR32 ICmp name-based labels with a per-function counter to avoid duplicate labels across repeated comparisons. Added a linked null-delete fixture; debug CTest is 245/245.
- **2026-11-11T14:05:23-08:00** — Added KR32 `strerror` for the runtime errno values and a linked fixture covering `ENOENT`, `EINVAL`, and an unknown code. Debug CTest is 246/246.
- **2026-11-11T14:07:38-08:00** — Added KR32 `perror` on top of `strerror` and the console, declared it in `<stdio.h>`, and exercised `open: No such file or directory` in the linked fixture. Debug CTest is 246/246.
- **2026-11-11T14:10:22-08:00** — Added KR32 stdio over the host file wrappers (`fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fflush`, `getc`/`fgetc`/`fputc`, `remove`) and stream access for `stdin`/`stdout`/`stderr`. A linked fixture writes, reads, and removes a file; debug CTest is 247/247.
- **2026-11-11T14:13:22-08:00** — Extended KR32 stdio with `fgets`, `getline`/`getdelim`, `ungetc`, `vfprintf`, `fprintf`, `vprintf`, `vsnprintf`, and `setvbuf`; the linked fixture round-trips `abc-7`, reads it back, and returns 0. Debug CTest is 247/247.
- **2026-11-11T14:36:18-08:00** — Added the `## Gate verification` section and recorded the 2026-11-11 Windows gate per configuration, with native Linux x64 and Linux ARM64 listed as unverified.
- **2026-11-11T14:39:53-08:00** — Parsed braced default member initializers, so `Value member{7}` constructs the member with its constructor. Added a linked fixture returning 7; debug CTest is 248/248.
- **2026-11-11T14:52:12-08:00** — Added external global declarations to IR under ADR 0021. `extern` objects emit `.extern` and no storage, a later definition in the same translation unit replaces the declaration, and two-object links resolve shared globals. Added two linked fixtures; all four host configurations pass 250/250.
- **2026-11-11T14:59:15-08:00** — Added KR16 external globals: definitions emit `.global` and declarations emit `.extern`. A two-object fixture links and runs to exit 7. Windows debug CTest is 251/251.
- **2026-11-11T15:01:17-08:00** — Filled in `strtok_r`, `strndup`, `strcoll`, `strxfrm`, and `memccpy`; a linked fixture exercises all five and returns 0. Windows debug CTest is 252/252.
- **2026-11-11T15:06:12-08:00** — Added internal global linkage under ADR 0022. C `static` globals emit local symbols, so two objects with `static int counter` link; a two-object fixture returns 3. Host matrix passes 253/253.
- **2026-11-11T15:10:31-08:00** — Added `sscanf` for integer, string, and character conversions with width and suppression, and completed `\a`, `\b`, `\f`, `\v`, and `\?` escapes. Linked fixtures cover both; Windows debug CTest is 255/255.
- **2026-11-11T15:12:28-08:00** — Added hexadecimal character escapes (`'\xNN'`), matching the existing string decoder. A linked fixture checks `A`, `127`, and two string bytes; Windows debug CTest is 256/256.
- **2026-11-11T15:19:05-08:00** — Added `vsscanf`, `vfscanf`, `fscanf`, and `scanf` on top of the scanner core, with a linked file-input fixture. Windows debug CTest is 257/257.
- **2026-11-11T15:20:49-08:00** — Added `time`, `difftime`, and `clock` over the host time service, with a linked fixture. Windows debug CTest is 258/258.
- **2026-11-11T15:22:36-08:00** — Added `labs`, `atoll`, `strtoll`, `strtoull`, and `_Exit`, with a linked fixture. Windows debug CTest is 259/259.
- **2026-11-11T15:24:19-08:00** — Implemented the complete KR32 ctype surface, with a linked fixture covering every predicate and both case conversions. Windows debug CTest is 260/260.
- **2026-11-11T15:26:04-08:00** — Added `fmin` and `fmax`, completing the declared math surface, with a linked fixture. Windows debug CTest is 261/261.
- **2026-11-11T15:35:22-08:00** — Added `fputs` and `putc`, with a linked write/read-back fixture. Windows debug CTest is 262/262.
- **2026-11-11T15:39:03-08:00** — Parsed `Value x = Value(args)` into direct constructor initialization, so C++17 copy elision avoids a temporary. The linked fixture returns 10; Windows debug CTest is 263/263.
- **2026-11-11T15:40:53-08:00** — Added `setbuf`, `fgetpos`, `fsetpos`, and `freopen`, with a linked file-position fixture. Windows debug CTest is 264/264.
- **2026-11-11T15:48:17-08:00** — Added `gmtime`, `localtime`, `mktime`, `strftime`, `asctime`, and `ctime` with civil-date conversion and a linked formatting fixture. Windows debug CTest is 265/265.
- **2026-11-11T15:51:02-08:00** — Added `tmpfile` and `tmpnam`, with a linked temporary-stream fixture. Windows debug CTest is 266/266.
- **2026-11-11T15:56:36-08:00** — Added `<unistd.h>` with `getcwd` and `chdir` over the existing host services, plus a linked fixture. Windows debug CTest is 267/267.
- **2026-11-11T15:58:38-08:00** — Exposed `open`, `close`, `read`, `write`, `seek`, and `unlink` through `<unistd.h>` with POSIX open flags, and added a linked descriptor I/O fixture. Windows debug CTest is 268/268.
- **2026-11-11T16:06:07-08:00** — Exposed `dup`, `dup2`, `truncate`, and `stat` through `<unistd.h>`, with a linked descriptor-duplication fixture. Windows debug CTest is 269/269.
- **2026-11-11T16:09:57-08:00** — Enabled `assert` with `__krc_assert_fail`, and added a passing-assertion fixture. Windows debug CTest is 270/270.
- **2026-11-11T16:18:01-08:00** — Added `<strings.h>` with case-insensitive `strcasecmp` and `strncasecmp`, plus a linked fixture. Windows debug CTest is 271/271.
- **2026-11-11T16:19:59-08:00** — Added `strsep`, with a linked tokenization fixture. Windows debug CTest is 272/272.
- **2026-11-11T16:23:13-08:00** — Added `%F`, `%T`, `%R`, `%x`, and `%c` `strftime` composites, with a linked fixture. Windows debug CTest is 273/273.
- **2026-11-11T16:29:55-08:00** — Added `dprintf` and `vdprintf` over descriptor writes, with a linked formatting fixture. Windows debug CTest is 274/274.
- **2026-11-11T16:32:25-08:00** — Added `getdelim` and routed `getline` through it, with a linked delimiter fixture. Windows debug CTest is 275/275.
- **2026-11-11T16:34:25-08:00** — Added `fdopen` and `fileno`, with a linked descriptor-to-stream fixture. Windows debug CTest is 276/276.
- **2026-11-11T16:41:25-08:00** — Added `memmem`, with a linked binary-search fixture. Windows debug CTest is 277/277.
- **2026-11-11T16:44:06-08:00** — Fixed `void *` versus object-pointer comparisons by bitcasting to the concrete pointer type, with a linked fixture. Windows debug CTest is 278/278.
- **2026-11-11T16:48:20-08:00** — Added `strerror_r`, with a linked bounded-copy fixture. Windows debug CTest is 279/279.
- **2026-11-11T16:56:35-08:00** — Added `<locale.h>` with `setlocale` and `localeconv`, fixing the runtime layout by including the header. A linked fixture returns 0; Windows debug CTest is 280/280.
- **2026-11-11T17:00:43-08:00** — Fixed `auto copy = record_local` so the copy inherits the record type, without regressing lambda inference. The linked fixture returns 3; Windows debug CTest is 281/281.
- **2026-11-11T17:03:18-08:00** — Verified friend-function access to a private member with a linked fixture returning 7. Windows debug CTest is 282/282.
- **2026-11-11T17:10:25-08:00** — Added `%Z` and `%z` to `strftime` as UTC, with a linked fixture. Windows debug CTest is 283/283.
- **2026-11-11T17:14:03-08:00** — Parsed static data member definitions and qualified access, mangling `S::value` to a global symbol; a linked fixture sets 9 and returns 9. Windows debug CTest is 284/284.
- **2026-11-11T17:16:49-08:00** — Added `%C`, `%D`, `%h`, `%n`, and `%t` to `strftime`, with a linked fixture. Windows debug CTest is 285/285.
- **2026-11-11T17:24:23-08:00** — Supported static member functions: Sema accepts `S::get()` from the record method table and IRGen passes a null receiver. A linked fixture returns 7; Windows debug CTest is 286/286.
- **2026-11-11T17:28:27-08:00** — Static methods now resolve unqualified class static data to the class-qualified global instead of a null-receiver field. A linked fixture returns 7; Windows debug CTest is 287/287.
- **2026-11-11T17:33:57-08:00** — Supported in-class static data initialization (`static const` and `inline static`), creating the class-qualified global and Sema symbol. A linked fixture returns 12; Windows debug CTest is 288/288.
- **2026-11-11T17:40:58-08:00** — Added linked coverage for `static constexpr` members, `decltype` copies, and `using` aliases; each returns 7. Windows debug CTest is 291/291.
- **2026-11-11T17:43:55-08:00** — Parsed trailing method qualifiers and `mutable`, with a linked fixture incrementing a mutable member in a `const` method and returning 2. Windows debug CTest is 292/292.
- **2026-11-11T17:51:07-08:00** — Parsed nested class types such as `Outer::Inner` as declaration types, with a linked fixture setting 3 and returning 3. Windows debug CTest is 293/293.
- **2026-11-11T17:53:20-08:00** — Covered constructor-initialized nested types (`Outer::Inner item(3)`), returning 3. Windows debug CTest is 294/294.
- **2026-11-11T18:01:02-08:00** — Supported out-of-line member definitions (`int S::get()`), attaching them to the class method table. A linked fixture returns 7; Windows debug CTest is 295/295.
- **2026-11-11T18:03:21-08:00** — Covered out-of-line methods with a parameter, returning 3. Windows debug CTest is 296/296.
- **2026-11-11T18:12:04-08:00** — Supported out-of-line constructors, including member initializer lists, by registering them in the constructor table. A linked fixture returns 3; Windows debug CTest is 297/297.
- **2026-11-11T18:14:44-08:00** — Supported out-of-line destructors with qualified `S::~S` parsing; a linked fixture observes one destruction. Windows debug CTest is 298/298.
- **2026-11-11T18:17:00-08:00** — Covered out-of-line virtual methods dispatched through a base pointer, returning 7. Windows debug CTest is 299/299.
- **2026-11-11T18:24:05-08:00** — Covered static member arrays with an out-of-class definition, returning 2. Windows debug CTest is 300/300.
- **2026-11-11T18:26:36-08:00** — Supported nested static member definitions (`Outer::Inner::value`) and covered them with a linked fixture returning 3. Windows debug CTest is 301/301.
- **2026-11-11T18:29:13-08:00** — Supported nested out-of-line methods by resolving the final class segment, with a linked fixture returning 3. Windows debug CTest is 302/302.
- **2026-11-11T18:36:21-08:00** — Covered out-of-line static methods called as `S::get()`, returning 7. Windows debug CTest is 303/303.
- **2026-11-11T18:38:42-08:00** — Covered delegating constructors, returning 7. Windows debug CTest is 304/304.
- **2026-11-11T18:46:29-08:00** — Covered out-of-line static methods with parameters, returning 3. Windows debug CTest is 305/305.
- **2026-11-11T18:49:18-08:00** — Let out-of-line method bodies use IRGen class context and parsed qualified operator names. Linked fixtures: member access returns 7, out-of-line `operator=` returns 3. Windows debug CTest is 307/307.
- **2026-11-11T18:51:45-08:00** — Covered an out-of-line compound assignment operator (`operator+=`), returning 5. Windows debug CTest is 308/308.
- **2026-11-11T18:58:44-08:00** — Covered an out-of-line `operator[]`, returning 5. Windows debug CTest is 309/309.
- **2026-11-11T19:01:04-08:00** — Covered an out-of-line call operator through `s(2)`, returning 3. Windows debug CTest is 310/310.
- **2026-11-11T19:08:08-08:00** — Covered an out-of-line prefix `operator++`, returning 2. Windows debug CTest is 311/311.
- **2026-11-11T19:10:31-08:00** — Covered an out-of-line prefix `operator--`, returning 2. Windows debug CTest is 312/312.
- **2026-11-11T19:18:38-08:00** — Made C++ `bool` a keyword/type and covered an out-of-line `operator<`, returning 1. Windows debug CTest is 313/313.
- **2026-11-11T19:21:10-08:00** — Covered `bool` functions and `true`/`false` conditionals, returning 0. Windows debug CTest is 314/314.
- **2026-11-11T19:30:19-08:00** — Covered `bool` logical operators and literals, returning 0. Windows debug CTest is 315/315.
- **2026-11-11T19:35:15-08:00** — Supported `operator bool` conversion in conditions, with a linked fixture returning 0. Windows debug CTest is 316/316.
- **2026-11-11T19:41:39-08:00** — Applied `operator bool` to unary `!` on a record, with the linked conversion fixture still returning 0. Windows debug CTest is 316/316.
- **2026-11-11T19:46:27-08:00** — Parsed `explicit operator bool` member definitions and covered them with a linked fixture returning 0. Windows debug CTest is 317/317.
- **2026-11-11T19:54:11-08:00** — Covered `while (object)` through `operator bool`, returning 1. Windows debug CTest is 318/318.
- **2026-11-11T19:57:14-08:00** — Applied `operator bool` to `static_cast<bool>`, with a linked fixture returning 0. Windows debug CTest is 319/319.
- **2026-11-11T20:06:12-08:00** — Applied `operator int` to `static_cast<int>` and made `type_named` resolve builtin `int`/`bool`; a linked fixture returns 7. Windows debug CTest is 320/320.
- **2026-11-11T20:11:30-08:00** — Applied `operator int` to implicit record-to-int initialization, with a linked fixture returning 7. Windows debug CTest is 321/321.
- **2026-11-11T20:19:38-08:00** — Applied `operator int` to integer returns, with a linked fixture returning 7. Windows debug CTest is 322/322.
- **2026-11-11T20:22:39-08:00** — Applied `operator int` to function arguments, with a linked fixture returning 7. Windows debug CTest is 323/323.
- **2026-11-11T20:30:56-08:00** — Fell back to `operator int` in conditions when `operator bool` is absent, with a linked fixture returning 0. Windows debug CTest is 324/324.
- **2026-11-11T20:33:45-08:00** — Fell back to `operator int` for `!object`, with a linked fixture returning 0. Windows debug CTest is 325/325.
- **2026-11-11T20:42:18-08:00** — Generalized record numeric casts through conversion operators and covered `operator double`, returning 0. Windows debug CTest is 326/326.
- **2026-11-11T20:45:10-08:00** — Applied `operator double` to implicit floating initialization, with a linked fixture returning 0. Windows debug CTest is 327/327.
- **2026-11-11T20:53:25-08:00** — Applied `operator double` to floating arguments, with a linked fixture returning 0. Windows debug CTest is 328/328.
- **2026-11-11T20:56:18-08:00** — Applied `operator double` to floating returns, with a linked fixture returning 0. Windows debug CTest is 329/329.
- **2026-11-11T21:04:23-08:00** — Covered `operator float` through `static_cast<float>`, returning 0. Windows debug CTest is 330/330.
- **2026-11-11T21:07:26-08:00** — Selected a converting constructor for scalar copy-initialization, with a linked fixture returning 7. Windows debug CTest is 331/331.
- **2026-11-11T21:16:09-08:00** — Constructed record temporaries for scalar arguments through the converting constructor, with a linked fixture returning 7. Windows debug CTest is 332/332.
- **2026-11-11T21:19:10-08:00** — Constructed record returns from scalars through the converting constructor, with a linked fixture returning 7. Windows debug CTest is 333/333.
- **2026-11-11T21:27:35-08:00** — Selected converting constructors for string-literal copy-initialization, with a linked fixture returning 0. Windows debug CTest is 334/334.
- **2026-11-11T21:30:37-08:00** — Constructed record arguments and returns from string literals through the converting constructor, with linked fixtures returning 0. Windows debug CTest is 336/336.
- **2026-11-11T21:38:56-08:00** — Covered `bool` and `nullptr` copy-initialization through converting constructors, with linked fixtures returning 0. Windows debug CTest is 338/338.
- **2026-11-11T21:41:41-08:00** — Covered braced copy-initialization through the converting constructor, returning 7. Windows debug CTest is 339/339.
- **2026-11-11T21:50:00-08:00** — Covered `new S[2]` default construction and `delete[]`, returning 7. Windows debug CTest is 340/340.
- **2026-11-11T21:52:47-08:00** — Covered `delete[]` running the destructor for every element, returning 2. Windows debug CTest is 341/341.
- **2026-11-11T22:01:26-08:00** — Resolved unqualified static data member access inside methods, with a linked fixture returning 2. Windows debug CTest is 342/342.
- **2026-11-11T22:06:16-08:00** — Fixed KR32 argument loading so float arguments cannot clobber the `this` register, unlocking `S(float)`/`S(double)` constructors and `S s = 1.5f`; three linked fixtures return 0. Windows debug CTest is 345/345.
- **2026-11-11T22:15:21-08:00** — Covered `new S(1.5f)` and mixed integer/float call arguments after the ordering fix, returning 0 and 4. Windows debug CTest is 347/347.
- **2026-11-11T22:18:10-08:00** — Covered a mixed `int`/`float` signature returning `float`, with a linked fixture returning 0. Windows debug CTest is 348/348.
- **2026-11-11T22:26:51-08:00** — Covered a constructor with `int` and `float` parameters, returning 0. Windows debug CTest is 349/349.
- **2026-11-11T22:30:34-08:00** — Supported functional-cast record temporaries in returns and initialization, with linked fixtures returning 0. Windows debug CTest is 351/351.
- **2026-11-11T22:39:45-08:00** — Default-constructed and destroyed local record arrays, with linked fixtures returning 7 and 2. Windows debug CTest is 353/353.
- **2026-11-11T22:42:31-08:00** — Confirmed local array destructors run on function return, with a linked fixture returning 2. Windows debug CTest is 354/354.
- **2026-11-11T22:54:36-08:00** — Destroyed array members element by element, with a linked fixture returning 2. Windows debug CTest is 355/355.
- **2026-11-11T22:58:07-08:00** — Default-constructed array members element by element, with a linked fixture returning 7. Windows debug CTest is 356/356.
- **2026-11-11T23:08:00-08:00** — Ran global constructors for array members, with a linked fixture returning 7. Windows debug CTest is 357/357.
- **2026-11-11T23:10:48-08:00** — Covered `delete` destroying array members, with a linked fixture returning 2. Windows debug CTest is 358/358.
- **2026-11-11T23:18:58-08:00** — Constructed array members for `new Outer`, with a linked fixture returning 7. Windows debug CTest is 359/359.
- **2026-11-11T23:21:57-08:00** — Covered a function-local `static` record constructor, returning 7. Windows debug CTest is 360/360.
- **2026-11-11T23:30:45-08:00** — Confirmed a function-local static record constructs exactly once, returning 1. Windows debug CTest is 361/361.
- **2026-11-11T23:33:55-08:00** — Constructed function-local static record arrays through a guard, with a linked fixture returning 7. Windows debug CTest is 362/362.
- **2026-11-11T23:42:06-08:00** — Covered `delete[]` destroying array members of every element, returning 4. Windows debug CTest is 363/363.
- **2026-11-11T23:45:55-08:00** — Confirmed a function-local static record array constructs its elements once, returning 2. Also generated looped destructor calls for global array members; that path is not directly observable from `main`.
- **2026-11-11T23:54:14-08:00** — Constructed array members for `new Outer[2]`, with a linked fixture returning 7. Windows debug CTest is 365/365.
- **2026-11-11T23:57:13-08:00** — Covered a `using Base::method;` declaration in a derived class, returning 7. Windows debug CTest is 366/366.
- **2026-11-12T00:05:37-08:00** — Covered an inherited constructor through `using B::B;`, with a linked fixture returning 7. Windows debug CTest is 367/367.
- **2026-11-12T00:09:00-08:00** — Parsed `constexpr`/`inline` constructor specifiers and covered a `constexpr` global object, returning 7. Windows debug CTest is 368/368.
- **2026-11-12T00:17:23-08:00** — Covered a `constexpr` function and `static_assert`, returning 9. Windows debug CTest is 369/369.
- **2026-11-12T00:20:55-08:00** — Covered `if constexpr` selection, with a linked fixture returning 7. Windows debug CTest is 370/370.
- **2026-11-12T00:30:26-08:00** — Parsed `final` class specifiers and covered inheritance from one, returning 7. Windows debug CTest is 371/371.
- **2026-11-12T00:33:30-08:00** — Covered a defaulted constructor, with a linked fixture returning 7. Windows debug CTest is 372/372.
- **2026-11-12T00:41:55-08:00** — Covered a `static constexpr` data member, returning 7. Windows debug CTest is 373/373.
- **2026-11-12T00:45:05-08:00** — Covered a `noexcept` function, with a linked fixture returning 7. Windows debug CTest is 374/374.
- **2026-11-12T00:54:40-08:00** — Parsed namespace aliases and resolved a qualified call through one, returning 7. Windows debug CTest is 375/375.
- **2026-11-12T00:58:11-08:00** — Parsed nested namespace definitions and resolved `A::B::f`, returning 7. Windows debug CTest is 376/376.
- **2026-11-12T01:07:36-08:00** — Parsed inline namespaces and resolved `A::f` through the inline member, returning 7. Windows debug CTest is 377/377.
- **2026-11-12T01:10:46-08:00** — Covered `using namespace A;` exposing an unqualified function, returning 7. Windows debug CTest is 378/378.
- **2026-11-12T01:19:33-08:00** — Added a negative test requiring `static_assert(false, ...)` to fail. Windows debug CTest is 379/379.
- **2026-11-12T01:23:30-08:00** — Parsed trailing return types and covered `auto f() -> int`, returning 7. Windows debug CTest is 380/380.
- **2026-11-12T01:32:13-08:00** — Covered record copies carrying array members, returning 7. Windows debug CTest is 381/381.
- **2026-11-12T01:35:22-08:00** — Covered copy-assigning a record with array members, returning 7. Windows debug CTest is 382/382.
- **2026-11-12T01:39:11-08:00** — Added the C++ `alignof` keyword and covered it in `static_assert` and a return, returning 0. Windows debug CTest is 383/383.
- **2026-11-12T01:49:11-08:00** — Registered out-of-line inc/dec methods by parameter count and covered postfix `s++`, returning 1. Windows debug CTest is 384/384.
- **2026-11-12T01:52:21-08:00** — Covered out-of-line postfix decrement, returning 3. Windows debug CTest is 385/385.
- **2026-11-12T02:01:22-08:00** — Covered a `decltype(variable)` declaration, returning 7. Windows debug CTest is 386/386.
- **2026-11-12T02:05:40-08:00** — Bound range-for references to record elements and covered the sum, returning 7. Windows debug CTest is 387/387.
- **2026-11-12T02:14:33-08:00** — Covered `auto` and `auto &` range-for variables, returning 7 and 9. Windows debug CTest is 389/389.
- **2026-11-12T02:17:45-08:00** — Covered a friend function reading a member, returning 7. Windows debug CTest is 390/390.
- **2026-11-12T02:27:10-08:00** — Covered a `mutable` counter changed by a `const` method, returning 1. Windows debug CTest is 391/391.
- **2026-11-12T02:30:25-08:00** — Covered a friend class reading members, returning 7. Windows debug CTest is 392/392.
- **2026-11-12T02:39:12-08:00** — Covered `reinterpret_cast` through `void *`, returning 7. Windows debug CTest is 393/393.
- **2026-11-12T02:42:30-08:00** — Covered `switch` over an `enum class`, returning 7. Windows debug CTest is 394/394.
- **2026-11-12T02:51:33-08:00** — Covered the `if constexpr (false)` else branch, returning 7. Windows debug CTest is 395/395.
- **2026-11-12T02:54:45-08:00** — Covered a move constructor selected by an rvalue cast, returning 7. Windows debug CTest is 396/396.
- **2026-11-12T02:58:03-08:00** — Covered a move assignment operator selected by an rvalue cast, returning 7. Windows debug CTest is 397/397.
- **2026-11-12T03:07:59-08:00** — Parsed `extern "C"` function declarations and covered a linked call, returning 7. Windows debug CTest is 398/398.
- **2026-11-12T03:11:44-08:00** — Parsed `extern "C" { ... }` blocks and covered a linked call, returning 7. Windows debug CTest is 399/399.
- **2026-11-12T03:21:06-08:00** — Covered a pure virtual base with an `override` implementation, returning 7. Windows debug CTest is 400/400.
- **2026-11-12T03:24:19-08:00** — Covered dispatch through a base pointer to an abstract base, returning 7. Windows debug CTest is 401/401.
- **2026-11-12T03:34:09-08:00** — Parsed pure virtual destructor declarations and covered delete through a base pointer, returning 7. Windows debug CTest is 402/402.
- **2026-11-12T03:34:45-08:00** — Refreshed the M10 evidence list with range-for, decltype, alignof, friends, mutable, move semantics, pure virtuals, extern C, reinterpret_cast, enum switch, and if constexpr coverage. Windows debug CTest remains 402/402.
- **2026-11-12T03:44:49-08:00** — Made reference casts lvalues in IRGen and covered `static_cast<int &>` and `const_cast<int &>`, returning 7. Windows debug CTest is 404/404.
- **2026-11-12T03:48:07-08:00** — Covered `sizeof(record)` in `static_assert` and an expression, returning 7. Windows debug CTest is 405/405.
- **2026-11-12T03:58:01-08:00** — Converted record operands in `&&`/`||` through `operator bool`, returning 7. Windows debug CTest is 406/406.
- **2026-11-12T04:01:17-08:00** — Covered a record operand of `||`, returning 7. Windows debug CTest is 407/407.
- **2026-11-12T04:10:56-08:00** — Covered fixed-point `printf` precision (`%.2f`), returning 0, and refreshed the M9 formatting note. Windows debug CTest is 408/408.
- **2026-11-12T04:14:27-08:00** — Covered `%e` and `%g` formatting alongside `%.2f`, returning 0. Windows debug CTest is 409/409.
- **2026-11-12T04:18:55-08:00** — Implemented `getenv`, `setenv`, and `unsetenv` over a runtime table and covered their contract, returning 0. Windows debug CTest is 410/410.
- **2026-11-12T04:32:18-08:00** — Implemented `atexit` with a weak default in `crt0.s`, ran handlers from normal return and `exit`, and covered the path returning 7. Windows debug CTest is 411/411.
- **2026-11-12T04:36:17-08:00** — Covered `_Exit` bypassing registered handlers, returning 7. Windows debug CTest is 412/412.
- **2026-11-12T04:45:57-08:00** — Implemented `at_quick_exit` and `quick_exit`, covered the handler path returning 7. Windows debug CTest is 413/413.
- **2026-11-12T04:49:33-08:00** — Covered a const global object constructed before `main`, returning 7. Windows debug CTest is 414/414.
- **2026-11-12T04:53:47-08:00** — Constructed record return values from braced initializers and covered the path returning 7. Windows debug CTest is 415/415.
- **2026-11-12T05:04:06-08:00** — Parsed functional braced casts and covered `S{7}`, returning 7. Windows debug CTest is 416/416.
- **2026-11-12T05:08:02-08:00** — Materialized call-result temporaries for member access and covered `make().value`, returning 7. Windows debug CTest is 417/417.
- **2026-11-12T05:18:02-08:00** — Materialized call-result temporaries as method receivers and covered `make().get()`, returning 7. Windows debug CTest is 418/418.
- **2026-11-12T05:21:59-08:00** — Parsed braced `new` arguments and covered `new S{7}`, returning 7. Windows debug CTest is 419/419.
- **2026-11-12T05:32:01-08:00** — Materialized call-result temporaries for conversion operators, covered `static_cast<int>(make())`, returning 7. Windows debug CTest is 420/420.
- **2026-11-12T05:36:06-08:00** — Materialized call-result records in conditions and covered `if (make())`, returning 7. Windows debug CTest is 421/421.
- **2026-11-12T05:46:07-08:00** — Materialized call-result records in logical operators and covered `make() && true`, returning 7. Windows debug CTest is 422/422.
- **2026-11-12T05:50:06-08:00** — Materialized returned temporaries for implicit scalar initialization and covered `int x = make()`, returning 7. Windows debug CTest is 423/423.
- **2026-11-12T06:00:19-08:00** — Materialized returned temporaries for implicit return conversions and covered `return make()`, returning 7. Windows debug CTest is 424/424.
- **2026-11-12T06:04:52-08:00** — Materialized returned temporaries for implicit argument conversions and covered `take(make())`, returning 7. Windows debug CTest is 425/425.
- **2026-11-12T06:16:07-08:00** — Materialized returned temporaries for implicit assignment conversions and covered `result = make()`, returning 7. Windows debug CTest is 426/426.
- **2026-11-12T06:20:20-08:00** — Converted returned temporaries for integer comparisons and covered `make() == 7`, returning 7. Windows debug CTest is 427/427.
- **2026-11-12T06:32:21-08:00** — Prevented structural operands from entering float promotion and covered `make() > 1.0`, returning 7. Windows debug CTest is 428/428.
- **2026-11-12T06:36:25-08:00** — Covered `static_cast<double>(make())`, returning 7. Windows debug CTest is 429/429.
- **2026-11-12T06:47:54-08:00** — Converted returned temporaries for arithmetic and covered `make() + 1`, returning 8. Windows debug CTest is 430/430.
- **2026-11-12T06:52:11-08:00** — Covered `auto` inference from a returned record temporary, returning 7. Windows debug CTest is 431/431.
- **2026-11-12T07:03:30-08:00** — Materialized returned temporaries for unary `!` and covered `!make()`, returning 7. Windows debug CTest is 432/432.
- **2026-11-12T07:08:04-08:00** — Materialized returned temporaries for unary `-` and `~`, covered `-make()` producing -7. Windows debug CTest is 433/433.
- **2026-11-12T07:19:42-08:00** — Bound a call-result temporary to a const reference local and covered its member read, returning 7. Windows debug CTest is 434/434.
- **2026-11-12T07:23:48-08:00** — Materialized a temporary for a reference parameter and covered `take(make())`, returning 7. Windows debug CTest is 435/435.
- **2026-11-12T07:34:32-08:00** — Converted returned temporaries for `switch` conditions and covered `switch (make())`, returning 7. Windows debug CTest is 436/436.
- **2026-11-12T07:38:31-08:00** — Covered a returned temporary as a `while` condition, returning 7. Windows debug CTest is 437/437.
- **2026-11-12T07:48:51-08:00** — Covered a two-argument braced record return, returning 7. Windows debug CTest is 438/438.
- **2026-11-12T07:52:39-08:00** — Covered an aggregate returned from a braced initializer, returning 7. Windows debug CTest is 439/439.
- **2026-11-12T08:04:09-08:00** — Initialized braced `new` array elements and covered `new int[3]{1, 2, 3}`, returning 6. Windows debug CTest is 440/440.
- **2026-11-12T08:08:33-08:00** — Initialized record elements in a braced `new` array and covered `new P[2]{{1, 2}, {3, 4}}`, returning 4. Windows debug CTest is 441/441.
- **2026-11-12T08:19:23-08:00** — Covered a `new` array with an empty bound and braced initializer, returning 6. Windows debug CTest is 442/442.
- **2026-11-12T08:23:21-08:00** — Covered a record array with an empty bound and braced elements, returning 3. Windows debug CTest is 443/443.
- **2026-11-12T08:35:06-08:00** — Initialized a scalar `new` allocation and covered `new int(7)`, returning 7. Windows debug CTest is 444/444.
- **2026-11-12T08:39:35-08:00** — Zero-initialized a value-initialized scalar allocation and covered `new int()`, returning 0. Windows debug CTest is 445/445.
- **2026-11-12T08:51:37-08:00** — Zero-initialized a braced `new` array before storing elements and covered `new int[3]{}`, returning 0. Windows debug CTest is 446/446.
- **2026-11-12T08:55:44-08:00** — Covered a partial braced `new` array initializer, returning 1. Windows debug CTest is 447/447.
- **2026-11-12T09:07:00-08:00** — Covered `std::move` selecting a move constructor, returning 7. Windows debug CTest is 448/448.
- **2026-11-12T09:11:07-08:00** — Covered `std::move` selecting a move assignment operator, returning 7. Windows debug CTest is 449/449.
- **2026-11-12T09:22:53-08:00** — Treated `std::forward` as a pass-through rvalue and covered it selecting a move constructor, returning 7. Windows debug CTest is 450/450.
- **2026-11-12T09:27:28-08:00** — Implemented `std::swap` for lvalue scalars and covered it, returning 7. Windows debug CTest is 451/451.
- **2026-11-12T09:38:54-08:00** — Covered `std::swap` on two records, returning 7. Windows debug CTest is 452/452.
- **2026-11-12T09:43:38-08:00** — Implemented `std::min` and `std::max` for integers and covered them, returning 37. Windows debug CTest is 453/453.
- **2026-11-12T09:55:22-08:00** — Extended `std::min` and `std::max` to doubles and covered `std::min(1.5, 0.5)`, returning 7. Windows debug CTest is 454/454.
- **2026-11-12T09:59:34-08:00** — Covered default construction of a braced class array, returning 7. Windows debug CTest is 455/455.
- **2026-11-12T10:12:07-08:00** — Selected the constructor for placement new and covered `new (storage) S(7)`, returning 7. Windows debug CTest is 456/456.
- **2026-11-12T10:28:20-08:00** — Fixed a duplicate evaluation of call initializers (which doubled `printf` output), observed with the `../kr32` corpus, and covered single evaluation returning 7. Windows debug CTest is 457/457.
- **2026-11-12T10:46:56-08:00** — Accepted namespace-qualified type names in declarations and `new`, found while compiling the `../kr32` matrix example, and covered `ns::S` returning 10. Windows debug CTest is 458/458.
- **2026-11-12T10:58:26-08:00** — Parsed `thread_local` as static storage and compiled the `../kr32` TLS example, which exposed the open 64-bit division gap. A linked fixture returns 7. Windows debug CTest is 459/459.
- **2026-11-12T11:03:07-08:00** — Accepted `thread_local` at block scope as a static local and covered it, returning 7. Windows debug CTest is 460/460.
- **2026-11-12T10:12:07-08:00** — Refreshed the M10 evidence list for temporary materialization, braced/placement `new`, and the supported `std` utilities. Documentation only; Windows debug CTest remains 456/456.
- **2026-11-12T11:15:37-08:00** — Added `handoff.md` with machine-transfer state, build and gate procedure, completed surfaces, open M8/M9/M10 work, known correctness limits, corpus status, and traps. Documentation only; the 11:03 gate remains 460/460.
- **2026-11-12T20:31:28-08:00** — Normalized CRLF when the sysroot-bits test compares generated headers with the checked-in ones. The test failed on this machine only because `core.autocrlf` checked `sysroot/*/include/bits/*.h` out with CRLF. Windows debug CTest is 460/460.
- **2026-11-12T20:42:35-08:00** — Lowered `long long` on KR32 as a low/high word pair through the backend: pair load/store, 64-bit constants, add/subtract/multiply/bitwise/shift/compare, truncation and extension, 64-bit arguments and returns, and `__krc_*di3` runtime helpers in `targets/kr32/runtime/libc.c`. Added `MULH`/`MULHU` to the KR32 assembler and encoder with golden words, `krc_link_longlong_arith`, a long-long object determinism check, and ADR 0023. `krc_link_longlong_arith` links; its runtime exit code is not yet observed because the `../kr32` VM was being rebuilt. Host matrix, incremental: windows-msvc-debug, windows-msvc-release, linux-gcc-debug, and linux-gcc-release each 462/462. This is a matrix run, not a gate.
- **2026-11-12T21:34:32-08:00** — Recorded `= delete` and `= default` as written instead of reading both as a pure specifier, and diagnosed `use of deleted function` for a member or qualified call; `= default` no longer marks its class as abstract for IR generation. Added `krc_cxx_deleted_function`, which asserts the rejection and the message, and `cmake/CheckDiagnostic.cmake`. Windows debug CTest is 463/463; host matrix, incremental: windows-msvc-debug, windows-msvc-release, linux-gcc-debug, and linux-gcc-release each 463/463. This is a matrix run, not a gate.
- **2026-11-12T21:47:39-08:00** — Recorded access specifiers on member declarations, defaulted a `class` to private and `struct`/`union` to public, and reported access to a non-public member from a free function unless it is a declared friend; friend declarations are now recorded instead of being read as methods. Added `krc_cxx_private_member_access` and `krc_cxx_protected_member_access`. Windows debug CTest is 465/465; host matrix, incremental: windows-msvc-debug, windows-msvc-release, linux-gcc-debug, and linux-gcc-release each 465/465. This is a matrix run, not a gate.
- **2026-11-12T22:13:07-08:00** — Gave out-of-line member bodies class scope: the definition resolves its class, an unqualified name falls back to the defining class and its bases, and `this` is declared as a pointer local, so Sema now analyzes those bodies instead of skipping any name containing `::`. Access control extends to them, allowing the defining class, derived classes for protected members, and declared friends. Added `krc_cxx_out_of_line_private_access` and `krc_link_out_of_line_protected_access`; the nine previously failing `krc_link_out_of_line_*` fixtures pass. Host matrix, incremental: windows-msvc-debug, windows-msvc-release, linux-gcc-debug, and linux-gcc-release each 467/467. This is a matrix run, not a gate.
- **2026-11-12T22:15:18-08:00** — Recorded `friend class` declarations and granted them access, and stopped reading such a declaration as an anonymous member: a class holding one `int` and naming a friend with two fields reported `sizeof` 12 instead of 4. Added `krc_link_friend_class_access`, which asserts both the access and the unchanged size through `static_assert`. Windows debug CTest is 468/468.
- **2026-11-12T22:44:51-08:00** — Analysed in-class member bodies, the last body form Sema skipped. Required visiting bodies after the whole record is registered, registering static data members at class scope whether or not they carry an initializer, and resolving an unqualified call to a member function of the defining class or a base; access control now covers in-class bodies too. Added `krc_cxx_in_class_private_access`. Windows debug CTest is 469/469.
- **2026-11-12T23:00:52-08:00** — Rejected `long long` on the KR16 IP16 profile with `'long long' is not supported by the kr16 profile` instead of lowering it to a zero-width integer type, and guarded the 64-bit declarations in `sysroot/include/stdlib.h` under `__kr16__`. Added `krc_kr16_long_long_rejected` and `-DTARGET` support in `cmake/CheckDiagnostic.cmake`; the same source still compiles for KR32. Floating point stays undiagnosed until the shared math headers are profile-conditional, recorded in `docs/targets/kr16.md`.
- **2026-11-12T23:14:53-08:00** — Rejected `float`, `double`, and `long double` on the KR16 IP16 profile, completing the profile checks that plan.md §4.3 requires; `<math.h>` now hides every declaration there, `<stdlib.h>` hides `atof`/`strtod` alongside the 64-bit conversions, and `<time.h>` hides `difftime`. Added `krc_kr16_float_rejected`; including the three headers still compiles on KR16 and the floating-point sources still compile on KR32. Windows debug CTest is 471/471.
- **2026-11-12T23:24:50-08:00** — Ran the 64-bit fixture under the `../kr32` VM once that tree had committed its changes. Verified: a two-operand `long long` division returns 42 (the previous handoff reported 0), `krc_link_longlong_arith` exits 0 with a 1.6-million-step budget so its eight checks pass at runtime, a 64-bit value survives a fixed parameter and a return, and `va_arg` reads both words. Found a new defect: `printf` renders a 64-bit argument from its low word only, so `%lld`/`%llu` of 5000000001 both print 705032705; the caller and `va_arg` are correct, so the loss is in `format_output`'s length handling in the KR32 runtime. Recorded as an open limit rather than fixed.
- **2026-11-12T23:30:00-08:00** — Localized the printf symptom to a compiler defect: a 64-bit conditional expression is truncated to its low word. `printf("%llx", 0x100000001LL)` prints `100000001`, so the runtime path is correct; the minimal reproducer fails at `value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value`, while the same cast without the conditional is exact. `emit_condition_expression` infers the result type with a local `infer_type` that returns null for an integer literal, so a `cast + literal` branch supplies no type. Not fixed this round; recorded with the reproducer.
- **2026-11-12T23:29:58-08:00** — Fixed the 64-bit conditional truncation found by the previous run. The backend lowered `BitCast` by loading the operand's low word and zeroing the high one, but the conditional lowering emits an `i64`-to-`i64` bitcast per branch, so every 64-bit conditional lost its high word; `printf("%lld")` was only the visible symptom. A cast between equal-width values now copies both words, and a narrower destination still stores the low one. `krc_link_longlong_conditional` covers four branch shapes and exits 0 under the VM, and `longlong_arith` now prints all four of its values correctly. Windows debug CTest is 472/472.
- **2026-11-12T23:40:00-08:00** — Measured the `../kr32` corpus under the VM: 13 of 15 examples compile, link, and run, with `cxx_tls.cpp` returning the 42 its `long long` division is meant to produce. Two blockers remain at compile time (`FileOpen.cpp`, `matrix.cpp`), and two programs — `binarytree.c` and `balanced-tree.cpp` — halt with statuses 115 and 36 despite `main` returning 0 and the assembly emitting `MOV R0, #0`, printing no tree. Recorded as M8 evidence with the anomalies left open rather than smoothed over.
- **2026-11-12T23:43:30-08:00** — Narrowed the corpus failure rather than resolving it. Ruled out: a synthesised `HALT` in the generated code (none), the return-value emission (assembly ends `MOV R0, #0`), the recursion plus `putchar` indentation plus `printf("%s\n")` shape (a reduced program prints correctly and exits 0), and the exit status surviving `printf`/`malloc`/`strcmp` (it does). Both failing programs build a linked tree from `malloc`'d nodes and then walk it, so the remaining suspect is a corrupted child pointer during construction; left open with that evidence.
- **2026-11-12T23:55:00-08:00** — Bisected the corpus tree failure to the AVL balance path. Reduced copies of `binarytree.c` at 20 inserts print correctly and exit 0; 22, 24, 26, 28, and 30 print no tree and return garbage statuses. Replacing `return balance_node(root);` with `return root;` makes all of them behave correctly, since the plain BST is then detected as unbalanced and the program prints its message and exits 1. Ruled out along the way: heap exhaustion (134 MB arena), a hand-written 26-node AVL tree of the same shape (works), and the print/recursion/exit paths. The fault is in `balance_node` and its rotations for particular shapes.
- **2026-11-13T00:05:00-08:00** — Characterized the corpus tree failure as corrupted control flow rather than a bad value. A 22-insert copy stops after four right descents even when the node name is replaced by a string literal, and `check_balanced` completes before printing, so the tree validates; the VM's halt PC disassembles as zeroed padding between sections, meaning execution left the function. A stand-alone recursive printer of the same shape runs to depth 7 and exits 0, so the remaining suspect is the recursive frame sequence (`PUSH R7`/`ADJSP`/`POP R7`/`RET`) with a variadic call in between. Recorded as a known defect with `bt20`/`bt22` reproducers instead of a fourth round on it.
- **2026-11-12T23:52:10-08:00** — Added executed tests: `cmake/RunKr32Image.cmake` runs a linked image under the KR32 VM and compares its exit status, registered for `longlong_arith` and `longlong_conditional` when `KRC_KR32_VM` resolves. A link-only test cannot see a wrong result in generated code, which is how a truncated 64-bit cast survived the suite; asking for a deliberately wrong status fails with `expected exit 3, got 0`, so the check is not vacuous. Windows debug CTest is 474/474. Also recorded that the corpus tree defect is not frame size: `print_tree` reserves 168 bytes, so six levels cannot overflow the stack, and `bt20`/`bt22` share identical code.
- **2026-11-13T00:01:39-08:00** — Extended the executed tests to compare program output, not only the exit status: `krc_run_printf_long_long` asserts the text `-123 456 1c8`, which fails if the 64-bit conversion regresses to the low word that a link-only test accepted. Verified the check bites by asking for the old truncated value. Windows debug CTest is 475/475.
- **2026-11-13T00:12:00-08:00** — Settled whose bug the corpus tree failure is: the same reduced source built with the sibling VM's own C compiler (`kr32.exe build bt22.c`) prints the whole tree and exits 0, so the defect is in krc code generation, not in the VM or the program, and `../kr32` is now a reference build to diff against. Also checked the generated assembly statically — every `[R7 + N]` use lies inside its function's own `ADJSP` frame, so an out-of-frame store is not the cause.
- **2026-11-13T00:24:00-08:00** — Corrected the corpus defect record and moved it forward. A `krld --map` file shows the halt address is exactly `putchar`, not padding: `.text` spans `0x10000`–`0x3CBB8`, and my earlier "ran off the end" arithmetic put the section end 64 bytes short, so that claim is withdrawn. `kr32 dis` reports zero words at that address, contradicting the map and `crt0.s`, so it is unusable for these non-flat images as evidence. A stand-alone `putchar` loop inside a recursive function to depth 10 works, so the console path is sound in general. The defect remains krc-only, with the reference build from `../kr32` as an oracle.
- **2026-11-13T00:22:07-08:00** — Made `-S -o FILE` write the assembly to that file instead of ignoring the path and printing to stdout; without `-o` it still prints. Fixed on both the KR32 and KR16 paths, the help text now says so, and `krc_c_emit_hello_asm_file` and `krc_kr16_emit_asm_file` assert that the file appears and holds assembly. Windows debug CTest is 477/477.
- **2026-11-13T00:36:05-08:00** — Diagnosed assignment through a const object: a member of a const record carries no qualifier of its own, so `constant.value = 3` and `pointer->value = 3` through a pointer-to-const were accepted. Both now report `assignment to a member of a const object`; assigning through a non-const object still compiles. Added `krc_cxx_const_member_assign` and `krc_cxx_const_pointer_member_assign`. Windows debug CTest is 479/479.
- **2026-11-13T00:50:00-08:00** — Completed the const-member rule with the `mutable` exemption: `mutable` travels on the declaration specifiers and is now recorded per member, so `constant.counter = 5` compiles while `constant.value = 3` still reports `assignment to a member of a const object`. Added `krc_link_const_mutable_member`. Windows debug CTest is 480/480.
