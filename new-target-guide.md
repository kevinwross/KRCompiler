# Building a new target backend

**Status:** guide, 2026-11-21, at revision `73611af`. Written for a session that has read
`AGENTS.md` and `AGENTS.local.md` and nothing else.
**Reads with:** [ADR 0002](decisions/0002-target-descriptor-schema-freeze.md) (descriptor schema
freeze), [ADR 0046](decisions/0046-object-layout-and-aggregate-rules.md) (object layout and
aggregate rules), `docs/abi.md` and `docs/abi-kr16.md` (the two existing ABIs), `docs/symbols.md`
(KDBG), `docs/object.md` (KRO), `plan.md` §4 (data models) and §16 (milestones).

---

## 0. What "a target" is here, and the rule that governs the work

A target is **four things**, and only one of them is a backend:

| layer | what it decides | where it lives today |
|---|---|---|
| **profile** | widths, data model, capabilities | `src/target/target.cpp` (`TargetProfile` table) |
| **descriptor** | calling convention: registers, roles, alignment | `src/backend/kr{32,16}_descriptor.{h,cpp}` |
| **assembler + object writer** | how bytes are emitted and encoded | `src/asm/kr{32,16}_assembler.cpp`, `kr16_*_encoder.cpp`, `kro_writer.cpp` / `kr16_object.cpp` |
| **backend** | selection, frames, asm text | `src/backend/kr{32,16}_backend.{h,cpp}` |
| **runtime + sysroot** | libc, `crt0`, `bits/*.h` | `targets/<name>/runtime/`, `sysroot/<name>/include/` |
| **a VM** | executing the tests | `vm/krc_kr<name>[.exe]`, `cmake/RunKr<Name>Image.cmake` |

**The governing rule:** the profile, the descriptor, the ABI, the object encoding and the KDBG
target id are **frozen contracts**. Adding a target changes them, so it needs, *before any code*:

1. an **ADR** under `docs/decisions/` (ADR 0002 froze the descriptor schema; a new target either
   fits it or changes it, and either way the ADR says which), and
2. an **Appendix A entry** in `plan.md` if a `D-xxx` decision is affected, and
3. a **`docs/abi-<name>.md`** of its own — the two existing targets each have one, and the guide in
   `docs/abi.md` assumes a reader comparing them.

Practical consequence: **the first commit for a new target is documentation and a failing test**,
not a backend. Section 9 sequences this.

---

## 1. Decide the profile first, because everything else is derived

`TargetProfile` (`include/krc/target/target.h`) is a flat struct of widths and capability flags.
Its fields, in order — read them, do not guess:

```
id, name, macro, family,
pointer_bits, endianness, data_model,
char_bits, char_unsigned, short_bits, int_bits, long_bits, long_long_bits,
float_bits, double_bits, long_double_bits,
size_t_bits, ptrdiff_bits, intmax_bits, wchar_bits,
has_fpu, has_matrix, has_atomics, has_threads, has_eh, has_rtti, has_tls, has_cpp,
backend_available, aggregate_rules
```

Three of those decide most of the downstream work:

- **`aggregate_rules`** — `MemoryOnly` or `RegistersOrMemory`. `MemoryOnly` (KR16) means every
  aggregate travels through memory and the backend writes copy loops; `RegistersOrMemory` (KR32)
  means small records travel in registers. `docs/abi.md` and ADR 0046 give the rules.
- **`data_model`** — `Ilp32`/`Ip16`/`Lp64`. A `0` width means **the type does not exist** on that
  target (`long` and `long long` are `0` on KR16), and the front end must refuse it rather than
  emit something narrower.
- **`pointer_bits`** — is what the assembler's relocation width, the object's address fields and
  the debug frame layout all mean by "a word".

**Narrow-target trap, live today:** integer literal typing is width-sensitive and currently
defective for `int` narrower than 32. On KR16, `long x = 32768;` is rejected with *"integer
constant is too large for its type"* even though `long` is 32 bits there, which contradicts
C11 6.4.4.1 and ADR 0034's own wording (`src/frontend/sema.cpp:3602/3615`). **A new narrow target
will inherit that defect**, and its fixtures will fail until it is fixed. Fix it before trusting a
`char_bits == 8, int_bits == 16` target's arithmetic tests.

## 2. Write the descriptor, which is the ABI in machine-readable form

A descriptor is the profile's calling convention as data: `pointer_bits`, `stack_alignment`,
`public_call_alignment`, `return_register`, `frame_pointer`, `thread_pointer`,
`argument_registers`, and a fixed array of `RegisterInfo{name, role, callee_saved,
argument_index}` for integer and floating registers. `Kr32Descriptor` in
`src/backend/kr32_descriptor.h` is the model to copy; KR16's is the same shape.

The register **roles** are load-bearing beyond the backend: they tell the prologue which registers
to spill, and they decide what a call may destroy. If the target has no FPU, `floating_registers`
is empty and `has_fpu = false`; the front end then refuses floating point rather than the backend
silently mishandling it.

## 3. The backend contract

