# Documentation audit — 2026-11-27

**Status:** record of one audit pass. The living documents were updated in the same pass; this
file lists what could not be changed without approval, because the text is frozen or the plan is
approved, plus the code defects the audit found. Nothing here has been applied.

**Method:** four parallel read-only audits compared the living documents with `src/`, `targets/`,
`sysroot/`, `CMakeLists.txt`, and the gate scripts; this session then checked every cited line
before editing anything. No build, test, assembler, linker, or VM run was performed.

**To answer:** each item below asks one question with its options and a recommendation, and
section D repeats them with the cost of each choice. Write a letter after `Your answer:`
(`A1: b`) in this file or reply with the list; a dash means nothing is decided.

**Labels:** BLOCKS (the next step cannot proceed), FIX-BEFORE (workable now, must be resolved
before the next milestone or merge), ERRATA (correctness or clarity, no schedule impact).

## A. Frozen or approved text that disagrees with the code

Each item needs an Appendix A entry, and the format items also need an ADR revision. The document
side is not edited until the decision is made.

| # | Finding | Evidence | Resolution | Label |
|---|---|---|---|---|
| A1 | KR232 `float` is 8 bytes in the compiler, 4 in the accepted ABI. | `src/target/target.cpp:160` sets `float_bits` 64; `tests/unit/test_target.cpp:31` asserts it; `docs/abi-kr232.md:20` says 4; ADR 0057 line 56 says 32/64. | Decide binary32 (fix the profile, the test, and `sysroot/kr232/include/bits/float.h`) or binary64 (amend ADR 0057 and the appendix). | BLOCKS |
| A2 | KR216's profile passes aggregates in registers; the frozen ABI says memory only. | `src/target/target.cpp:140` is `RegistersOrMemory`; ADR 0053 line 44 and `docs/abi-kr216.md:130` say `MemoryOnly`; IRGen reads the profile (`src/irgen/c_irgen_setup.cpp:1217`). | Set the profile to `MemoryOnly` and add a descriptor test, or amend ADR 0053 and the appendix. | BLOCKS |
| A3 | KR216's alignment model exists in three incompatible forms. | `docs/abi-kr216.md:18` says `long` aligns 2; Sema returns `min(size, 8)` (`src/frontend/sema.cpp:5195`), IRGen `ceil(bits/8)` (`src/irgen/c_irgen_expressions.cpp:4267`), the backend 4 or 8 (`src/backend/kr216_backend.cpp:30`). | Add the alignment values to ADR 0053 and make Sema, IRGen, and the backend read them. | BLOCKS |
| A4 | `this` is documented in R10; the code passes it as IR argument 0. | `docs/abi-kr216.md:74` and `docs/abi-kr232.md:82` say R10; `src/irgen/c_irgen_setup.cpp:1470` adds `this` as parameter 0; `src/backend/kr216_backend.cpp:721` and `kr232_backend.cpp:1729` store it from R2. | Move `this` to R10 in both backends, or document it as argument 0 and mark `this_register` invalid. | FIX-BEFORE |
| A5 | KR232 documents R8 as callee-saved, but every function uses it as scratch and saves nothing. | `docs/abi-kr232.md:56`, `:93`, `:120`; `src/backend/kr232_backend.cpp:115`, `:1641`, `:1673` (`enter #frame, #0`). | Save R8 in `ENTER`'s mask, or make R8 caller-saved and update the runtime stubs. | FIX-BEFORE |
| A6 | KR232's indirect-call form is documented with the target in R7; the code puts it in R8. | `docs/abi-kr232.md:91`; `src/backend/kr232_backend.cpp:2390-2412` (`ld r7,#back; push r7; jmp r8`). | Correct the appendix, or emit `call r7` as documented. | FIX-BEFORE |
| A7 | KR232 kernel images have no 4-byte `.reset` at `0x0100`. | `docs/abi-kr232.md:242`, ADR 0058 lines 71/89; `src/linker/kr232_image.cpp:169` emits only linked sections (the kr216 writer synthesises its reset, `kr216_image.cpp:105-113`). | Implement the section for kernel images, or amend ADR 0058. | FIX-BEFORE |
| A8 | KR232's "no heap, no `atexit`, `dynamic_cast` fails at link" claims are false for the shipped runtime. | `docs/abi-kr232.md:191`, `:216-219`; `targets/kr232/runtime/libc_full.c:19` defines `__krc_dynamic_cast`, `:805` `malloc`, `:2900` `atexit`. | Name which runtime 1.x describes and align the Deferred list and `has_rtti`. | FIX-BEFORE |
| A9 | Variadic and unprototyped calls are documented as all-stack; the front end drops `has_prototype`, so they lower as ordinary calls. | `docs/abi-kr216.md:147`, `docs/abi-kr232.md:155`; `src/frontend/sema.cpp:148`, `src/irgen/c_irgen_setup.cpp:1205-1206`, `src/backend/kr216_backend.cpp:1601`. | Carry `has_prototype` into IR and the backends, or narrow the rule to variadic callees. | FIX-BEFORE |
| A10 | KR32's "FP-only aggregate of 9–16 bytes in two FPRs" is implemented nowhere. | `docs/abi.md:51`; `src/backend/kr32_backend.cpp:231-238` returns sret above 8 bytes, `:2618-2635` passes structs word-wise. | Implement the class, or delete the clause and keep ADR 0001's >8 rule. | FIX-BEFORE |
| A11 | KR32's 16-byte public call alignment is declarative only. | `docs/abi.md:49`; frames round to 8 (`src/backend/kr32_backend.cpp:366`); the descriptor value has no consumer outside an encoder test. | Add 16-byte frame and call alignment, or reduce the frozen value. | FIX-BEFORE |
| A12 | KR32's FP return is documented as F0/F1; every result, including `double`, uses F0. | `docs/abi.md:49`; `src/backend/kr32_backend.cpp:2510`, `:2729`. | Implement the pair, or correct the wording to F0. | FIX-BEFORE |
| A13 | KR216's stack top is `0xFEF0` in the appendix, `0xF000` in the linker. | `docs/abi-kr216.md:212`; `src/linker/linker.cpp:608-609`. | Agree the value with the VM's reset SP and set `__stack_top` accordingly. | FIX-BEFORE |
| A14 | Descriptor schema v2 is only partly the profile's shape, and the document disagrees with itself on the version. | `docs/targets/schema.md:5` says v2, `:18` says `1`; `include/krc/target/target.h:48-79` has no `schema_version`, `max_alignment`, `stack_alignment`, `sret_threshold`, `varargs_slots`, `tls_model`, or `eh_rtti`; only the kr232 descriptor carries `this_register`. | Decide the v2 field set and version, then add the fields or revise the schema. | FIX-BEFORE |
| A15 | The frozen IR document contradicts the implemented lowering and levels. | `docs/ir.md:80-84` says the backends have no `phi` lowering (both emit edge copies: `kr32_backend.cpp:1475`, `kr16_backend.cpp:594`); `:147` says no inlining in v1, but `-O2`/`-Os` run `inline_small_functions` (`src/toolchain/toolchain.cpp:676`, `src/tools/krc/main.cpp:561`); verifier rules 7/10/12 accept any pointer and do not count operand lanes (`src/ir/ir.cpp:1949`, `:2027`, `:2101`). | One Appendix A entry for lowering and levels, one decision for each verifier rule (enforce or restate). | FIX-BEFORE |
| A16 | The frozen object document's build-ID recipe, relocation validation, and reader validation do not match the code, and K232 and archives are undocumented. | `docs/object.md:96-98`, `:107-109`, `:114-115`; `src/linker/kro_object.cpp:317-336` (hash material), `:384-391` (no reader validation); `src/linker/linker.cpp:68-71` (no type/width pair check); `src/linker/archive.cpp:9` (`!<arch>\n`); `src/image/k232.cpp:18-53` (K232 reader). | ADR entries for the build-ID recipe, relocation pairs, and reader validation; add K232 and archive sections to the document. | FIX-BEFORE |
| A17 | The frozen KDBG document's `.kdbg_var` field order is wrong, its CRC scope is unstated, and `pointer_width` is never set. | `docs/symbols.md:60`; `src/debug/kdbg.cpp:324-332` writes offset, type, size (the decoder at `:608-615` agrees); `:464-470` is a whole-payload CRC; `src/linker/linker.cpp:736-751` never sets `pointer_width`, so every payload records 4 (`include/krc/debug/kdbg.h:119`). | ADR 0009 revision for the field order and CRC coverage; set `pointer_width` for 16-bit targets and document its units. | BLOCKS |
| A18 | The approved plan's target summary and flags are stale. | `plan.md` Appendix C (`:1647-1666`) has no KR232 row and says KR32/KR216 EH/RTTI are v1.1; D-050 (`:1598`) says KR216's flags stay off until the 16-bit EH descriptor, which ADR 0055 supplied; D-050's link at `:1598` is `decisions/0053-…` and misses `docs/`. | One Appendix A entry updating Appendix C and D-050's wording, plus the link fix. | FIX-BEFORE |
| A19 | The KR32/KR16 mangling section describes Itanium `_Z` names; member symbols are emitted as `Class_method`. | `docs/abi.md:58`; `src/irgen/c_irgen_internal.h:200` (`tag + "_" + name`). | Record the actual scheme or mark the section as unimplemented until mangling lands. | FIX-BEFORE |

