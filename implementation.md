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
| M7 KR16 backend + C | G7 | in progress | KR16 syntax, IP16 data model, IR, scalar codegen, assembler, and a minimal `.kex` image work; `hello.c` and `sum.c` run locally; relocatable objects, the full linker, and the wider runtime remain |
| M8 v1.0 hardening | G8 | pending | — |
| M9 v1.1 runtime/libc/host services | G9 partial | pending | — |
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

- [x] Create the repository tree: `include/krc/`, `src/frontend/`, `src/ir/`, `src/backend/`, `src/asm/`, `src/linker/`, `src/debug/`, `src/runtime/`, `src/tools/`, `tests/`, `sysroot/`, `tools/`, `examples/`.
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
- The sysroot has 16 common headers plus per-target `bits/types.h` and `bits/stdint.h` for KR32, KR16, and KR64. `krc_gen_sysroot_bits` generates those bits into the build tree and `krc` uses the generated directory before the checked-in fallback; `test_sysroot_bits` verifies the generator output matches the checked-in headers.
- `tests/unit/test_sema.cpp` covers scopes, duplicate declarations, struct members, function arity, arrays/pointers, enums, sizeof/offsetof, bit-fields, `_Alignas`, static assertions, K&R rejection, and unsupported features.
- M1 has no local checklist items left. The only open evidence item is the remote CI run, which cannot start until a GitHub remote exists. C++ remains M5.

### M2 — IR + KR32 backend + assembler (8–10 weeks)

- [x] Typed SSA IR model, builder, verifier, and textual printer.
- [ ] Freeze `docs/ir.md` and `docs/targets/kr32.md` before implementation. `docs/ir.md` is frozen by [ADR 0003](docs/decisions/0003-ir-core-freeze.md) and extended by [ADR 0004](docs/decisions/0004-ir-data-array-and-declarations.md); the KR32 register/instruction tables and their values remain the M2 descriptor task.
- [ ] C IRGen: scalars, aggregates, pointers, arrays, calls, varargs, `volatile`, debug locations. Scalars, pointers, arrays, calls, varargs calls, globals/string literals, returns, if/while/do/for, and break/continue are implemented; aggregates, `volatile`, debug locations, switch/goto/labels, and many conversions remain.
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
- [x] `crt0`, heap symbols, minimal libc (`putchar`, `puts`, `exit`), KR32 image writer. `src/runtime/kr32/crt0.s` and `src/runtime/kr32/libc.c` provide startup, console, `puts`, `printf` integer/string formats, and exit; the linker defines `__heap_start`/`__heap_end`/`__stack_top`/`__stack_limit`; floating-point formatting and the allocator remain.
- [x] Gate G2b: `hello.c` and `mainentry.c` assemble, link, and run on KR32. Both pass through `krc -c` + `krld` + the KR32 VM; `hello` prints `Hello, KR32!` and exits 0, and `mainentry` prints argc/argv and exits 0.

**M3 evidence update (2026-11-09T22:03:10-08:00):**

- `src/runtime/kr32/crt0.s` is assembled by the in-tree assembler through `krld`; `krld` now accepts `.s` inputs alongside KRO objects.
- `krld hello.kro src/runtime/kr32/crt0.s -o hello.k32` links and runs: output `Hello, KR32!`, exit 0.
- `krld mainentry.kro src/runtime/kr32/crt0.s -o mainentry.k32` links and runs to exit 0; `mainentry` prints argc/argv through the integer/string `printf` path.
- Local-label and data-symbol references now emit relocations, so runtime calls such as `CALL exit` and `LEA .str` are patched when sections are placed.
- `cmake/LinkRuntime.cmake` and `krc_link_hello`/`krc_link_puts` cover the object → link image path in CTest.
- `src/runtime/kr32/libc.c` supplies `puts`; `krc -c puts.c` + `krc -c libc.c` + `krld` + crt0 prints `hello from puts` and exits 0. The linker defines the four heap/stack boundary symbols.
- `krc_tests` has 86 cases / 462 assertions; all six configurations pass 21/21 CTest.
- The double-build determinism check now covers both a KR32 object and a KR16 `.kex`: `krc_determinism_object` and `krc_determinism_kr16_image` compile the same input twice and compare SHA-256 digests.

**M3 evidence (2026-11-09 21:49:58 -08:00, in progress):**

