# Handoff — KRCompiler

Written 2026-11-12 for continuation on another machine. Read this together with
`AGENTS.md`, `AGENTS.local.md`, `plan.md`, and `implementation.md`.
## State at handoff

- Repository: `https://github.com/kevinwross/KRCompiler.git`, branch `master`.
- `master` equals `origin/master`; the tip of the latest gate is named in
  `implementation.md` with the per-configuration counts.
- The gate timestamps and the per-configuration counts live in `implementation.md`
  under `## Gate verification`. That section owns them; restating them here is how
  a handoff starts disagreeing with the tracker.
- This is a Windows/MSVC checkout. `build/` is untracked.
- The corpus is measured, not assumed: 14 of 15 examples build and 11 run
  with the status their `main` returns. One wrong-code defect remains,
  `binarytree.c` 115 and `balanced-tree.cpp` 108; `matrix.cpp` needs the
  matrix intrinsics; `FileOpen.cpp` builds and links but blocks if run.
- The Linux presets need Ninja in WSL; see `AGENTS.local.md`, Environment assumptions.
## Objective and policy

Objective: **“Proceed with implementation of M0 thru M10.”**
The previous session used a goal cap of 3000 rounds. The goal was marked
`blocked` only because the user asked to pause at the completed gate; that is a
pause, not a technical blocker. Resume implementation rather than treating it as
finished.

Authoritative process is in `AGENTS.local.md`:

1. Inner loop: `windows-msvc-debug` on Windows.
2. Boundary change (anything under `include/krc/`, `src/ir/`, target descriptor,
   object/symbol format, ABI, `CMakeLists.txt`, `CMakePresets.json`): run the
   host matrix before committing.
3. Milestone gate: delete the four build directories, then build and test
   `windows-msvc-debug`, `windows-msvc-release`, `linux-gcc-debug`, and
   `linux-gcc-release` concurrently. Push only after a clean gate.

The gate timestamp is the **start** of the first build job, not the time the
record is written. Record one line per configuration and an explicit
`not verified:` line. A record saying “all configurations” is invalid.

Every merged change updates `implementation.md`. Documentation-only changes
need no build. No external runtime dependency; doctest is the only vendored test
dependency. ABI/object/symbol/target/IR changes need an ADR under
`docs/decisions/` and, when a `D-xxx` decision changes, an Appendix A entry.
## First steps on the new machine

