# Source tree and build output reorganization

**Status:** draft — phase 1 and phase 2 approved 2026-11-11; phase 3 onward not started.

**Scope:** separate host compiler sources from target-compiled sources, define where target
artifacts are built, and make debug source paths independent of the host OS.

**Recommendation in one sentence:** keep `src/` as the host-only compiler tree, move all
target-compiled source to a top-level `targets/`, keep `sysroot/` as the target-facing
header/library interface, stage every build into a single install-shaped `stage/`, and make
debug paths logical and `/`-separated with a root table rather than physical host paths.

---

## 1. Current state

### 1.1 Host and target source are mixed by directory

- Host compiler code lives under [src](../src): `frontend`, `ir`, `irgen`, `backend`, `asm`,
  `linker`, `debug`, `target`, and `tools`.
- Target-compiled source also lives under `src/runtime`:
  [src/runtime/kr32/libc.c](../src/runtime/kr32/libc.c) and
  [src/runtime/kr32/crt0.s](../src/runtime/kr32/crt0.s).
- Names alone are not a reliable boundary. `src/backend/kr32_backend.cpp` and
  `src/asm/kr32_assembler.cpp` are host C++ that generate KR32 output; they are not target
  source. `src/runtime/kr32/libc.c` is target C compiled by `krc`.

### 1.2 `sysroot/` is a target interface, not a source tree

- Common target headers are under `sysroot/include/`; per-target headers are under
  `sysroot/<target>/include/bits/`.
- `sysroot/<target>/lib/` is reserved for prebuilt runtime libraries but does not exist yet.
  The approved layout is in [plan.md](../plan.md) §5.3, lines 298–307.
- The checked-in `bits/` files are a fallback. CMake generates authoritative copies into
  `build/<preset>/generated-sysroot`, and `krc` searches generated first
  ([CMakeLists.txt](../CMakeLists.txt) lines 69–88,
  [sysroot/README.md](../sysroot/README.md) line 3).

So “sysroot contains all kr32/kr16/kr64 files” is not accurate today: it contains headers
only, plus generated fallbacks. It contains no runtime source and no libraries.

### 1.3 Build output is split across the build tree and the source tree

- Host binaries go to `build/<preset>/bin/<config>` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`).
- Generated sysroot bits go to `build/<preset>/generated-sysroot`.
- Test executables and target images go wherever the test command says, usually
  `build/<preset>/...`.
- A direct `krc -c foo.c` with no `-o` writes `foo.kro` beside the input, which can put target
  artifacts in the source tree.
- There is no install-shaped staging tree and no single place that represents a target.

### 1.4 Debug paths are currently host-specific

`krc` records the physical path it loaded. With a Windows build and an absolute source path,
`krc --dump-symbols` shows:

```text
source 0 C:\Users\kevin\source\repos\KRCompiler\tests\fixtures\c\hello.c
```

That violates the intended contract that objects carry no absolute host paths
([docs/object.md](../docs/object.md) line 118) and the determinism/path-remapping decision
D-035 ([plan.md](../plan.md) line 1550). A Linux build would record
`/home/.../tests/fixtures/c/hello.c`. Sysroot paths have the same problem and can appear with
either `/` or `\`.

The path comes from `SourceManager::load_file` storing `path.string()`
([src/frontend/source_manager.cpp](../src/frontend/source_manager.cpp) line 98), then into
instruction debug records and KDBG sources
([src/debug/kdbg.cpp](../src/debug/kdbg.cpp) lines 241–245).

### 1.5 Runtime is not tracked per target

`src/runtime` has one target implementation, KR32. There is no KR16 or KR64 runtime source,
no per-target manifest, and no build target that produces a reusable runtime library. Target
tests recompile `libc.c` for each linked fixture via
[cmake/LinkRuntime.cmake](../cmake/LinkRuntime.cmake).

---

## 2. Proposed source tree

```text
src/                         host compiler only; built by MSVC/GCC/Clang
  frontend/  ir/  irgen/  backend/  asm/  linker/  debug/  target/  tools/