## B. Code defects found while auditing

| # | Finding | Evidence | Label |
|---|---|---|---|
| B1 | KR232's `float_bits` 64 is deliberate in the test ("one floating-point format"), so A1 is a decision, not a typo. | `tests/unit/test_target.cpp:31`; `src/target/target.cpp:160`. | BLOCKS (same decision as A1) |
| B2 | The profile's capability flags contradict shipped behaviour, and nothing reads them. | `has_cpp` false for KR16 (`src/target/target.cpp:72`) though KR16 C++ runs (`krc_run_kr16_recursive_variadic`); `has_rtti` false for KR32 (`:37`) though ADR 0025 implements it. | ERRATA |
| B3 | A comment on the KR232 profile says `backend_available` is false until M3, next to the `true`. | `src/target/target.cpp:145` versus `:175`. | ERRATA |
| B4 | `getchar` returns the raw negative host code, contradicting the layer's negate-to-`-1` rule. | `targets/kr32/runtime/libc.c:1582-1584` versus the rule at `:1574-1580`. | ERRATA |
| B5 | Public-header comments are stale: `optimize` "0 or 1", and "Both VM backends produce" the debug bridge. | `include/krc/toolchain.h:87-89`, `:84-86`. | ERRATA |

## C. Link errata

| # | Finding | Evidence |
|---|---|---|
| C1 | ADR 0032 links a file that does not exist. | `docs/decisions/0032-class-template-instantiation.md`: `0027-destructor-cleanup.md`; the file is `0027-exception-cleanup.md`. |
| C2 | The plan's D-050 link names a root-relative path. | `plan.md:1598`: `decisions/0053-kr216-target-profile.md`; it resolves only as `docs/decisions/…`. |
| C3 | The reorganisation plan links the pre-move runtime paths. | `docs/planning/source-tree-reorganization.md`: `../../src/runtime/kr32/libc.c`; the files live under `targets/kr32/runtime/`. |