```pwsh
# From the repository root
pwsh -File .agents/doctor.msvc.ps1
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Then read:

- `plan.md` — design contract, milestone gates, decision log.
- `implementation.md` — live tracker; gate records near line 454, running log at
  the end.
- `AGENTS.local.md` — build tiers, gate format, commit/push rules, known traps.

Sibling repositories `..\kr32` and `..\kr16` are **read-only inputs**. Clone
them next to `KRCompiler` if corpus work is needed.
## What has been completed

M0 through the M7/M8/M9 surfaces are substantially implemented. The current
tree supports:

- C11 and a large C++17 subset through typed SSA IR.
- KR32 code generation, `.kro` objects, `krld`, KDBG symbols/debug data.
- KR16 C path with `.kex`/KRO output and runtime; KR64 remains a placeholder.
- Deterministic double-build checks for KR32 objects, KR16 `.kex`, KR16 KRO,
  and linked KR16 images.
- Runtime/libc: allocator, stdio, string/memory, conversions, `qsort`/`bsearch`,
  file I/O wrappers, `printf` integer/float formatting, `%e`/`%g`, environment
  table (`getenv`/`setenv`/`unsetenv`), `atexit`, `_Exit`, `quick_exit`.
- C++ support exercised by linked fixtures: classes, constructors/destructors,
  inheritance, virtual dispatch, pure virtuals, out-of-line methods/operators,
  static members, nested types, namespaces and aliases, inline namespaces,
  `using namespace`, `extern "C"`, `decltype`, `alignof`, `sizeof`, lambdas,
  range-for, move/copy semantics, `std::move`/`forward`/`swap`/`min`/`max`,
  `thread_local` as static storage, braced and placement `new`, and returned
  temporary materialization across many expression contexts.
- 64-bit integers on KR32 are a low/high word pair with inline arithmetic and
  division through runtime helpers ([ADR 0023](docs/decisions/0023-kr32-64-bit-integers.md));
  the fixture links, its runtime exit code is unobserved.
- A call to a deleted member function is rejected, and access to a non-public
  member from a free function is rejected unless it is a friend.
- The const and inheritance markers are enforced rather than parsed:
  assigning a member of a const object is refused unless it is `mutable`,
  calling a non-const member through a const object, pointer or reference is
  refused while a const-qualified member stays callable, `override` naming
  no base member is refused, `final` is enforced on classes and members,
  and an object of an abstract class cannot be created.

Recent commits worth knowing:

- `6db6d1a` — call initializers are evaluated once; this fixed doubled `printf`
  output found by running the `../kr32` corpus.
- `4044d9a` — namespace-qualified type names in declarations and `new`.
- `f24f1be`, `de2c793` — `thread_local` at namespace and block scope.
- `902968b` — 64-bit integer lowering (ADR 0023); `480b4ed` fixed a `BitCast`
  that truncated it. Both verified by execution under `../kr32`'s VM, where
  `cxx_tls.cpp`'s `long long` division now returns 42.
- `d71bee9`, `41b58a3`, `7eb8ec4`, `91df34a` — deleted functions, access
  control, out-of-line class scope, and friend classes.
- `f249956` — Sema analyses in-class member bodies.
- `23d1cf1`, `86594bc` — KR16 profile rejects `long long` and floating point.
- `6ab2671`, `cf10b13` — executed tests run images under the VM and check exit
  status and output, which is how the `BitCast` truncation was found.
- `b1ec5c2`, `a744e14`, `4826601` — const-object member assignment, its
  `mutable` exemption, and a guard that reads still compile.
- `1001430` — `-S -o FILE` writes the assembly instead of ignoring the path.
## Important test caveat

Most CTest entries named `krc_link_*` only **link** a fixture; they do not assert
the program's runtime result. Behaviour was verified manually by running the
linked image:

```pwsh
..\kr32\build\windows-msvc\Release\kr32.exe vm `
  build\windows-msvc-debug\stage\tests\<name>_linked.k32 --max-steps 1000000
```

When adding a fixture, add the `add_test` entry with
`cmake/LinkRuntime.cmake`, run it, then run the VM manually and record the exit
value in `implementation.md`. Do not claim a runtime result from CTest alone.
## Known open work

### M8

- Full Tier 0–2 corpus green on KR32 and Tier 0–1 on KR16 is not established.
- Debugger parity for C and C++ is not fully demonstrated.
- Release notes and tag `v1.0` are not done.

### M9

- Broader file APIs and error mapping remain.
- KR16 file I/O and broader KR16 libc integration remain.
- Common host-service ABI and kernel profile remain.

### M10 (the largest gaps)

- Table-driven exceptions and unwinding.
- RTTI, `dynamic_cast`, `typeid`.
- Variadic templates and fold expressions.
- Per-thread dynamic TLS. `thread_local` currently aliases static storage.
- `memory`, `tuple`, `functional`, `array`.
- Full KR32 libc/kr32os and KR16 runtime integration.

### Known correctness limits

Compiler-side limits are listed in `implementation.md` under the M8–M10 evidence
and the running log, next to the change that introduced each one. That list is
the one to trust: a copy here has gone stale after every recent change.

The sibling corpus carries two blocks of its own:
## Corpus notes

Measured by compiling with `krc -c`, linking with `libkrcrt.kro`, `fileio.s`
and `crt0.s`, and running each image under `..\kr32`'s VM: **14 of 15 examples
build, and 11 of those run with the status their `main` returns.** Those eleven
are `hello.c`, `strings.c`, `types.c`, `varargs.c`, `libc-test.c`, `floats.c`,
`fileio.c`, `mainentry.c` (all 0), `hello.cpp` (42), `cxx_libc.cpp` (0) and
`cxx_tls.cpp` (42, the value its `long long` division is meant to produce).

Not green, and why:

- `binarytree.c` and `balanced-tree.cpp` run but halt with statuses 115 and
  108 (the second varies between builds) and print no tree. This is a krc
  code-generation defect, not a VM or program defect: the same source built
  with `kr32.exe build` runs correctly, which makes `../kr32` a reference
  oracle. `implementation.md` carries the narrowed evidence, the reduced
  reproducers, and the hypotheses already eliminated.
