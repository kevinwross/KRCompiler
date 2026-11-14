# Handoff — KRCompiler
previous `blocked` state as a technical blocker — it meant “paused by user.”
## Sixteen-bit profiles are now honest about their widths

KR16 declares `short`, `int`, and `long` as sixteen bits and `long_long_bits`
as zero, so it has no 64-bit type at all: `long long` is rejected where it is
written. Two defects made that width lie — a literal wider than its type was
silently narrowed, and `MOV` accepted an immediate wider than a word — and both
are fixed ([ADR 0034](docs/decisions/0034-integer-literals-on-narrow-targets.md)):
the front end types a literal per C11 against the profile and rejects one that
fits no type, and the assembler rejects an immediate outside `-32768..65535`.
No ABI change is needed, and the earlier note here that KR16 needed word-based
argument assignment was wrong.
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
- The corpus is measured, not assumed: 15 of 15 examples build and link, and the
  14 that do not wait on an interactive host dialog run with the status their
  `main` returns. `FileOpen.cpp` links but blocks on the host file dialog if
  run, so exclude it from a loop.
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
  `krc_run_longlong_arith` and `krc_run_printf_long_long` execute it.
- The `kr32::matrix` surface lowers to tile instructions and a by-value
  aggregate above 8 bytes returns through the destination pointer in `R4`
  ([ADR 0024](docs/decisions/0024-kr32-tile-intrinsics.md)); the transform stack
  is set up by `crt0.s`. `krc_run_matrix_intrinsics` requires exit 42.
- RTTI works over weak `_ZTV`/`_ZTI`/`_ZTS` globals
  ([ADR 0025](docs/decisions/0025-rtti-vtable-typeinfo.md)): `typeid` names the
  dynamic type through a polymorphic glvalue, `typeid == typeid` compares
  typeinfo addresses, and `dynamic_cast<T*>` is a bitcast for an upcast or a
  `__krc_dynamic_cast` call for anything else. A reference cast lowers to the
  same call and throws `std::bad_cast` when it fails, which needs the exception
  runtime; `krc_run_dynamic_cast_reference` requires exit 42 and catches the
  failure both as `std::bad_cast&` and as `std::exception&`. Polymorphic
  construction allocates no table; objects of one class share one.
  `krc_run_rtti` requires exit 42.
- Table-driven exceptions work across frames
  ([ADR 0026](docs/decisions/0026-exception-descriptors.md)): `throw`,
  `try`/`catch`, catch-all, rethrow, and handlers that catch a base of the
  thrown class. Every function contributes a `.cpp_except` descriptor, the
  runtime `eh.s` walks the saved `R7` chain using each frame's recorded size,
  and an unmatched exception aborts with exit 134. `krc_run_exceptions`
  requires exit 42.
- Destructor cleanup on propagation works through cleanup landing pads
  ([ADR 0027](docs/decisions/0027-exception-cleanup.md)): every scope with a
  destructible object gets a pad that destroys it in reverse order and chains
  into the enclosing pad, so a `try` body's objects are destroyed before its
  handler runs. `krc_run_exception_cleanup` requires exit 42 and fails on a
  wrong destruction order.
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

- The KR32 corpus is green: 15 of 15 examples build and link, and the 14
  non-interactive ones run with the status their `main` returns. Tier 0–1 on
  KR16 is not established.
- Debugger parity for C and C++ is not fully demonstrated.
- Release notes and tag `v1.0` are not done.

### M9

- Broader file APIs and error mapping remain.
- KR16 file I/O and broader KR16 libc integration remain.
- Common host-service ABI and kernel profile remain.

### M10 (the largest gaps)

