# KRCompiler

KRCompiler is a retargetable C/C++ compiler for the KR-family virtual machines. It ships as `krc` (compiler driver) and `krld` (linker), with a typed SSA IR, per-target backends, an object format, a symbol/debug format, and a common runtime.

The plan is approved. **M1 is complete locally** and **M2 is in progress**: the frozen typed-SSA IR, C IRGen, KR32 assembly emitter, encoder, assembler, graph-colouring allocator, and peepholes are implemented; `hello.c` and `mainentry.c` emit assembly, and `hello` assembles, links with a stub runtime, and runs on KR32. G2a has local evidence. **M3 is in progress**: KRO v1 is frozen, `krc -c` writes objects, `krld` links them into a KR32 `.k32` executable, and `src/runtime/kr32/crt0.s` starts programs. G2a and G2b pass locally. G3 passes: the extracted `binarytree.c`, `libc-test.c`, and `varargs.c` compile, link, and run on KR32 with expected output. G4 passes through the ADR 0008 `.debug_info` bridge: the KR32 GUI/DAP debugger sets a source breakpoint, stops in `binarytree.c`, and shows frames, locals/globals, aggregate expansion, and memory. M4 is complete locally. M5 has reached G5 locally: classes, methods, constructors, block/function-scope destructors, `new`/`delete` including arrays, default copy, single inheritance, static devirtualization, and vptr-based dynamic dispatch through pointers, references, and base-method self calls work. M6 has started: `nullptr`, `auto`/`decltype`, range-for, `constexpr`, and scoped `enum class` work. M4 now handles positional, designated, and nested designated aggregate initializers. M6 has started: `nullptr`, `true`/`false`, `auto` locals and return types, `decltype`, range-for over arrays and braced lists, `constexpr` variables/functions with branches, loops (for/while/do), recursion, and constant array bounds, C++ `static_assert`, named casts, `if constexpr` with branch discard, inline variables and weak inline functions/methods, scoped `enum class` with underlying types, applied `alignas`, flat namespaces, qualified namespace lookup, `using` aliases, default function/method arguments, constructor overloads, constructor member-initializer lists and delegating constructors, reference-returning and chained assignment via `return *this`, braced class/aggregate initialization, copy and move constructors/assignment via rvalue casts and `std::move`, `explicit`, `noexcept`/`final`, reference variables/parameters and reference members, member/free operators including reference-returning subscript, call, unary, distinct prefix/postfix increment-decrement, and compound assignment, implicit array-member indexing, no-capture lambdas with deduced return types and by-value, by-reference, init, and default [=]/[&] capturing lambdas, structured bindings over structs and arrays, array `new[]`/`delete[]` lifetimes, placement new, and int-bound function-template syntax work. `hello.cpp` and `balanced-tree.cpp` run; real template deduction, capture lambdas, and move semantics remain. See [plan.md](plan.md) and [implementation.md](implementation.md). See [plan.md](plan.md) and [implementation.md](implementation.md).

## Build

### Windows (MSVC)

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

### Linux (WSL, GCC)

```sh
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

Other presets: `linux-gcc-release`, `linux-clang-debug`, and `linux-sanitize`.

## Current M2 surface

- Typed SSA IR types, constants, globals, functions, blocks, and instructions.
- `IRBuilder`, dominance/type verifier, and deterministic textual IR dump.
- C IRGen for scalar types, pointers, arrays, calls, variadic calls, globals/string literals, returns, and `if`/`while`/`do`/`for` control flow.
- KR32 assembly emitter with stack slots, call/varargs staging, prologue/epilogue, and `.text`/`.data` output.
- In-tree KR32 base encoder, C ABI register descriptor, assembly parser/encoder with labels and relocations, graph-colouring allocator, and peephole pass.
- KRO v1 object writer/reader, `krc -c` object emission, `krld` section/symbol/relocation linking, the KR32 `.k32` executable writer, the KR32 crt0 runtime, a C `puts`, and linker-defined heap/stack symbols.
- Recursive named structs, `.`/`->` member access, struct layout/GEP lowering, bit-fields, positional aggregate initializers, function pointers, integer/floating-point varargs, integer/string/float `printf`, a C string/memory library, and a free-list allocator.
- C front end from M1: lexer, preprocessor, parser, Sema, generated sysroot bits, and `krc -fsyntax-only`.
- `krc -E`, `krc --dump-ast`, `krc --dump-ir`, and `krc -S`.
- KR32/KR16/KR64 target profiles and frozen target descriptor schema.
- `krc`, `krld`, `krc_extract_fixtures`, and `krc_gen_sysroot_bits`.
- CMake presets, CI, and the CLI determinism harness.

```sh
krc -fsyntax-only path/to/file.c
krc --target=kr16 -fsyntax-only path/to/file.c
krc -E path/to/file.c
krc --dump-ast path/to/file.c
krc --dump-ir path/to/file.c
krc -S path/to/file.c
krc -c path/to/file.c -o path/to/file.kro
krld path/to/file.kro ... -o path/to/image.k32
```

`krc --version`, `krc --help`, `krc --list-targets`, and `krld --version` work. Code generation is not implemented yet; compiling without `-fsyntax-only` parses and checks the input, then exits with a not-yet-implemented message.

## Documentation

- [docs/README.md](docs/README.md)
- [docs/abi.md](docs/abi.md)
- [docs/object.md](docs/object.md)
- [docs/symbols.md](docs/symbols.md)
- [docs/ir.md](docs/ir.md)
- [docs/attributes.md](docs/attributes.md)
- [docs/targets/kr32.md](docs/targets/kr32.md), [kr16.md](docs/targets/kr16.md), [kr64.md](docs/targets/kr64.md)