There is **no abstract backend interface** — the toolchain names the concrete class. A backend
must therefore provide exactly this shape, and be added to the dispatch:

```cpp
class Kr<Name>Backend {
  public:
    Kr<Name>Backend(const ir::Module& module, const target::TargetProfile& profile,
                    OptimizationLevel level = OptimizationLevel::O0);
    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;   // assembly text
    [[nodiscard]] const std::string& debug_info() const;                      // .debug_info payload
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function&) const;       // --dump-stats, S4
};
```

The two dispatch sites, both in `src/toolchain/toolchain.cpp`, are `frame_bytes` (~line 126) and
`emit` (~line 503). `src/tools/krc/main.cpp` has six more target branches, and one of them is worth
knowing before it surprises you: KR16's assembly is *suppressed* only when `-S` is combined with
`-c` (`suppress_assembly = target_id == Kr16 && emit_object`), because the object path assembles the
text itself. `-S` alone writes the file for both targets. `--list-targets` needs no change — it
iterates `target_profiles()` and prints `backend=` from `backend_available`.

**Do not write a second selection pass.** KR32's backend is being converted in place to build
`machine::Instruction` records and print them (`src/backend/machine_ir.{h,cpp}`,
`machine_liveness.{h,cpp}`); a new backend should adopt that from the start, because the recording
is what makes a slot or a register assignment checkable. A byte-for-byte diff of `-S` output
before and after a change is the only honest guard during conversion — see
`implementation.md`, `2026-11-19T05:40:23`, where it caught a miscompile no test noticed.

## 4. The assembler and the object writer

`src/asm/kr32_assembler.h` and `kr16_assembler.h` show the two variants. The contract is small:

- `assemble(source, AssemblyResult&, error)` — text and data as separate byte vectors, symbols,
  relocations, and a per-statement **listing** used for debug line mapping.
- Section handling: KR32 emits `.text`, `.data`, `.cpp_except`; KR16 additionally emits `.bss` and
  named `.text.<name>` sections. If the new target needs sections the KRO writer does not know,
  that is an **object-format change**: ADR + `docs/object.md` + `kro_object.cpp`'s
  `kROTargetId` switch (`1` KR32, `2` KR16, `3` KR64 — a new target takes the next id, and the
  linker and `krc --dump-symbols` both read it).
- `make_object(assembled, profile, object, error)` writes the object. `kro_writer.cpp` and
  `kr16_object.cpp` each assert the profile they were handed (`profile.id != TargetId::Kr…`), so
  a copy must change that guard, not inherit it.
- **Two distinct "not there yet" errors, and they mean different things.** `main.cpp` reports
  *"target '<name>' has no code generator yet"* when `backend_available` is false (the KR64 path
  today), while `toolchain.cpp:~198` reports *"target '<name>' has no assembler"* when a backend
  exists but the assembler dispatch has no branch for it. A new target passes through the second
  while its assembler is being written, and the branch in `toolchain.cpp` is one of the two
  `if/else if` chains (`emit`, and the earlier `frame_bytes`) that must gain a case.

## 5. Runtime, `crt0` and sysroot — the part that is easy to under-budget

Under `targets/<name>/runtime/` the existing targets ship:

| file | why |
|---|---|
| `crt0.s` | the entry point: sets up the frame, calls `main`, passes the exit status to the VM |
| `libc.c` | the C runtime; the compiler compiles it with itself, so it is also a test of the backend |
| `fileio.s` / `fileio.c` | the file service the VM exposes (KR16 keeps it heap-free) |
| `eh.s` | the exception runtime (KR32 only today) |
| `host.s` | the host-call stub (KR16) |

Then `sysroot/<name>/include/bits/` holds the type headers, which are **generated**, not written:
`krc_gen_sysroot_bits` runs `krc::target::generate_bits_types` / `generate_bits_stdint` from the
profile (`include/krc/target/sysroot_bits.h`). So `bits/types.h` and `bits/stdint.h` for a new
target come free once the profile is right — and if they do not, the profile is wrong.

CMake wiring, all in the top-level `CMakeLists.txt`:

- `KRC_STAGE_RUNTIME` / `KRC_STAGE_RUNTIME_KR16` (lines ~20) — one per target, staged under
  `${KRC_BUILD_DIR}/stage/sysroot/<name>/lib`.
- `krc_stage_sysroot` (~188) — copies every target's `include/` and runs the bits generator. A new
  target adds one `copy_directory`.
- `krc_target_runtime_<name>` (~231 and ~246) — compiles the runtime **with `krc` itself**, writes
  `.new` then renames, and copies `crt0.s`. Copy the KR32 rule for a register target and the KR16
  rule if the runtime is split into several objects.

## 6. The VM is a hard dependency for the executed tests

`cmake/RunKr32Image.cmake` and `RunKr16Image.cmake` run a linked image under
`KRC_KR32_VM` / `KRC_KR16_VM` and check the exit status (and output, where meaningful). Without a
VM the run tests are **not registered**, and the suite silently shrinks — which is exactly what the
`AGENTS.local.md` trap about `vm/` and gates exists to prevent. A new target needs its own
`cmake/RunKr<Name>Image.cmake` plus a cache variable and a fallback search, or its executed rows do
not exist.

