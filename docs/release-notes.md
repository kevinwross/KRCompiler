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
- Four code-generation targets: KR32 (ILP32), KR16 (IP16), KR216 (IP16 with a
  32-bit `long`), and KR232 (ILP32 without a 64-bit integer). KR64 is described
  by a target profile and has no code generator.
- A runtime and C library for KR32, KR16, and KR232: allocator, stdio with integer
  and floating formatting, string and memory functions, conversions, `qsort` and
  `bsearch`, environment table, `atexit`, and the C++ ABI runtime pieces
  (throwing, unwinding, typeinfo, `bad_cast`). The KR216 runtime sources are in
  the tree; this build does not stage them.
- The debug format: KDBG sections in a linked image, source paths relative to a
  chosen root, and `.debug_info` records that name a member function by its
  source name (`Widget::size`), not its symbol.

## How to build and test

The toolchain is host software; KR32, KR16, KR216, and KR232 are output targets,
not hosts.

```pwsh
# Windows
pwsh -File .agents/doctor.msvc.ps1
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

The Linux presets are `linux-gcc-debug`, `linux-gcc-release`, `linux-clang-debug`,
and `linux-sanitize`, under WSL or on a Linux host. The executed tests need a VM
binary: `KRC_KR32_VM`, `KRC_KR16_VM`, and `KRC_KR232_VM` name them, and the run
tests are not registered when the file is absent. `AGENTS.local.md` records the
environment assumptions, and `implementation.md` records the counts the last gate
produced.

## Compatibility

- Object format, symbol mangling, the KR32, KR16, KR216, and KR232 ABIs, and the
  KDBG encoding are frozen as documented in [`docs/abi.md`](abi.md),
  [`docs/abi-kr16.md`](abi-kr16.md), [`docs/abi-kr216.md`](abi-kr216.md),
  [`docs/abi-kr232.md`](abi-kr232.md), [`docs/object.md`](object.md),
  [`docs/symbols.md`](symbols.md), and the decision records under
  [`docs/decisions/`](decisions/README.md).
- Output is deterministic: the same inputs produce byte-identical objects and
  images, which the test suite checks by building twice.
- A profile the back end cannot honestly represent is a diagnostic, not a
  silent miscompile: `long long` and floating point are rejected on KR16, a
  64-bit integer type is rejected on KR232, and `_Atomic` is rejected
  everywhere.

## Optimisation levels

`krc` has four levels. **`-O1` is the default**, so a build with no `-O` flag is
byte-identical to `-O1`; `krc_optimization_levels` asserts that identity and
that the level is deterministic. `-O1` runs the pass list in
[`docs/optimize.md`](optimize.md) §7 — constant folding, local
common-subexpression elimination, dead code elimination, single-block and
cross-block (`phi`) alloca promotion, empty-block elimination, and a register
allocator that keeps a value in a register rather than a frame slot.

**`-O0`** is straightforward lowering: every value keeps its frame slot, so it
is the level to use when a debugger needs a slot per source variable. It differs
from `-O1`, and the difference is asserted rather than assumed. Selection
quality — a literal operand folded into the instruction, an address computed in
the register it is needed in, an identity cast removed — belongs to lowering
rather than to a level (`D-049`), which is why `-O0` also improves on the
pre-optimiser compiler.

**`-O2` and `-Os`** take the aggressive path: the text pipeline runs to a second
fixed point, and the second result is kept only when it makes the text smaller,
so repetition cannot inflate code.

Measured against the pre-optimiser compiler, on the corpus the benchmarks record
in `benchmarks/size.tsv` and `benchmarks/ticks.tsv`:

| | `-O0` | `-O1` |
|---|---|---|
| KR32 corpus instructions | −27.0% | −41.7% |
| KR16 corpus instructions | −24.1% | −27.7% |
| `bench` ticks, KR32 | −29.1% | −43.3% |
| `bench` ticks, KR16 | −28.0% | −30.7% |

KR16 gains less under `-O1`: it has five registers with two reserved for the
frame and stack pointers, and its two-operand ALU makes an allocated value cost
the same as a frame access, so it allocates nothing. That asymmetry is measured
rather than assumed — see [ADR 0049](decisions/0049-register-allocation-wiring.md).

## What is not in this release

- KR64 code generation, and the kernel profile the plan reserves for it.
- Native Linux and ARM64 CI. The Linux rows are built in WSL and pass there;
  ARM64 host support is deferred and untested.
- Threads. `<threads.h>` refuses to be included: the VM is single-threaded, so
  `thread_local` is static storage with a guarded initializer.
- A debugger user interface. The symbols are complete and documented; consuming
  them is the debugger's job.
- Template machinery beyond the recorded subset: member templates, multiple packs
  in one template, and the fold forms other than `(expr op ...)`. A function
  template is not overloadable: two declarations that differ only in arity are
  `redefinition of function`, because the template table is keyed by bare name.
  Variadic function and class templates, `sizeof...(pack)`, left folds over a
  binary operator, pack expansion in a call or a `new` argument list, partial
  specialisation of a class template selected by argument count, and recursive
  variadic class templates do work. Explicit class-template arguments parse and
  instantiate, both `Box<int>` and `template <> struct Box<int>`; `sysroot/include/tuple`
  depends on the former throughout. Deduction of a non-type template parameter from
  an argument works: `int size(Fixed<N>&)` called with `Fixed<2>` binds `N` to 2, and
  the by-value, pointer, two-parameter and alongside-a-pack forms all bind. A
  qualified use through a template-id with a non-type argument (`Index<0>::pick`)
  parses but does not resolve: the instance's tag is found and its static members
  are registered too late for that lookup.
- `get<I>`, and the parts of `<tuple>` that need it. `sysroot/include/tuple` does
  ship — `tuple_size`, `tuple_head`, `tuple_tail`, the members `.head` / `.tail`,
  in-place construction, and `make_tuple` — with link-and-run fixtures on both
  targets. The element constructor takes one argument per element and works for three
  as readily as for two, `make_tuple` returns two-element, three-element and
  mixed-type tuples with each element keeping its own width, and `tuple<>` can be
  declared as a local. `get<I>` is the one
  absence, and the header states its measured reason: a member function template does
  not parse here, and neither does a qualified use through a template-id with a
  non-type argument.
- A callable supplied as a *template parameter* is still rejected, so no generic
  algorithm can take `std::function`-style function objects.
- Release tagging. The `v1.0` tag is an operator decision and is not created by
  this document.

## Where the detail lives

`implementation.md` is the live tracker: milestone checklists, evidence, the
running log, and the gate records with per-configuration counts. `plan.md` is
the design contract and the decision log. Neither is restated here.