- Exception cleanup: destructor cleanup on propagation, a personality routine,
  and reference `dynamic_cast` (needs `std::bad_cast`).
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
and `crt0.s`, and running each image under `..\kr32`'s VM: **15 of 15 examples
build and link, and the 14 that do not wait on a host dialog run with the status
their `main` returns.** Those fourteen are `hello.c`, `strings.c`, `types.c`,
`varargs.c`, `libc-test.c`, `floats.c`, `fileio.c`, `mainentry.c` (all 0),
`hello.cpp` and `matrix.cpp` (42 each), `cxx_libc.cpp` (0), `cxx_tls.cpp` (42,
the value its `long long` division is meant to produce), and `binarytree.c` and
`balanced-tree.cpp` (0 each, since the heap-base fix).

Two earlier blockers have closed, and the reasons matter if they reappear:

- `binarytree.c` and `balanced-tree.cpp` halted with wrong statuses until the
  heap base was fixed: `malloc` started at `0x00020000`, inside `.text` for a
  large program, so allocations overwrote the code that was about to run.
  `HEAP_BEGIN` is now `0x00100000`, and `krc_run_heap_above_code` regresses it
  by allocating a large block and then calling a function defined after it.
- `matrix.cpp` failed at link on the tile intrinsics, and once they existed, on
  the 64-byte `mat4` return. Both are implemented
  ([ADR 0024](docs/decisions/0024-kr32-tile-intrinsics.md)):
  the `kr32::matrix` calls lower to `MLOAD`/`MMUL`/`MMAC`/`MTRANS`/`MVEC`/`VDOT`
  and the transform stack, and a by-value aggregate above 8 bytes returns
  through the destination pointer in `R4`. `krc_run_matrix_intrinsics` requires
  exit 42.

One corpus example is interactive by design: `FileOpen.cpp` builds and links
since the `sys_open_dialog` host service was added to
`targets/kr32/runtime/fileio.s` and declared in `sysroot/include/stdio.h`, but
its run waits for the host file dialog instead of returning.

Compile, link and run pattern used:

```pwsh
krc.exe -c ..\kr32\examples\<file> -o build\corpus\<file>.kro `
  -I sysroot\include -I ..\kr32\include --sysroot build\windows-msvc-debug\stage\sysroot\kr32
krld.exe build\corpus\<file>.kro `
  build\windows-msvc-debug\stage\sysroot\kr32\lib\libkrcrt.kro `
  targets\kr32\runtime\fileio.s targets\kr32\runtime\eh.s `
  build\windows-msvc-debug\stage\sysroot\kr32\lib\crt0.s --source-root=. -o build\corpus\<file>.k32
..\kr32\build\windows-msvc\Debug\kr32.exe vm build\corpus\<file>.k32 --max-steps 20000000
```

`eh.s` is the exception runtime and belongs in every KR32 image; the CTest link
scripts append it themselves.

A run leaves `host-output.txt` in the project root; delete it before committing.

Skip `FileOpen.cpp` when running the corpus in a loop: it now links, and
its run waits for the host file dialog instead of returning.
## Traps that have caused real failures

- **Never rewrite a file with a PowerShell line array.** `New-Object
  System.Collections.Generic.List[string]` plus a write loop has damaged a
  source file or document four times: a mistyped type name left the list null
  and wrote a 2-byte `handoff.md`, empty lines accumulated at EOF, and lines
  were dropped silently. Use the `edit` tool for text, and when a whole-file
  rewrite is genuinely needed, print the resulting line count in the same
  command and compare it against the original.
- **Check `git status` before trusting a bulk edit.** The 2-byte handoff was
  caught only because `git status` ran in the same command and showed the file
  modified; `git checkout -- <path>` restored it and the edit was redone.
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

1. Cross-casts between sibling classes and multiple inheritance in
   `dynamic_cast`, plus `type_info::before()` and `hash_code`.
2. Then variadic templates and fold expressions, per-thread dynamic TLS, the
   `std` containers (`memory`, `tuple`, `functional`, `array`), KR16
   libc/file I/O, and the broader host-service ABI.

Matrix intrinsics are done: `matrix.cpp` links and runs to 42, so the corpus has
no unimplemented capability left. `FileOpen.cpp` stays interactive by design.

## Known gaps in exception handling