## D. Questions for the owner

**Answered 2026-11-27: every question took its recommended option.** The receipt is the table
below; the question text after it is the original questionnaire and is kept as the record of what
was asked. "Applied" means the change is in this tree; "pending" means it needs the code batch and
a build.

| ID | Answer |
|---|---|
| A1 | (b) Amend the ABI to one 64-bit format |
| A2 | (a) Set KR216 `aggregate_rules` to `MemoryOnly` |
| A3 | (a) Amend the appendix to the size-based alignment |
| A4 | (b) Document `this` as argument 0, `this_register` reserved |
| A5 | (a) Declare KR232 R8 caller-saved |
| A6 | (a) Correct the appendix to the implemented indirect call |
| A7 | (a) Implement the KR232 `.reset` section |
| A8 | (a) Update the appendix to `libc_full.c` |
| A9 | (a) Carry `has_prototype` into IR |
| A10 | (b) Delete the two-FPR clause |
| A11 | (a) Implement 16-byte KR32 call alignment |
| A12 | (b) Correct the FP return to F0 |
| A13 | (c) Check the VM first; fallback (b) |
| A14 | (a) Revise `schema.md` to the implemented profile |
| A15a | (a) Amend the IR document for `phi` |
| A15b | (a) Amend the IR document for levels and inlining |
| A15c | (a) Tighten rules 7/10, restate rule 12 |
| A16a | (a) Document the implemented build-ID recipe |
| A16b | (a) Add the relocation type/width check |
| A16c | (a) Add KRO reader validation |
| A16d | (a) Add K232 and archive sections to `object.md` |
| A17a | (a) Correct the `.kdbg_var` field order in the document |
| A17b | (a) State the whole-payload CRC coverage |
| A17c | (a) Set `pointer_width` from the target |
| A18 | (a) One Appendix A entry for Appendix C and D-050 |
| A19 | (a) Mark the Itanium scheme unavailable |
| B2 | (a) Correct the capability flags and consult them |
| B4 | (a) Wrap `getchar` |
| B3/B5/C1–C3 | (a) Apply all errata |