The linker is target-neutral apart from the target id it stamps; a new target's `krld` support is
only the id and the object sections.

## 7. Debug information, which a target inherits but must not assume

`.kdbg` records carry a **target id** (`1` KR32, `2` KR16, `3` KR64 — `docs/symbols.md:128`), and
`Variable` records carry frame offsets and sizes in the target's words. Consequences for a new
target: `kROTargetId` gains a case, `docs/symbols.md` gains a row, and the frame layout the backend
computes must match what the `Function` record claims — there is a test for that on the existing
targets (`krc_debug_invariants_o1`), and it should be cloned.

KDBG v1.1 is additive: a `Location` record (tag 9 in `.kdbg_loc`) carries a live range, and a v1
reader skips it. A new target must emit the same v1 records KR32 does; it need not emit `Location`
records until the debugger work needs them.

## 8. Tests: what to clone, not what to write

`tests/CMakeLists.txt` is 7,000+ lines and carries the suite per target. For a new target the
cheapest honest path is to add it to the existing **loops** rather than to write new tests:

- The per-target link/run loops (`foreach(tier…)` and the `_o1` twins) take the target as a
  variable; adding a target there multiplies coverage rather than duplicating it.
- The unit tests that name a target (`test_target.cpp`, `test_irgen.cpp`, `test_machine_ir.cpp`,
  `test_machine_liveness.cpp`, `test_mem2reg.cpp`) each have a "both targets" case; each needs a
  third.
- Determinism: every target has a double-build byte-comparison test. A new target without one is
  outside §15.6.

Then a **clean gate** on the host's row, recorded in `implementation.md` under
`## Gate verification` with the timestamp of the first build (not of writing the record).

## 9. The order to do it in, with the evidence each step produces

| step | artefact | passes when |
|---|---|---|
| 1 | ADR + `plan.md` entry + `docs/abi-<name>.md` | reviewed; says which frozen contract changed |
| 2 | profile row in `src/target/target.cpp`, `TargetId` entry, `to_string`, `parse_target`, `--list-targets` | `test_target` covers the new row; `krc --list-targets` shows it |
| 3 | sysroot `include/bits` generated from the profile | `bits/stdint.h` round-trips the widths in a unit test |
| 4 | assembler + encoder + object writer, with a **round-trip unit test** (assemble → bytes → disassemble) | bytes match, listing addresses are right |
| 5 | backend: prologue/epilogue, then load/store, then ALU, then calls — adopting the machine-IR recording | `-S` output diffs to zero against each step's predecessor; a frame-size test holds |
| 6 | `crt0.s` + a runtime that returns a status | one image links and exits 0 under the VM |
| 7 | per-target link/run loops in `tests/CMakeLists.txt` + `RunKr<Name>Image.cmake` | executed rows exist and pass |
| 8 | determinism + KDBG invariants + `_o1` twins | the suite count grows on every row |
| 9 | clean gate, recorded | six rows on this host, or the four Linux rows on a Linux host |

**Realistic estimate**, from this project's own history: steps 4–5 are where the time goes. For
scale, `kr32_backend.cpp` is **2,555 lines** and `kr16_backend.cpp` **1,306** — and the gap is not
incidental: KR32 owns inline 64-bit arithmetic, floating point, matrix intrinsics and the register
allocator path, while KR16 delegates 64-bit to helper calls, has no FPU and allocates nothing (ADR
0049). A new target sits somewhere on that spectrum, and where it sits decides the size. The two
failed optimisation attempts recorded in `implementation.md` (`2026-11-19T01:30:20`, `01:43:57`)
show what happens when a backend is modified without a byte-identical guard. Budget rounds, not
hours, and expect the first suite-green target to be `-O0`-only: a new target should *measure*
whether its ISA can benefit from `-O1` rather than assume it, which is what KR16's row in ADR 0049
records.

## 10. What this guide does not decide

- **Which target to add.** The profile table already carries **KR64** (`Lp64`, 64-bit pointers,
  `backend_available = false`) with `sysroot/kr64/include/` present — the cheapest real new target
  is finishing KR64 rather than inventing a fourth. The gap is visible in two commands:
  `krc --list-targets` prints `backend=no` for it, and `krc --target=kr64 -c file.c` stops with
  **"target 'kr64' has no code generator yet"**. But `krc --target=kr64 --dump-ir file.c` already
  works and prints `; target: kr64`, which is the useful fact: for KR64 the **profile, front end and
  IR are done**, and what is missing is steps 4–6 of §9 (assembler, object writer, backend,
  runtime) rather than the whole guide.
- **Endianness.** Both existing targets are little-endian, and the object's relocation and debug
  encodings are written for that. A big-endian target is an object-format and KDBG question, not a
  backend question.
- **A second `-O1` strategy.** `D-049` makes selection quality lowering; a target whose ISA cannot
  benefit (like KR16's two-operand ALU) must be *measured* and reported, not given a parallel
  optimiser.