- There is no personality routine: the landing pad matches catch clauses itself,
  and the descriptor hands the runtime only the pad address.
- A destructor that throws is not terminated as the standard requires; it
  unwinds to the enclosing pad.
- A handler that ends in a throw skips its `__cxa_end_catch`, so the exception
  object leaks and the current-exception link is not restored.
- `throw` accepts a class object only; scalars and pointers are rejected.
- `throw` is a statement, not an expression; `noexcept` and exception
  specifications are not supported.
- Exception state is a single global; the VM is single-threaded for the
  supported subset.

## Class templates

`template <typename T> class Box` instantiates per type: `Box<int>` names the
record `Box__int` with its own layout, constructors, destructor, and members,
and `Box<double>` alongside it is a different record
([ADR 0032](docs/decisions/0032-class-template-instantiation.md)). A member may
take its own instance by reference, and `Pair<int, double>` and
`Pair<double, int>` are different types. Not implemented: a member defined
outside the class body, a non-type parameter, a default argument, partial
specialisation, and a member template.

A scope may hold several destructible objects: each unwinding point protects
the objects built before it, a return, break, or continue destroys the scopes it
leaves without consuming them, and the pad chain is disjoint so nothing is
destroyed twice ([ADR 0027](docs/decisions/0027-exception-cleanup.md)).
## std::array

`std::array<T, N>` lives in `sysroot/include/array` and is built on the class
template and value-parameter forms: the storage is a plain array member, so its
layout is `T[N]`. `operator[]`, `data`, `size`, `empty`, and `fill` work.
`<stdexcept>` provides `logic_error`, `runtime_error`, `out_of_range`,
`invalid_argument`, `length_error`, and `overflow_error`.

A member function returning `T&` is dereferenced when read as a value, so
`front()`, `back()`, and `at()` work in expressions; assigning *through* such a
call (`values.front() = 11`) still writes a temporary and is not fixed. Two
other gaps remain: the injected class name inside a template class body is not a
type, and a range-for over a class is rejected. Each has a reproduction in
`implementation.md`.
## Non-type template parameters

`template <typename T, int N>` works for class templates: the parameter is
substituted by its number in the instance, so `Buffer<int, 4>` is the record
`Buffer__int__4` with a `[4 x i32]` member
([ADR 0033](docs/decisions/0033-non-type-template-parameters.md)). Only an
integer literal is accepted as a value argument; a value parameter on a function
template is not implemented.

`&&` and `||` short-circuit: the right operand is lowered inside the block
its own branch guards, and the deciding value crosses the branch in an allocation
slot because neither backend lowers `phi` ([ADR 0027](docs/decisions/0027-exception-cleanup.md)
uses the same rule for cleanup); `docs/ir.md` states the constraint.
## Function templates

`template <typename T>` deduces and instantiates: an all-int call still resolves
to the definition the parser substituted, and any other deduced type clones the
body into `name__tag` (`add__double`) ([ADR 0031](docs/decisions/0031-function-template-instantiation.md)).
Deduction is one level (`T`, `T*`, `T&`, `const T&`, array decay for pointers)
and every parameter must be deduced unless an explicit argument list binds it:`add<double>(1, 2)` and `convert<int>(2.75)` instantiate the named type and
deduce the rest. Class templates, variadic templates, fold expressions, and
partial specialization are not implemented.
## Thread storage

C++ `thread_local` and C11 `_Thread_local` share static storage on the
single-threaded VM. A block-scope thread-local object takes the local-static
path: a guarded initializer that runs on first use, once, and a destructor
registered to run at exit. IRGen's block-scope path accepts `static`,
`thread_local`, and `_Thread_local`; it tested only for `static` before, so a
thread-local object was emitted as an automatic and re-initialized on every
call. `<threads.h>` still refuses to be included because guest threads do not
exist.
## Release notes

`docs/release-notes.md` states what v1.0 contains, how to build it, the compatibility
contracts, and what it deliberately does not contain. The `v1.0` tag itself is an
operator decision; the notes say so rather than implying it exists.