**Status after the answers.** Contract text applied — A1, A3, A4, A5, A6, A8, A10, A12,
A13 (fallback (b)), A14, A15a, A15b, A15c (rule 12 restated), A16a, A16d, A17a, A17b,
A17c (document side), A18 (Appendix A entry D-053 and Appendix C), A19, and C1–C3.
Code applied — A2 (KR216 `MemoryOnly`), A7 (KR232 `.reset`, with its image tests), A9
(`unprototyped` through IR, IRGen, the parser, the verifier, and four backends), A11
(KR32 frames 8 mod 16), A15c (`ptr<i8>` checks), A16b (relocation pairs), A16c (KRO
reader checks), A17c (`pointer_width`), B2's flag corrections and C++-mode diagnostic,
and B3–B5.

**A2 needed an IRGen fix.** The first flip failed `vector__Item_push_back` with
`store type does not match its pointer`: an instantiated template body could not recover
the frontend parameter type that marks a hidden-pointer parameter indirect.
`src/irgen/c_irgen.cpp` now prefers the type recorded for the instance over the pattern
AST's declaration type, so `cpp_vector_objects.cpp` compiles under `MemoryOnly`, and
`guest_cpp_vector_objects` and `guest_aggregate_param` pass on KR216.

**Not applied — the `has_rtti` gate.** KR16 records `has_rtti` false, but
`krc_run_kr16_dynamic_cast_void` passes on KR16, so the gate would turn a passing
executed row into a diagnostic.

**Verification:** compiled and tested. `windows-msvc-debug` and `windows-msvc-release`
are 1012/1012, the family gate passes krcompiler's four rows (1012 rows registered in
both configurations), and kr216's row is 57/58 with only the documented
`density_benchmark` calibration failure. The family Linux x64 wave prints
`GATE LINUX X64: PASS` with 758/758 on gcc-debug, gcc-release, and clang-debug. In the
family summary 17 of 20 rows pass; besides kr216's density row, kr232's row fails in its
own host build under MSVC 14.52 with warnings as errors (its `test_memory.cpp`,
`<string>`, and `<chrono>`), unrelated to this tree. See the gate record in
`implementation.md`.

Each question maps to the evidence row of the same number above. Answer with a letter (`A1: b`) in this file or in your reply; a dash means nothing has been decided, and nothing below has been applied.

### A1 — KR232 `float` width

- **(a) Restore binary32.** Set `float_bits` to 32, regenerate `sysroot/kr232/include/bits/float.h`, drop the "float is double" assertion in `test_target.cpp`, and add the widen-on-load / round-on-store step to the kr232 backend.
- **(b) Amend the ABI to one 64-bit format.** ADR 0057 and `abi-kr232.md` record `float_bits` 64, and the kr232 plan owner is told the profile deviates from plan §2.5.
- **(c) Defer.** Keep the code, mark the appendix draft, and reopen at the M3 freeze.
- **Recommendation: (b).** The one-format model is deliberate and tested, and binary32 widening is new compiler work rather than a documentation correction. The cost is an ABI change that needs the sibling plan's agreement.
- **Your answer:** —

