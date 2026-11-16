# Release notes — v1.0

This is the reference release of KRCompiler: a retargetable C and C++ compiler
for the KR-family virtual machines, with its object format, linker, runtime, and
debug information.

## What the release contains

- `krc`, the compiler driver: preprocess, parse, check, lower to typed SSA IR,
  select instructions, emit assembly, write `.kro` objects, or link an image.
- `krld`, the linker: resolves objects, applies relocations, checks the object
  CRC, synthesises the stack and heap symbols, and writes `.kdbg` debug data.
- The C11 front end and the C++17 subset recorded in
  [`implementation.md`](../implementation.md), including classes, inheritance,
  virtual dispatch, RTTI, exceptions, destructors with cleanup, templates
  (function, class, and non-type parameters), and `thread_local`.
- Two code-generation targets: KR32 (C11 and C++) and KR16 (C, with 16-bit
  words). KR64 is described by a target profile and has no code generator.
- A runtime and C library for both targets: allocator, stdio with integer and
  floating formatting, string and memory functions, conversions, `qsort` and
  `bsearch`, environment table, `atexit`, and the C++ ABI runtime pieces
  (throwing, unwinding, typeinfo, `bad_cast`).
- The debug format: KDBG sections in a linked image, source paths relative to a
  chosen root, and `.debug_info` records that name a member function by its
  source name (`Widget::size`), not its symbol.

## How to build and test

The toolchain is host software; KR32 and KR16 are output targets, not hosts.

```pwsh
# Windows
pwsh -File .agents/doctor.msvc.ps1
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

The Linux presets are `linux-gcc-debug` and `linux-gcc-release` under WSL or on
a Linux host. The executed tests need a VM binary: `KRC_KR32_VM` and
`KRC_KR16_VM` name them, and the run tests are not registered when the file is
absent. `AGENTS.local.md` records the environment assumptions, and
`implementation.md` records the counts the last gate produced.

## Compatibility

- Object format, symbol mangling, the KR32 and KR16 ABIs, and the KDBG encoding
  are frozen as documented in [`docs/abi.md`](abi.md),
  [`docs/abi-kr16.md`](abi-kr16.md), [`docs/object.md`](object.md),
  [`docs/symbols.md`](symbols.md), and the decision records under
  [`docs/decisions/`](decisions/README.md).
- Output is deterministic: the same inputs produce byte-identical objects and
  images, which the test suite checks by building twice.
- A profile the back end cannot honestly represent is a diagnostic, not a
  silent miscompile: `long long` and floating point are rejected on KR16, an
  aggregate return on KR16 is rejected, and `_Atomic` is rejected everywhere.

## What is not in this release

- KR64 code generation, and the kernel profile the plan reserves for it.
- Native Linux and ARM64 CI. The Linux rows are built in WSL and pass there;
  ARM64 host support is deferred and untested.
- Threads. `<threads.h>` refuses to be included: the VM is single-threaded, so
  `thread_local` is static storage with a guarded initializer.
- A debugger user interface. The symbols are complete and documented; consuming
  them is the debugger's job.
- Template machinery beyond the recorded subset: member templates, explicit
  template arguments on a *class* template, multiple packs in one template, and
  the fold forms other than `(expr op ...)`. Variadic function and class
  templates, `sizeof...(pack)`, left folds over a binary operator, pack expansion
  in a call or a `new` argument list, partial specialisation of a class template
  selected by argument count, and recursive variadic class templates do work.
- A `<tuple>` header. A recursive variadic class template — the shape a tuple
  needs — compiles and runs, but the header itself is not written, so `<tuple>`
  cannot be included.
- A callable supplied as a *template parameter* is still rejected, so no generic
  algorithm can take `std::function`-style function objects.
- Release tagging. The `v1.0` tag is an operator decision and is not created by
  this document.

## Where the detail lives

`implementation.md` is the live tracker: milestone checklists, evidence, the
running log, and the gate records with per-configuration counts. `plan.md` is
the design contract and the decision log. Neither is restated here.