## Sixteen-bit profiles are honest about their widths

KR16 declares `short`, `int`, and `long` as sixteen bits and `long_long_bits` as
zero, so it has no 64-bit type at all: `long long` is rejected where it is
written. Two defects made that width lie — a literal wider than its type was
silently narrowed, and `MOV` accepted an immediate wider than a word — and both
are fixed ([ADR 0034](docs/decisions/0034-integer-literals-on-narrow-targets.md)):
the front end types a literal per C11 against the profile and rejects one that
fits no type, and the assembler rejects an immediate outside `-32768..65535`.
No ABI change is needed, and the earlier note here that KR16 needed word-based
argument assignment was wrong.

## KR16 streams use the standard names

`<stdio.h>` declares a KR16 branch: `FILE` is the sixteen-bit
`struct krc16_file`, and `fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/
`putc`/`fgets`/`fputs`/`ungetc`/`ftell`/`fseek`/`feof`/`ferror`/`clearerr`/
`remove`/`rename` come from `targets/kr16/runtime/fileio.c` over `host.s`.
`stdout` and `stderr` are accessor calls, not globals, because a data pointer
whose initialiser needs a relocation is not emitted on this profile. `SEEK_END`
is refused; see the entry below.

## A returned call runs once

`int forward(void) { return bump(); }` used to emit the call twice and return
the second result, so a forwarding wrapper ran its callee's side effects twice
and `getc` read the file's second byte. `emit_return` now returns a non-record
call result directly instead of falling through to the generic tail that emitted
the expression again. `return_call_once.c` pins it. A record-valued call with no
conversion method still falls through that path, and that corner is recorded in
`implementation.md` rather than fixed.

## The dump precedes verification

`--dump-ir` prints the module before the verifier runs, so a dump can show a
defect that `krc -c` would reject. Rule 9 of [`docs/ir.md`](docs/ir.md) covers a
`ret` matching the function's return type, and two unit tests in
`tests/unit/test_ir.cpp` pin it. Do not infer a missing verifier rule from a dump.

## Open: a KR16 seek leaves the descriptor unreadable

A seek on a KR16 file descriptor is accepted and then the following read returns
zero bytes. `krc16_sys_seek(fd, 0)` reports success (status 0, position 0) and
`krc16_sys_read(fd, buffer, 1)` then returns 0 on a five-byte file; a read before
any seek returns its byte. That is the **host service**, not this compiler: the
probe calls the wrappers directly with no stream layer, and the wrappers already
pass `fd`, the offset low and high words, and a zero whence in the service block.
The sibling VM is a read-only input, so it is reported rather than fixed; the
reproduction is `build/corpus/rawseek2.c` in a working tree of this commit.
`SEEK_END` and `fsetpos` stay refused until it works, and the size they need is
available from `SYS #6` (STAT) at offsets 4 and 6.

## Known front-end gaps

Three gaps block a complete `std::array`, each with a reproduction:

- **Assignment through a reference-returning call.**
  (The literal-to-const T& case that used to sit here is fixed: a temporary
  is materialized, so alues.fill(5) works.)
 `values.front() = 11`
  writes a temporary; `emit_lvalue` builds one for every call. The value form
  works, so reads through `front`, `back`, and `at` are fine. An attempt that
  returned the call's address from `emit_lvalue` produced *store type does not
  match its pointer* in a fixture that otherwise compiles; the failing store was
  not identified, so the change was reverted.
- ~~The injected class name inside a template class body~~ — fixed: the
  class-template branch of type resolution consumed a bare `C` and never
  reached the typedef, and the instance clone now rewrites the injected name to
  the instance's tag (`inherited_class_name.cpp`, ADR 0032).
- ~~A range-for over a class~~ — fixed: the loop uses the class's egin() and
  nd() and compares pointers, and the reference form writes the elements.
- ~~Binary operators do not resolve to member overloads~~ — fixed for an
  operand that names an object (`*this`, a member access): the record comes
  from the address the operand names. A cast operand is not attempted.