### A2 — KR216 aggregate rules

- **(a) Set the profile to `MemoryOnly`.** One line in `target.cpp`, plus a descriptor test that a small aggregate travels as a hidden pointer.
- **(b) Amend ADR 0053 and the appendix** to `RegistersOrMemory` and describe the register class.
- **Recommendation: (a).** ADR 0053 and the appendix already describe the memory class, and the KR16 path for it exists.
- **Your answer:** —

### A3 — KR216 alignment

- **(a) Amend the appendix to the implemented rule** (alignment is `min(size, 8)`, so `long` 4 and `double` 8) and note it in ADR 0053.
- **(b) Make Sema, IRGen, and the backend use the appendix's 2-byte alignment.**
- **(c) Add alignment fields to the target descriptor** and have all three read them.
- **Recommendation: (a).** The compiler is internally consistent and no external reader sees guest struct layout; (c) is the better long-term shape but is a descriptor change for a later pass.
- **Your answer:** —

### A4 — `this` register

- **(a) Implement R10** as ADR 0053/0057 and D-050 say: `this` stops consuming an argument slot and both backends read it from R10.
- **(b) Document `this` as argument 0**, mark `this_register` reserved, and amend D-050 and both appendices.
- **Recommendation: (b).** The code is uniform across both targets and nothing observable depends on R10; (a) changes IRGen call setup and both backends for no behavioral gain.
- **Your answer:** —

### A5 — KR232 R8

- **(a) Declare R8 caller-saved** in the appendix and correct the callee-saved list; update any runtime stub that assumes otherwise.
- **(b) Save R8** by setting its bit in every `ENTER` mask and restoring it on `LEAVE`.
- **Recommendation: (a).** R8 is the address scratch in every function, so the current text promises something the compiler never delivered; (b) taxes every call to keep a promise no fixture relies on.
- **Your answer:** —

### A6 — KR232 indirect calls

- **(a) Correct the appendix** to the implemented sequence (`ld r8,#target; ld r7,#back; push r7; jmp r8`).
- **(b) Change the backend** to the documented `call r7` form.
- **Recommendation: (a).** The ISA has no register call and the two forms are equivalent; (b) rewrites working code to match prose.
- **Your answer:** —

### A7 — KR232 `.reset`

- **(a) Implement it**: a 4-byte section at `0x0100` holding the entry word for kernel images, as the kr216 writer does.
- **(b) Amend ADR 0058 and the appendix** to say kernel images carry no `.reset`.
- **Recommendation: (a).** The loader contract is already frozen that way and the kr216 writer is a working model.
- **Your answer:** —

### A8 — KR232 runtime claims

- **(a) Update the appendix** to the shipped `libc_full.c`: heap, `atexit`, `__krc_dynamic_cast`, and a corrected Deferred list and `has_rtti`.
- **(b) Declare the minimal runtime canonical** and move `libc_full.c` to a benchmark-only role.
- **Recommendation: (a).** The kr232 test rows link `libc_full.c`, so it is the runtime the ABI is observed against.
- **Your answer:** —

### A9 — Unprototyped calls

- **(a) Carry `has_prototype` into IR** and lower an unprototyped call as all-stack, as the appendix says.
- **(b) Narrow the appendix rule** to variadic callees and accept that `int f(); f(1,2,3)` uses the normal argument registers.
- **Recommendation: (a).** The current lowering is a genuine ABI hole for a legal C program; (b) would make the appendix describe a different language.
- **Your answer:** —

### A10 — KR32 9–16-byte FP-only aggregates

- **(a) Implement the two-FPR class** for an FP-only argument of 9–16 bytes.
- **(b) Delete the clause** and keep ADR 0001's "above 8 bytes is sret".
- **Recommendation: (b).** ADR 0001 is the frozen rule, no fixture needs the class, and (a) adds a second classification path.
- **Your answer:** —

### A11 — KR32 16-byte call alignment

