# KRCompiler

KRCompiler is a retargetable C/C++ compiler for the KR-family virtual machines. It ships `krc` — the driver, assembler, and image writer — and `krld`, the linker. The tree holds a C11 and C++17 front end, a typed-SSA IR with a verifier and optimiser, per-target backends, the KRO object format, the KDBG symbol and debug format, and a per-target runtime and C library.

## Status

The plan is approved (2026-11-09). [implementation.md](implementation.md) is the live tracker: milestone status, test counts, gate evidence, and the running log live there and are not repeated here.

- **Complete locally:** M0 foundation, M1 C front end and sysroot, and M4 C core and debug format. M8 v1.0 hardening has all of its evidence and waits only on the `v1.0` tag.
- **In progress:** M2 and M3 backend and linker hardening, M5 C++ core, M6 C++11/14/17, M7 KR16 runtime, and M9/M10 v1.1 runtime and language completion.
- **Remote CI is not configured.** The GitHub Actions workflow was removed on 2026-11-23 (`e3bed04`); every gate on record is a local run on the hosts named in [AGENTS.local.md](AGENTS.local.md).

## Targets

`krc --target=NAME` selects one of five profiles. Each document owns its target's data model and ABI rules.

| Target | Data model | Document |
|---|---|---|
| `kr32` | ILP32, 32-bit pointers | [docs/abi.md](docs/abi.md) |
| `kr16` | IP16, 16-bit pointers and `long`, no floating point or 64-bit integer | [docs/abi-kr16.md](docs/abi-kr16.md) |
| `kr216` | IP16 with a 32-bit `long` | [docs/abi-kr216.md](docs/abi-kr216.md) |
| `kr232` | ILP32 with no 64-bit integer | [docs/abi-kr232.md](docs/abi-kr232.md) |
| `kr64` | LP64 | [docs/targets/kr64.md](docs/targets/kr64.md) — profile only; `krc` has no kr64 code generator |

`krc --list-targets` prints the same set with each profile's capabilities; the descriptor fields are defined by [docs/targets/schema.md](docs/targets/schema.md).

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

Other presets: `windows-msvc-release`, `linux-gcc-release`, `linux-clang-debug`, and `linux-sanitize`. The Linux presets use Ninja and run inside WSL when the host is Windows. [docs/kr-build-instructions.md](docs/kr-build-instructions.md) is the family build and gate guide: presets, gate flags, install prefixes, the generated test images, and the clean-build procedure.

## Build outputs

Host binaries are written to `build/<preset>/host/bin/<config>/`. Target-facing files are staged under
`build/<preset>/stage/`: `stage/sysroot/` mirrors the installed sysroot,
`stage/sysroot/<target>/lib/` holds each staged runtime, and `stage/tests/` holds linked test
images. `build/<preset>/kr232-images/` holds the KR232 images that the KR232 test rows read.
Target source lives under `targets/`; the host compiler never builds it directly.
See [docs/tree.md](docs/tree.md).

## What the tools do today

- **C front end:** preprocessor, lexer, C11 parser, and Sema; `-E`, `-fsyntax-only`, and `--dump-ast`.
- **C++ front end:** classes with single inheritance, virtual dispatch, RTTI, exceptions where the target supports them, function and class templates including partial specialisation, variadic packs and fold expressions, `constexpr` including `if constexpr`, lambdas with captures, structured bindings, `enum class`, range-for, and `auto`/`decltype`.
- **IR:** typed SSA with a verifier, a deterministic textual dump, and per-function statistics (`--dump-ir`, `--dump-stats`), plus mem2reg, inlining, and the optimisation pipeline.
- **Optimisation:** `-O0`, `-O1`/`-O` (the default), `-O2`, and `-Os`.
- **Backends:** KR32, KR16, KR216, and KR232 emit assembly (`-S`), KRO objects, and runnable images.
- **Linking:** `krld` links KRO objects and archives into `.kr32`, `.kr16`, `.kr216`, and `.kr232` images with a deterministic map, section sizes, layout scripts, weak symbols, and section garbage collection. `krc image` exposes the same link from the driver.
- **Debug information:** KDBG v1 plus the v1.1 scope records; `krc --dump-symbols` prints text or JSON.
- **Runtime:** per-target `crt0`, the C string and memory library, `printf`, `malloc`, and host and file services where the target provides them.
- **Determinism:** the same inputs built twice must be byte-identical; the suite compares objects, images, and CLI output.
- **Tooling:** `krc_extract_fixtures`, `krc_gen_sysroot_bits`, and the install and packaging scripts.

```sh
krc -fsyntax-only path/to/file.c
krc --target=kr16 -fsyntax-only path/to/file.c
krc -E path/to/file.c
krc --dump-ast path/to/file.c
krc --dump-ir path/to/file.c
krc -S path/to/file.c
krc -c path/to/file.c -o path/to/file.kro
krld path/to/file.kro ... -o path/to/image.kr32
```

`krc --help` and `krc image --help` list the options; [docs/krc-user-manual.md](docs/krc-user-manual.md) explains them.

## Documentation

[docs/README.md](docs/README.md) is the index. Start with:

- [docs/krc-user-manual.md](docs/krc-user-manual.md) — building and linking C and C++ programs.
- [docs/kr-build-instructions.md](docs/kr-build-instructions.md) — configuring, building, and gating the family.
- [docs/tree.md](docs/tree.md) — the host/target boundary and the build output contract.
- [docs/release-notes.md](docs/release-notes.md) — what v1.0 contains and what it does not.

**Branches.** `master` is the shipped line. `adopt-krc`, `kdbg-v1-1`, and `libc` remain as history and as the revisions consumers pin; new work lands on `master`.