- **Overloaded member functions.** `class_methods_` keeps one function per
  name and the two `operator[]` overloads share a symbol, so const and
  non-const dispatch is not distinct. Reads work through either.

`array<T, N>` written in a template *signature* also needs a pattern's value
argument to resolve through the stub binding, which is the missing piece for
free comparison operators on `std::array`.

## A class-template body resolves its parameters to the stub

Inside a class template's member body, a template argument is deduced from the
**pattern's stub type**, not from the instance's. `~unique_ptr()` calls
`destroy_owned(pointer)` and the IR for `unique_ptr<Item>` names the callee
`destroy_owned(ptr<i16>)`, so `delete` runs on an integer and destroys nothing.

Probes that separate "the destructor ran" from "its delete destroyed the object"
(`10 + entries * 2 + items`) locate it exactly: `delete` in `main` destroys,
`delete` in a *function* template destroys, `delete` in a *class-template*
destructor runs the destructor and destroys nothing, and delegating that
destructor to a function template still destroys nothing because the delegation
is instantiated with `i16`. A class-template destructor that only counts is
unaffected.

This is the stub-binding limitation ADR 0032 records, with its consequence for
owning class templates: `std::unique_ptr` cannot be correct until a
class-template body resolves template arguments from the instance.

## A copy and a move constructor of one arity

## A copy and a move constructor of one arity

Two constructors of the same name and arity are both recorded as `Box#1`, so the
member generation must match the **declaration's own** parameter category: it
walks the first `ParameterDeclaration`'s whole subtree for a `ReferenceDeclarator`
labelled `rvalue` and takes only the recorded type with the same category.
Matching on name and arity alone took the first recorded type for both, emitted
the move constructor twice, never generated the copy constructor, and left
whichever body the linker kept — a move that cleared its source silently did
nothing. A first-child walk is not enough: the reference declarator sits beside
the specifiers, not under the first child, and the duplicate just moves to the
other suffix. `tests/fixtures/c/copy_and_move_constructors.cpp` pins the pair.

## Debug display names

`.debug_info` `F` records carry a display name, so a C++ dump shows
`Widget::size`, `Widget::Widget`, `Widget::~Widget`, and `Counter::bump`
instead of the flattened symbol ([ADR 0030](docs/decisions/0030-debug-display-names.md)).
The symbol stays mangled, the KDBG format is unchanged, and display names are
not unique — a synthesized copy of an inherited method keeps the base's name —
so anything that resolves still uses the symbol. `krc_debug_names` checks the
names and `krc_run_debug_names` runs the same image.
## `<memory>` is blocked on three gaps

`std::unique_ptr` cannot be written yet, and an attempt is recorded in
`implementation.md` rather than half shipped. Reading a private member through
an **rvalue-reference parameter** in a template class body fails with *member
access on a non-record value* when the class also declares a **deleted copy
constructor**; a **member initializer** cannot read another object's member
through a reference parameter; and a **template overload set** is unsupported, so
three `make_unique` arities collide. Do not re-analyse an argument with
`analyze_expression` from the declaration path to work around the first one — it
re-enters declaration analysis and **hangs** the compiler on `brace_init.cpp`.

## KR16 host services

The KR16 VM implements the file services — `OPEN`, `CLOSE`, `READ`, `WRITE`,
`SEEK`, `STAT`, `UNLINK`, `RENAME`, `MKDIR`, `READDIR`, `TIME`, `RANDOM`, and
`FSINFO` over a sixteen-byte block in `R0`. [`docs/host-services.md`](docs/host-services.md)
owns the protocol. This repository has the wrappers (`targets/kr16/runtime/host.s`)
and the stream layer (`fileio.c`, declared by `<stdio.h>` under `__kr16__`).
**A seek leaves the descriptor unreadable**, which is a VM defect recorded above.

KR16 also has no aggregate return convention: the backend rejects one with
`KR16 aggregate returns are not implemented` rather than storing a truncated
value in `R0`, so `<stdlib.h>` omits `div`/`ldiv`/`lldiv` on that profile.
## KR16 heap