- **(a) Implement it**: round every frame to 16 and keep SP 16-aligned at each call, with a test.
- **(b) Reduce the frozen value to 8** in `abi.md` and the descriptor.
- **Recommendation: (a).** The value is inherited from the kr32 C ABI revision 1; (b) would diverge from that contract to save a few bytes of frame.
- **Your answer:** —

### A12 — KR32 FP return

- **(a) Implement F0/F1** for a split representation.
- **(b) Correct the wording to F0**, which is where every result, including `double`, already lands.
- **Recommendation: (b).** The F registers are 64-bit, so a `double` is one register; F0/F1 reads like a 32-bit-FPU plan.
- **Your answer:** —

### A13 — KR216 stack top

- **(a) Change the linker** to `0xFEF0` so `__stack_top` matches the appendix.
- **(b) Amend the appendix** to `0xF000`, the linker's value today.
- **(c) Check kr216's reset SP first**, then do (a) or (b) to match it.
- **Recommendation: (c); if the VM is not at hand, (b).** `__stack_top` is what guest programs actually observe, and today it is `0xF000`.
- **Your answer:** —

### A14 — Descriptor schema

- **(a) Revise `schema.md`** to the implemented `TargetProfile`, keeping v2 for the two ABI fields that exist on the kr232 descriptor, and correct the version line.
- **(b) Add the missing fields** (`max_alignment`, `stack_alignment`, `sret_threshold`, `varargs_slots`, `tls_model`, `eh_rtti`) and make every profile set them.
- **Recommendation: (a).** Adding fields nothing reads is dead weight; (b) can return when a consumer needs them.
- **Your answer:** —

### A15a — IR: `phi` lowering

- **(a) Amend the IR document** (with ADR 0003 revision) to say KR32 and KR16 lower `phi` on the incoming edge and IRGen is what avoids it.
- **(b) Remove the lowering** to restore the documented model.
- **Recommendation: (a).** Both backends and mem2reg depend on the lowering; (b) would undo a working design.
- **Your answer:** —

### A15b — IR: inlining and levels

- **(a) Amend the Passes and levels section** (ADR 0003 revision) to the four levels and inlining at `-O2`/`-Os`, citing ADR 0060.
- **(b) Remove `-O2`/`-Os` and the inliner** to match the frozen text.
- **Recommendation: (a).** ADR 0060 already made `-O1` the default and the levels are exercised by `krc_optimization_levels`.
- **Your answer:** —

### A15c — IR verifier rules 7, 10, 12

- **(a) Tighten rules 7 and 10 in the verifier** to require `ptr<i8>` for `vastart` and `landingpad`, and restate rule 12 to cover result types only.
- **(b) Restate all three rules** to the permissive behaviour the code has.
- **Recommendation: (a).** The two pointer checks are a few lines and match the frozen text; the lane-count rule was written for results and should say so.
- **Your answer:** —

### A16a — KRO build-ID recipe

- **(a) Document the implemented material** (section name, size, payload; symbol name, value, binding; then relocations) with an ADR revision.
- **(b) Change the writer** to the documented recipe.
- **Recommendation: (a).** Nothing validates a build ID today, so documenting the deployed bytes is the safe direction.
- **Your answer:** —

### A16b — Relocation type/width pairs

- **(a) Add the pair check** to the writer or the linker and reject a mismatch.
- **(b) Weaken the frozen sentence** to "widths 1, 2, 4, 8 are accepted".
- **Recommendation: (a).** A malformed pair today patches the wrong number of bytes silently.
- **Your answer:** —

### A16c — KRO reader validation

- **(a) Add the listed checks** (target id, ABI version, pointer width, table bounds) to the reader.
- **(b) Restate the list** to what the reader checks.
- **Recommendation: (a).** A reader that trusts a truncated or foreign object is the more expensive failure.
- **Your answer:** —

### A16d — K232 and archives in `object.md`

- **(a) Add a K232 image section** (ADR 0058) and an archive section (the `!<arch>` format `krld` accepts), and mark kind 2 unused.
- **(b) Leave them undocumented** in `object.md` and point at the ADR and `archive.cpp`.
- **Recommendation: (a).** The K232 container is shipped and `--dump-symbols` reads it; a reader needs a specification.
- **Your answer:** —