- `docs/object.md` and [ADR 0005](docs/decisions/0005-kro-object-format-freeze.md) freeze KRO v1: 120-byte header, fixed 64/40/32-byte section/symbol/relocation records, table order, string table, payload alignment, CRC32, build ID, and the relocation registry.
- `src/linker/kro_object.*` writes and reads KRO objects deterministically, sorts symbols, remaps relocation indices, validates magic/version/bounds/CRC, and round-trips a section/symbol/relocation fixture.
- `src/asm/kro_writer.*` converts the in-tree assembler result (including external relocations) into a KRO object; `krc -c file.c -o file.kro` emits one.
- `src/linker/linker.*` merges sections, resolves local/global/weak symbols, patches absolute and PC-relative relocations, validates target/ABI/pointer width, and writes a KR32 `.k32` executable with sections, symbols, strings, and CRC32.
- `krld objects... -o image` reads KRO files, links, and writes the K32 executable. Unit tests cover resolution, relocation patching, entry-symbol errors, KRO round-trips/CRC/determinism, and K32 header emission.
- `krc_tests` has 86 doctest cases; all six configurations pass 20/20 CTest.
- `src/runtime/kr32/crt0.s` provides `_start`, `putchar`, and `exit`; `src/runtime/kr32/libc.c` provides `puts` and integer/string `printf`.
- Remaining M3 hardening: weak/COMDAT, section GC, relaxation. `krld` extracts GNU/BSD KRO archive members, writes deterministic maps/section sizes, and honors NAME/ADDRESS layout placements.

**M4 evidence (2026-11-09T22:19:21-08:00, in progress):**

- [ADR 0006](docs/decisions/0006-recursive-aggregate-types.md) adds `struct_forward`/`complete_struct` to the IR type context so self-referential C records can be constructed.
- `CIRGen` maps `struct` records by `RecordInfo`, caches the forward type, completes fields, and lowers `.`/`->` member lvalues with struct GEPs; null pointer constants now coerce correctly.
- The KR32 backend computes recursive aggregate layout/alignment and lowers struct-member GEPs with field offsets.
- `tests/fixtures/c/struct.c` links/runs on KR32 and exits 5; the unit suite covers recursive struct IR and assembly.
- [ADR 0007](docs/decisions/0007-varargs-base-opcode.md) adds `vastart`; `stdarg.h` now defines `va_list` and real `va_start`/`va_arg`/`va_copy` macros. Integer varargs functions work end to end.
- `src/runtime/kr32/libc.c` supplies integer/string `printf`, `puts`, a free-list `malloc`/`free`/`calloc`/`realloc`, `_heapwalk`, and string/memory routines; function pointers and indirect calls work through `CALL Rn`. `mainentry` prints `argc`/`argv`, the varargs fixture prints `6`, the function-pointer fixture exits 5, and the extracted `binarytree.c` prints its AVL tree and exits 0.
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
- `sprintf` formats into a caller buffer through the same formatter as `printf`; `sprintf(text, "%d:%s:%03d", 7, "ok", 5)` writes `7:ok:005` and returns 8.
- `snprintf` bounds the write while returning the untruncated length: with a five-byte buffer, `snprintf(text, 5, "%s", "abcdef")` writes `abcd` and returns 6.
- `strtol`/`strtoul` parse optional signs and `0x`/octal prefixes with a base argument; `srand`/`rand` expose the existing LCG. The conversion fixture parses `0x1f` as 31, `-42`, and octal `17` as 15.
- Destructible function-local statics register with `__krc_destroy_globals` and run after user `main`. The fixture prints `ctor`, `main`, then `dtor`.
- `__func__` lowers to the source-level function name for free functions and methods; the fixtures verify `"main"` and `"method_name"`.
- Adjacent string literals concatenate their decoded contents, including prefixed literals; `"hello" " world"` compares equal to `"hello world"`.
- `krc_tests` has 98 cases / 585 assertions; all six configurations pass 145/145 CTest.

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
- `krc_tests` has 98 cases / 585 assertions; all six configurations pass 145/145 CTest.

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
- [ ] Codegen, frame layout, image writer. Scalar codegen, SP-relative frame layout, the in-tree assembler, and the minimal `.kex` writer work for single-unit integer/pointer code; relocatable objects and the full linker remain.
- [ ] Tier 0–1 C on KR16, including `hello.c` and `sum.c`. `sum.c` runs to exit 55 and `hello.c` prints `Hello, KR16!` and exits 0 through the single-unit `.kex` path; the relocatable object/linker and broader C runtime remain.
- [ ] Gate G7: KR16 C is accepted, assembled, linked, and run. The single-unit `hello.c`/`sum.c` path passes locally; the relocatable object and linker gate remains.

**M7 evidence (2026-11-10T10:36:06-08:00, in progress):**