include/krc/                 host public headers only
targets/                     target source only; never built by the host compiler
  common/runtime/            portable C runtime pieces shared by every target
  kr32/runtime/              KR32 libc implementation, crt0, syscall/FP helpers
  kr16/runtime/              KR16 crt0, syscall and 64-bit helpers
  kr64/runtime/              placeholder until the KR64 backend exists
sysroot/                     target interface: headers plus installed/prebuilt libraries
  include/
  kr32/include, kr32/lib
  kr16/include, kr16/lib
  kr64/include, kr64/lib
tests/
  unit/                      host C++ doctest and compiler unit tests
  fixtures/                  shared C/C++ inputs
examples/                    target example programs
```

### 2.1 The boundary rule

- **`src/` and `include/`:** host-only. Any `.c`/`.s` intended for `krc` is a boundary
  violation.
- **`targets/`:** target-only. Host CMake never adds these files to a host executable; a
  custom build step invokes `krc`/`krld`.
- **`sysroot/`:** target interface. Headers are read by `krc`; `lib/` contains prebuilt
  target objects/archives. Nothing here is host-compiled.
- **`tests/host`:** host C++ only. **`tests/target`:** target programs and expected output.

This is a smaller move than renaming `src/` to `host/`, keeps the approved `src/` layout and
all existing includes, and still gives one unambiguous rule. If a hard `host/` root is wanted
later, it can be a mechanical rename; it should not block this cleanup.

### 2.2 Target source split

Start by moving the current files unchanged:

```text
src/runtime/kr32/libc.c  -> targets/kr32/runtime/libc.c
src/runtime/kr32/crt0.s  -> targets/kr32/runtime/crt0.s
```

Do **not** split common code on day one. Move first, keep behavior identical, then factor the
portable allocator, string/memory, and integer/string `printf` into
`targets/common/runtime/` when the KR16 runtime is brought up. The split should be driven by
the first real KR16 compile, not guessed in advance.

Per-target directories may carry private headers under `targets/<target>/include/`; public
target headers stay in `sysroot/<target>/include/`.

---

## 3. Build output tree

### 3.1 Build layout

Every preset gets one host area and one install-shaped stage:

```text
build/<preset>/
  host/
    bin/<config>/             krc, krld, generators
    lib/                      host libraries, if any
    obj/                      host object files, if relocated
  stage/                      mirrors the installed toolchain
    sysroot/
      include/
      kr32/include/           checked-in headers + generated bits
      kr32/lib/               libkrcrt.kro, crt0.s
      kr16/include/, kr16/lib/
      kr64/include/
    targets/
      kr32/runtime/obj/       per-source .kro
      kr16/runtime/obj/
    tests/                    linked images and test data
  CMakeFiles/                 CMake internals
```

Rules:

1. **The source tree is never an output directory.** Tests and the build system always pass
   an explicit `-o` under `stage/`. A direct `krc` invocation still follows the normal
   compiler convention (output beside input or current directory); documentation should
   recommend `-o`.
2. **Host and target output never share a directory.** `host/` contains only host-built
   executables/libraries; `stage/` contains only target-facing files.
3. **`stage/sysroot` is the same shape as the installed sysroot.** Development and installed
   runs then use one path-resolution model.
4. **Generated bits are staged into the sysroot.** Replace the separate
   `generated-sysroot` root with `stage/sysroot/<target>/include/bits`; there is no second
   search root and no build path to leak into debug info.
5. **Each target has one runtime build.** The runtime is compiled once per preset/target, not
   once per fixture. Fixtures link the staged runtime object. Until a KRO archive writer is
   added, the staged runtime is the single `libkrcrt.kro` object.
6. **No cross-preset artifacts.** A Debug stage links only Debug runtime objects; Release
   links Release.

### 3.2 CMake changes

- Set `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/host/bin` (with per-config
  suffix) and `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/host/lib`.
- Add `KRC_STAGE_DIR=${CMAKE_BINARY_DIR}/stage` and `KRC_STAGE_SYSROOT=${KRC_STAGE_DIR}/sysroot`.
- Add a custom target `krc_stage_sysroot`:
  - copy `sysroot/include/` and `sysroot/<target>/include/` into the stage;
  - run `krc_gen_sysroot_bits` into `stage/sysroot/<target>/include/bits`;
  - never modify the checked-in fallback during a build.
- Add `krc_target_runtime_<target>` custom targets:
  - depend on `krc` and `krc_stage_sysroot`;
  - invoke `krc --target=<target> -c ... -o ...` with the staged sysroot as its default;
  - compile `libc.c` to `stage/sysroot/<target>/lib/libkrcrt.kro` and copy the staged
    `crt0.s`;
  - link fixtures from `libkrcrt.kro` and the staged `crt0.s`, not recompile `libc.c`.

### 3.3 `krc` sysroot lookup

The compile-time absolute `KRC_SYSROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sysroot"` must not be
the release default. Proposed lookup order:

1. `--sysroot=<dir>`.
2. `KRC_SYSROOT` environment variable, if set (for build systems and tests).
3. Sysroot installed relative to the executable: `<exe>/../share/krc/sysroot`.
4. Compile-time install prefix sysroot (`CMAKE_INSTALL_FULL_DATADIR/krc/sysroot`).
5. Development fallback only when explicitly enabled, e.g. `KRC_DEV_SYSROOT`.

This makes a copied/installed toolchain relocatable. Host build directories are never baked
into release binaries.

### 3.4 Install layout

```text
<prefix>/
  bin/krc, bin/krld
  share/krc/sysroot/
    include/
    kr32/include/, kr32/lib/
    kr16/include/, kr16/lib/
    kr64/include/
```

Target runtime source is not installed. Only headers and prebuilt libraries are. The
debugger resolves the logical `sysroot` root to this installed directory.

---

## 4. Debug source paths

### 4.1 Requirements

- No drive letters, UNC prefixes, leading `/`, `..`, or host separators in object/KDBG
  source records.
- Always `/`, independent of the build host.
- A path must identify its logical root, not its physical location.
- The same source compiled from two different checkout/build directories must produce
  identical debug source tables.
- `#line` filenames and `__FILE__` follow the same mapping.

### 4.2 Recommended model: logical roots

Record a small root table and store each source path relative to a root:

| Root kind | Logical base | Example source |
|---|---|---|
| `project` | project/workspace root | `src/main.c`, `tests/target/hello.c` |
| `sysroot` | installed or staged sysroot | `include/limits.h`, `kr32/include/bits/types.h` |
| `external` | external include root | `third_party/foo/foo.h` |
| `generated` | build-generated source | `generated/config.c` |

A KDBG source record then carries `(source_id, root_id, relative_path)` instead of a
physical path. The debugger maps root ids to real directories from its launch configuration
(`sourceRoot`, `sysroot`, or an equivalent DAP setting).

### 4.3 KDBG format consequence

KDBG v1 has no root table; the source record is only `(id, path)` and the path is currently
physical. Recommended:

- Bump KDBG to a backward-compatible **v1.1** or v2 with a `SourceRoot` section and an
  optional root id on source records.
- Keep the v1 reader working; old images remain readable with a single `project` root.
- Emit roots by default so installed toolchains do not guess.

If a format change is deferred, the fallback is to store paths with explicit portable
prefixes (`sysroot/...`, `src/...`, `tests/...`, `external/...`) and require the debugger to
map those prefixes. That is simpler but less precise; the root table is the better long-term
contract.

This is a symbol-format change: it needs an ADR and a KDBG version decision before
implementation.

### 4.4 Mapping algorithm

At file ingestion (`SourceManager`/preprocessor), map before recording:

1. Normalize separators and remove `\\?\`/UNC prefixes.
2. Apply explicit `--remap-path-prefix=FROM=TO` mappings, longest prefix first.
3. If under the staged/installed sysroot, map to root `sysroot`.
4. If under the project/workspace root, map to root `project`.
5. If under a configured external root, map to root `external`.
6. Otherwise use root `external` with the basename, or reject under `--debug-paths=strict`.
7. Emit only `/`-separated relative paths; collapse `.` and reject `..`.

Diagnostics may keep a physical or user-facing path; debug records use the mapped path. Add
`-fdebug-prefix-map`/`--remap-path-prefix` and a `--debug-path-mode=physical|logical` switch
for debugging the mapper itself.

### 4.5 Tests for paths

- Compile a fixture that includes `<limits.h>` from an absolute sysroot and an absolute
  source path; assert KDBG sources are `sysroot/include/limits.h` and
  `tests/target/<fixture>.c`, with no `\`, `:`, or leading `/`.
- Compile the same fixture from two different checkout/build roots; assert identical KDBG
  source tables.
- Round-trip roots through the reader/writer; assert old v1 images still load.
- Run the path test in the Windows and Linux matrices and compare normalized JSON.

---

## 5. Runtime tracking for KR16/KR32/KR64

### 5.1 Manifest generated from the target descriptor

Do not duplicate `TargetProfile` facts in CMake or runtime headers. Add a host generator
(extend `gen_sysroot_bits` or add `krc_gen_target_config`) that writes, per target:

- `kr_target.h`: `KR_TARGET_NAME`, `KR_POINTER_BITS`, `KR_CHAR_UNSIGNED`, `KR_INT_BITS`,
  `KR_LONG_BITS`, `KR_HAS_LONG_LONG`, `KR_HAS_FPU`, `KR_HAS_DOUBLE`, `KR_HAS_ATOMICS`,
  `KR_HAS_THREADS`, `KR_HAS_TLS`, `KR_HAS_CPP`, `KR_ABI_VERSION`.
- `target.cmake`: target id, sysroot paths, object/library names, supported language modes,
  and backend availability.

The compiler remains the source of truth; the runtime consumes generated facts. Common
runtime code uses `KR_HAS_FPU`/`KR_HAS_LONG_LONG` rather than `#ifdef __kr32__` wherever the
choice is a profile property.

### 5.2 Per-target build

| Target | Runtime source | Output |
|---|---|---|
| KR32 | `targets/common/runtime` + `targets/kr32/runtime` | `stage/sysroot/kr32/lib/libkrcrt.kro` |
| KR16 | `targets/common/runtime` + `targets/kr16/runtime` | `stage/sysroot/kr16/lib/libkrcrt.kro` |
| KR64 | config only; no backend | no runtime until backend exists |

- KR32 includes FP/matrix helpers.
- KR16 excludes float/double formatting and uses integer/string paths; its runtime manifest
  must fail the build if a common source pulls in unavailable features.
- KR64 is generated and validated but not built while `backend_available` is false.
- Each runtime archive carries the target id and ABI version for linker validation.
- Staged runtime stamps include target/ABI/compiler hash; a mismatch fails the target test,
  not the host compiler build.

### 5.3 Runtime staging and tests

- Build runtime once per preset/target: `krc_target_runtime_kr32`,
  `krc_target_runtime_kr16`.
- Link fixtures against the staged runtime object and staged `crt0`.
- Add a determinism test that rebuilds one runtime into a second stage and compares bytes.
- Add a KR16 smoke test that compiles and links a minimal C program against the KR16 runtime
  as soon as the runtime exists.
- Keep the host `krc` build independent of target runtime targets: a host-only build must
  still succeed when no target runtime is built.

---

## 6. Migration phases

### Phase 0 — lock the boundary and expose the path defect

- Add a tree-boundary check (CMake test or script): host targets contain no target `.c`/`.s`;
  `targets/` and `sysroot/` are never host include/source directories.
- Add a debug-path test that currently fails against the physical path.
- Record current `KDBG` output as the before-state.

### Phase 1 — move target source

**Status: approved 2026-11-11; implemented.**

- Create `targets/` and move `src/runtime/kr32` there.
- Update [cmake/LinkRuntime.cmake](../cmake/LinkRuntime.cmake), tests, README, and
  `implementation.md` paths.