### A17a — `.kdbg_var` field order

- **(a) Correct the document** to function, name, offset, type, size, matching the writer and decoder.
- **(b) Change the encoder and decoder** to the documented order, which changes every payload.
- **Recommendation: (a).** No second implementation reads the frozen order, and the two code paths agree with each other.
- **Your answer:** —

### A17b — KDBG CRC coverage

- **(a) State the coverage** in the header table: the CRC covers the whole payload with the field zeroed.
- **(b) Leave the table's "header CRC32" wording** and treat the name as historical.
- **Recommendation: (a).** A reader following the name alone rejects valid payloads.
- **Your answer:** —

### A17c — KDBG `pointer_width`

- **(a) Set it in `attach_kdbg`** from the object's target (2 for KR16/KR216) and document the units and values.
- **(b) Document 4 as the only value** and delete the field's target meaning.
- **Recommendation: (a).** The field exists to select 64-bit records; leaving it at 4 for a 16-bit target is wrong even if no current reader notices.
- **Your answer:** —

### A18 — Plan Appendix C and D-050

- **(a) One Appendix A entry** that adds the KR232 row, moves KR32/KR216 EH/RTTI to "yes" with the ADR references, rewrites D-050's flag clause, and fixes the `docs/decisions/` link.
- **(b) Amend only the link** and leave the table and D-050 as approved.
- **Recommendation: (a).** The table is the plan's target summary and it omits a shipped target.
- **Your answer:** —

### A19 — C++ mangling

- **(a) Mark the Itanium scheme unavailable** and record the emitted `Class_method` scheme in the draft C++ ABI section.
- **(b) Implement Itanium mangling** before the C++ ABI freeze.
- **Recommendation: (a).** The current scheme is internal and no external linker consumes it; (b) is a project, not a documentation decision.
- **Your answer:** —

### B2 — Capability flags

- **(a) Correct the flags and make Sema consult them**: `has_cpp` true for KR16 (C++ works and is documented), `has_rtti` true for KR32, and a diagnostic for a capability a target lacks.
- **(b) Delete `has_cpp` and `has_rtti`** since nothing reads them.
- **Recommendation: (a).** The flags are part of the descriptor contract; (b) removes the contract instead of fixing it.
- **Your answer:** —

### B4 — `getchar`

- **(a) Wrap the call in `host_result()`** like every other wrapper, so a negative host result becomes `-1` with `errno`.
- **(b) Document `getchar` as the exception** that returns the raw host code.
- **Recommendation: (a).** It is a one-line fix that makes the layer rule true.
- **Your answer:** —

### B3, B5, C1–C3 — Errata

- **(a) Apply all of them**: the two stale comments, the three broken links, and the KR232 profile comment.
- **(b) Apply only the link fixes.**
- **Recommendation: (a).** Each is a one-line correction with no contract effect.
- **Your answer:** —

## E. What was updated in the living documents

`README.md`, `docs/README.md`, `docs/krc-user-manual.md`, `docs/kr-build-instructions.md`,
`docs/release-notes.md`, `docs/benchmarking.md`, `docs/tree.md`, `docs/toolchain-embedding.md`,
`docs/new-target-guide.md`, `docs/arm64-tester.md`, `docs/host-services.md`,
`docs/attributes.md`, `docs/ir.md`, `docs/object.md`, `docs/symbols.md`, `docs/abi.md`,
`docs/abi-kr16.md`, `docs/abi-kr216.md`, `docs/abi-kr232.md`, `docs/targets/kr16.md`,
`docs/targets/kr32.md`, `docs/decisions/README.md`, the `src/*/README.md` signposts,
`targets/README.md`, `sysroot/README.md`, `tests/fixtures/README.md`, `tests/corpus/README.md`,
`implementation.md`, `AGENTS.local.md`, and the four root handoffs, which now carry a
point-in-time banner.

Default is no build: a documentation-only change is verified by reading, per
`AGENTS.local.md` (`Build policy`, tier 1).