The linker now reports KR16's memory limits: `__heap_start` is the first byte
past the image, `__heap_end`/`__stack_limit` are `0xE000`, and
`__stack_top` is `0xF000` (the KR32 values stay as they were). The runtime
heap in `targets/kr16/runtime/libc.c` is a first-fit free list over that
region with forward *and* backward merging, splitting, and a `realloc` that
copies only the bytes it owns.

## Tagged definitions inside a typedef

`typedef struct Node { struct Node *next; } Node;` and the same form for enums
and unions are accepted: the specifier records itself as the definition, so the
second analysis a typedef declaration performs is not a redefinition. Pointer
arithmetic with an unsigned operand (`bytes - sizeof(int)`) converts the
offset to the index type instead of failing the verifier.

## KR16 stack arguments and varargs

KR16 passes the first three scalar arguments in `R1`-`R3` and pushes the rest
last-first, one two-byte slot each; the caller releases the area with
`LEA SP, [SP + 2 * count]` after `CALL`, and the callee reads argument
`n >= 4` at `[R4 + 4 + 2 * (n - 4)]` ([ADR 0028](docs/decisions/0028-kr16-stack-arguments.md)).
A variadic call pushes every argument, named and variable, in two-byte slots; the
callee reads parameter `i` at `[R4 + 4 + 2 * i]` and `va_start` yields
`R4 + 4 + 2 * N` ([ADR 0029](docs/decisions/0029-kr16-variadic-calls.md)), with
`<stdarg.h>` advancing two bytes per argument under `__kr16__`. The runtime
formats output too: `printf`/`snprintf`/`sprintf`/`vsnprintf` live in
`targets/kr16/runtime/libc.c` and handle every integer and string conversion the
profile can express; there is no floating point on KR16, so no `%f`.

## KR16 execution

`KRC_KR16_VM` points at `../kr16/build/shell-debug/kr16.exe` on a Windows
host; when it exists, nineteen `krc_run_kr16_*` tests execute the linked and
single-unit images and check their exit status. The VM reports the program's
exit status as its own, so `cmake/RunKr16Image.cmake` mirrors the KR32 runner
with `--max-cycles` instead of `--max-steps`.

## Known gaps in the matrix support

- Aggregates larger than 8 bytes travel by value as a pointer to a copy the
  caller owns (memory class, per `docs/abi.md`), and an aggregate read out of an
  object (a member, an array element, a dereference, a global) is a value like
  any other. What remains: an aggregate passed through a variadic argument is
  still unsupported.
- An FP-only aggregate of 9–16 bytes returns through `R4` rather than `F0`/`F1`.
- `vec4` and `mat4` are laid out with 4-byte alignment; the reference ABI
  documents 16. Tile transfers need only 4, so the code is correct, but a
  future change must match the documented alignment or amend `docs/abi.md`.
- The transform stack is a fixed 64 KiB region between the program stack and the
  heap; a deeply nested tile save could exhaust it, and nothing checks.

## Known gaps in RTTI

- Cross-casts between sibling classes, multiple and virtual inheritance, and
  `type_info::before()` are not implemented.
- `typeid` of a non-class type is rejected rather than producing a
  `_ZTIi`-style object.
- `-fno-rtti` is not implemented; RTTI objects are always emitted.
- `type_info`'s first word is zero and it has no virtual destructor, so code
  that deletes a `type_info` through a pointer would be wrong; nothing does.
- `<typeinfo>` is a subset: `bad_cast` works, `bad_typeid` and `bad_alloc` do
  not exist yet, and `type_info::hash_code` is absent.

## Goal recommendation

Keep the objective **“Proceed with implementation of M0 thru M10.”** If the new
session supports persisted goals, create that goal with a 3000-round cap. Do not
mark it complete while M8/M9/M10 items above are open, and do not treat the
previous `blocked` state as a technical blocker — it meant “paused by user.”