- No behavior change; all existing tests remain green except intentional path updates.

### Phase 2 — stage the build

**Status: approved 2026-11-11; implemented.**

- Introduce `build/<preset>/host` and `build/<preset>/stage`.
- Move generated bits into `stage/sysroot/<target>/include/bits`.
- Add `--sysroot` and use the staged sysroot as the compiler default in build/test runs.
- Move host binaries to `host/bin`; move test outputs under `stage/tests`.
- Add `krc_target_runtime_kr32` and link fixtures from the staged runtime object.

### Phase 3 — portable debug paths

- Add the path mapper and `--remap-path-prefix`.
- Normalize `SourceManager`, `#line`, and `__FILE__`.
- Implement the KDBG root decision (recommended: additive root section with version bump).
- Add cross-host path tests and debugger resolution documentation.

### Phase 4 — runtime matrix

- Add the target config generator.
- Split common and per-target runtime sources.
- Build KR32 and KR16 runtimes; generate KR64 config only.
- Add runtime ABI/target checks and determinism tests.

### Phase 5 — install and CI

- Add install rules for `bin/` and `share/krc/sysroot/`.
- Remove absolute source `KRC_SYSROOT_DIR`; implement executable-relative lookup.
- Add Windows/Linux CI checks for install-tree use and normalized debug paths.
- Update `README.md`, `AGENTS.local.md`, `docs/README.md`, `docs/symbols.md`,
  `docs/object.md`, `plan.md` §4.5/§5.3/§14.1, and `implementation.md`.

---

## 7. Acceptance criteria

- `git grep` finds no target `.c`/`.s` under `src/`; host CMake builds no `sysroot/` or
  `targets/` file.
- `krc -c` for a target produces output only where `-o` says; tests write only under
  `stage/`.
- `stage/sysroot` has the same layout as the installed sysroot.
- A copied/installed toolchain finds its sysroot without source-tree paths.
- KDBG sources contain `/` only, no drive/absolute/`..` components, and are identical when
  the same source is built from two different host roots.
- The debugger can resolve project and sysroot roots from launch configuration.
- KR32 and KR16 runtime objects build once per preset; KR64 config is generated but not
  built.
- Existing 202-test matrix stays green, plus boundary/path/runtime tests.

---

## 8. Required records and documentation

- **ADR:** portable debug source paths and KDBG root/version decision (symbol format).
- **ADR check:** confirm whether the runtime target manifest and staged sysroot change any
  target-description or ABI contract; if yes, add an ADR. The archive/object format itself
  does not change.
- **plan.md:** update §4.5 tree, §5.3 sysroot staging, §14.1 runtime split, and any
  Appendix A decision whose statement changes.
- **docs:** add `docs/tree.md` as the authoritative boundary document; update
  `docs/symbols.md` for roots, `docs/object.md` if debug sections change, and
  `AGENTS.local.md` for the new target build commands.
- **implementation.md:** one log entry per phase, with test counts.

---

## 9. Risks and choices

| Choice | Recommendation | Why |
|---|---|---|
| `src/` host vs `host/` root | Keep `src/` host-only | Minimal churn; rule is still unambiguous |
| Runtime location | `targets/<target>/runtime` | Separates source from installed `sysroot` interface |
| Common split timing | Move first, split during KR16 bring-up | Avoid guessing portability boundaries |
| Debug path model | Logical roots + KDBG root table | Debugger resolves installed trees without host paths |
| KDBG root format | **OPEN** — v1.1 source-root decision needs an ADR; no format choice is approved yet | Avoids committing to an unapproved symbol-format change |
| If KDBG change is deferred | Portable prefixes + DAP `sourceMap` | Works, but debugger must guess less robustly |
| Build layout | `host/` + `stage/` per preset | Mirrors install; no source-tree output |
| KR16 runtime | Build once, no FP, generated profile facts | Prevents host/32-bit assumptions leaking |
| KR64 | Config only until backend exists | Avoid a fictional runtime target |