- `matrix.cpp` builds but does not link: `transform` is a compiler intrinsic
  in the sibling front end, lowering to MLOAD, MMUL, MVEC, MTRANS and MSTORE
  tile instructions. Implementing them is a capability, not a fix.
- `FileOpen.cpp` builds and links since the `sys_open_dialog` host service was
  added to `targets/kr32/runtime/fileio.s` and declared in `sysroot/include/stdio.h`.

Compile, link and run pattern used:

```pwsh
krc.exe -c ..\kr32\examples\<file> -o build\corpus\<file>.kro `
  -I sysroot\include -I ..\kr32\include --sysroot build\windows-msvc-debug\stage\sysroot\kr32
krld.exe build\corpus\<file>.kro `
  build\windows-msvc-debug\stage\sysroot\kr32\lib\libkrcrt.kro targets\kr32\runtime\fileio.s `
  build\windows-msvc-debug\stage\sysroot\kr32\lib\crt0.s --source-root=. -o build\corpus\<file>.k32
..\kr32\build\windows-msvc\Debug\kr32.exe vm build\corpus\<file>.k32 --max-steps 20000000
```

A run leaves `host-output.txt` in the project root; delete it before committing.

Skip `FileOpen.cpp` when running the corpus in a loop: it now links, and
its run waits for the host file dialog instead of returning.
## Traps that have caused real failures

- **Test-name and fixture collisions.** Before adding a fixture or `add_test`,
  run `ctest --preset windows-msvc-debug -N | Select-String <name>` and
  `git ls-files tests/fixtures/c/<name>`. Earlier work accidentally overwrote
  tracked fixtures with same-named new ones; restore with
  `git checkout -- <fixture>` and pick a unique name.
- **Duplicated initializer side effects.** Scalar call initializers were once
  emitted twice; check `--dump-ir` for a single call per initializer.
- **KR16 frame slots.** Locals/records must get distinct frame storage; shared
  2-byte slots previously corrupted arrays and structs.
- **KR16 branch range.** Use `JMP` rather than unconditional `BRA` for long
  bodies; invert short conditional branches.
- **Float call arguments.** KR32 call emission must load all floating arguments
  before integer arguments; otherwise a float load clobbers `R0` (`this`).
- **`implementation.md` literal `\n`.** Use real CRLF line endings; check with
  `git diff --check`.
- **Gate/build contention.** Do not run a manual MSVC build while a gate is
  building the same preset; MSVC PDB contention produces `C1041`. Delete the
  build directories and rerun cleanly.
- **Final gate record.** After the gate, `git diff --check`, commit the record,
  confirm `master` equals `origin/master`, then push.
## Suggested next actions

1. Give the corpus tree defect one static attempt, but not by diffing assembly:
   `kr32.exe build` accepts a C file and emits only an image — it has no assembly
   output, so the reference cannot be diffed textually. Read the reference
   compiler's frame conventions in `../kr32/src/cc` instead, or bisect
   behaviourally on the reference build. Seven rounds of probing krc alone
   produced no fix; something has to constrain the search. If it does not
   converge quickly, escalate it as a known wrong-code defect and move on.
   Already ruled out: the program compiles cleanly with the current
   compiler, and fixing `!` on a pointer — which was a real IRGen defect
   with a six-line reproducer — did not change its behaviour.
2. Then RTTI: `typeid` and `dynamic_cast` over the vtable/typeinfo layout in
   plan §10.4. The executed-test harness can verify it end to end.
3. Remaining backlog: the corpus tree wrong-code defect recorded in
   `implementation.md`, the rest of deleted constructors — a deleted default
   constructor is refused already, while `S(int) = delete` still permits
   `S object;` and `S x(5);` goes unchecked — RTTI, exceptions, variadic
   templates, dynamic TLS, the `std` containers, KR16 libc/file I/O, and
   matrix/host intrinsics.
## Goal recommendation

Keep the objective **“Proceed with implementation of M0 thru M10.”** If the new
session supports persisted goals, create that goal with a 3000-round cap. Do not
mark it complete while M8/M9/M10 items above are open, and do not treat the
previous `blocked` state as a technical blocker — it meant “paused by user.”