- `krc --target=kr16 -std=c11 -fsyntax-only` accepts a fixture asserting 16-bit `int`/`long`/pointers and unsigned `char`.
- `krc --target=kr16 --dump-ir` emits `; target: kr16` with `i16` function signatures.
- `src/asm/kr16_encoder.*` encodes the R/I/M/M8/B/S/BIT/C/X formats, relative control transfers, shifts, bit operations, and range validation; golden tests cover the ISA examples and out-of-range rejection.
- `src/asm/kr16_assembler.*` assembles the backend subset: labels, `.text`/`.data`/`.global`/`.extern`, register and immediate ALU forms, loads/stores/LEA, shifts, bit operations, conditional branches, relative and register `JMP`/`CALL`, and the control instructions. An unresolved or unpaired label is rejected.
- `src/backend/kr16_backend.*` emits scalar integer/pointer functions: SP-relative frame slots, parameters in `R1`–`R3`, two-operand ALU forms, loads/stores, `LEA`, comparisons, branches, calls, and `ret`. `sum.c` assembles and runs locally to exit 55.
- [ADR 0016](docs/decisions/0016-kr16-scalar-abi.md) and D-040 freeze the M7 scalar calling convention: `R1`–`R3` arguments, `R0` return, `R4` FP, SP-relative frame.
- [ADR 0017](docs/decisions/0017-kr16-exec-image.md) and D-041 define the minimal `.kex` image: 64-byte header, `.text` at `0x0100`, `.debug_str`, CRC32, and a generated `_start` that calls `main` and halts. `krc -c --target=kr16` writes it.
- `sum.c` now compiles to assembly, assembles in-tree, writes a `.kex`, and runs on the KR16 VM to exit 55 without the external toolchain.
- `hello.c` prints `Hello, KR16!` and exits 0: string globals are emitted as `.byte` text data, `LEA` resolves their labels, and the generated image includes a minimal MMIO `puts` shim.
- Constant folding now applies integer cast width and signedness, so `(char)-1 > 0` is true on the unsigned-char KR16 profile.
- CTest is 118/118 across all six configurations; `krc_tests` is 98 cases / 585 assertions.

### M8 — v1.0 hardening (6–8 weeks)

- [ ] Full Tier 0–2 corpus green on KR32 and Tier 0–1 on KR16.
- [ ] Debugger parity for C and C++; deterministic outputs.
- [ ] Finish `docs/abi.md`, `object.md`, `symbols.md`, `ir.md`, `attributes.md`, target profiles.
- [ ] Release notes and tag `v1.0`.
- [ ] Gate G8.

### M9 — v1.1 runtime/libc/host services (8–10 weeks)

- [ ] Port KR32 `libc.c` (allocator, stdio, string, `strto*`, `qsort`, file I/O).
- [ ] Standard `printf` with length modifiers and `%p`; float formatting on KR32. `%p`, `%ld`/`%lu`/`%zu`, unsigned values, `%o`, width/zero/left alignment, integer precision, `+`/space signs, `*` width/precision, and `%.Ns`/`%Ns` strings work; the locale-aware set remains.
- [ ] Common host-service ABI and per-VM shims; kernel profile.
- [ ] Integrate KR16 `libc.s`/`resources/fileio.c`.
- [ ] Gate G9 partial: Tier 3 runtime baseline.

### M10 — v1.1 language/runtime completion (8–10 weeks)

- [ ] Table-driven exceptions and unwinding; RTTI/`dynamic_cast`/`typeid`.
- [ ] Variadic templates and fold expressions.
- [ ] Dynamic `thread_local` initialization/destruction.
- [ ] `memory`, `tuple`, `functional`, `array`.
- [ ] Full KR32 `libc.c`/`kr32os` and KR16 runtime integration.
- [ ] Tag `v1.1`; Gate G9.

## 5. Open items and conditions

Approval conditions: none.

Tracked open questions are in `plan.md` §17.2. The decision points that affect milestones are:

- Target-descriptor schema and C ABI frozen before M2/M5.
- KR16 ABI appendix before M7.
- EH record encoding ADR before M10.
- `libc.c` port strategy before M9.
- `printf` compatibility policy before M9.
- 64-bit VM/ISA specification before any 64-bit backend work.
- ARM64 host CI timing; third-party test-suite policy.

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
- **2026-11-09T22:03:10-08:00** — Closed G2b locally: added `src/runtime/kr32/crt0.s` and `.s` input support in `krld`. `krc -c hello.c` → `krld hello.kro crt0.s` → `hello.k32` prints `Hello, KR32!` and exits 0; the same path links/runs `mainentry` to exit 0 with the printf bring-up stub. Local-label/data references now emit relocations so linked absolute addresses are patched. Added the `krc_link_hello` CTest. `krc_tests` has 86 cases / 459 assertions; all six configurations pass 20/20 CTest.
- **2026-11-09T22:06:08-08:00** — Extended the KR32 runtime with a C `puts` (`src/runtime/kr32/libc.c`), pointer-increment IRGen support, and linker-defined heap/stack boundary symbols. `krc -c puts.c` + `krc -c libc.c` + `krld` + crt0 prints `hello from puts` and exits 0. Added `krc_link_puts`; `krc_tests` now has 86 cases / 462 assertions and all six configurations pass 21/21 CTest.
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
