# KR build instructions

How to build, test and install the KR family from a fresh session: the compiler
(`krcompiler`), the four VM trees (`kr32`, `kr16`, `kr232`, `kr216`), and the
debugger (`krdbg`), on Windows x64, Linux x64 under WSL, and Linux ARM64.

This is an operating guide. Policy — when a build tier is required, what a gate
must record, and the definition of done — lives in each repository's `AGENTS.md`
and `AGENTS.local.md`, which win wherever this document and they disagree.

**Basis.** Every command below is taken from the scripts and presets named beside
it, not from memory, and the sequence was exercised end to end: a clean family gate
(19 of 20 rows, the one exception being `kr216`'s density row) and a clean ARM64
build on the Raspberry Pi both ran from these instructions. Where the plan and the
trees differ, this document follows the trees and says so.

## 1. Directory layout this assumes

All six checkouts sit beside each other under one **family root**, with exactly
these directory names:

```text
<family>/
  krcompiler/     the compiler, linker, sysroot, and the mirror/gate scripts
  kr32/           VM target 1
  kr16/           VM target 2
  kr232/          VM target 3, and scripts/check-family.ps1 (the family gate)
  kr216/          VM target 4
  krdbg/          the debugger
```

The names are load-bearing, not decorative:

- Every VM tree's `cmake/KrGuest.cmake` looks for the compiler at
  `../krcompiler`, and `krdbg` looks for the VMs at `../kr32`, `../kr16`,
  `../kr216`, `../kr232`. One spelling to know: `krdbg`'s default for the kr32
  tree is `../KR32` (upper case), which only resolves on Windows. Enabling that
  adapter on Linux needs `-DKRDBG_KR32_ROOT=../kr32`.
- `kr232/scripts/check-family.ps1` derives the family root as the parent of
  `kr232`, and then addresses every sibling by name.
- `krcompiler`'s executed tests default to the sibling build trees for their VM
  binaries (Windows only — see §6.4).

A checkout missing from that layout still builds on its own in most cases, but
the family gate and the executed rows will not work.

## 2. Tool inventory

### 2.1 Tools the build needs

| Tool | Needed by | Floor / resolved version | How it is found |
|---|---|---|---|
| PowerShell 7 (`pwsh`) | every Windows gate and script | 7.x (7.7.0 on the dev machine) | `pwsh --version` |
| MSVC `cl.exe` + Windows SDK | all Windows builds | VS 2026, MSVC 19.5x | `vswhere`, then `vcvarsall x64`; `pwsh -File .agents/doctor.msvc.ps1` |
| CMake | all | 3.25 (`cmake_minimum_required`) | `cmake --version`; Visual Studio ships one |
| Ninja | Linux presets; `krdbg` and `kr232` on Windows | any recent | `ninja --version`; Visual Studio ships one |
| GCC (`g++`) | `linux-gcc-*` | GCC 15.2 on the dev WSL image | `g++ --version` |
| Clang (`clang++`) | `linux-clang-debug`, `linux-sanitize` | Clang 21.1.8 on the dev WSL image | `clang++ --version` |
| Clang sanitizer runtimes | `linux-sanitize` | `libclang-rt-21-dev` (a separate package from `clang`) | a sanitize build links |
| `rsync` | the WSL mirror | any | `rsync --version` |
| `git` | build provenance (`krc --version` revision; reports `unknown` without a checkout) and `check-arm64.ps1` pulls | any | `git --version` |
| `node` | `kr232` spec lint (`tools/check-isa.mjs`) — optional; the gate names the skip when it is absent | any | `node --version` |
| SDL2 development files | `krdbg` and the `kr32` UI on **Linux** (`libsdl2-dev`, `pkg-config`) | system package | `pkg-config --modversion sdl2` |
| WSL 2 + Ubuntu | the Linux x64 host | Ubuntu 26.04 on the dev machine | `wsl -l -v` |
| SSH + an ARM64 host alias | the ARM64 gate | `bitwhiz` is the configured alias | `ssh bitwhiz uname -m` |

Windows uses the Win32 presenter and needs no SDL2 DLL; `krdbg`'s gate fails if it
finds one in the tree. On Linux both `krdbg` and `kr32` require SDL2.

### 2.2 Tools the build produces

| Binary | Repository | Where it lands |
|---|---|---|
| `krc` | krcompiler | `build/<preset>/host/bin/<Config>/` |
| `krld` | krcompiler | same |
| `krc_tests` | krcompiler | same — a **binary**, not a ctest row |
| `krc_extract_fixtures`, `krc_gen_sysroot_bits` | krcompiler | same, build-time only, not installed |
| `kr32` | kr32 | `build/windows-msvc/Debug/` |
| `kr16` | kr16 | `build/windows-ninja/` |
| `kr232`, `kr232-dis` | kr232 | `build/windows-msvc/` |
| `kr216`, `kr216-dis`, `kr216-bench` | kr216 | `build/windows-msvc-debug/tools/<tool>/Debug/` |
| `krdbg_demo`, `krdbg_settings` | krdbg | `build/windows-msvc/` |

`scripts/install-tools.ps1` holds the same table for both configurations and
copies all of them into one directory (§7.4).

## 3. Verify the machine before building

### 3.1 Windows

```powershell
pwsh -File .agents/doctor.msvc.ps1                    # machine: MSVC, SDK, CMake
pwsh -File scripts/doctor.ps1 -Repo krcompiler         # repo: cmake, git, node, cl
```

`.agents/doctor.msvc.ps1` exits 2 and names each missing component; it is
read-only by design and never installs anything. `scripts/doctor.ps1` is the J5
check every gate calls first; it prints each tool's version and path, and exits 1
naming what is missing. Add `-Need krc,krld` when a tree builds guest code.

Two caveats from `AGENTS.local.md`:

- `cmake` and `ctest` are **not on `PATH`** on the current dev machine; the Visual
  Studio copy is. The gates hardcode
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  and fall back to the bare names. Interactive `cmake` therefore needs a
  **Developer PowerShell for VS** — or use the gate scripts, which find it.
- In a confined sandbox the MSVC doctor cannot capture `vswhere.exe`'s output and
  reports MSVC and CMake missing on a machine that builds. Treat that failure as
  inconclusive and fall back to the configure step, which either finds the
  compiler or does not.

### 3.2 Linux (WSL or ARM64)

```sh
sh scripts/doctor.sh krcompiler            # cmake, ninja, cc, rsync, node
sh scripts/doctor.sh kr232 --need=krc,krld # when guest code will be built
```

The Linux doctor checks `cmake`, `ninja`, a C compiler (`cc`/`gcc`/`cl`), `rsync`,
and optionally `node`. A missing Ninja is a provisioning gap, not a broken preset.

## 4. Build order and dependencies

```text
krcompiler  ── builds krc, krld, krc_tests
     │
     ├── install into a prefix ──► the CMake package (krc::toolchain, krc::debug)
     │                                    │
     │                                    ▼
     ├── kr32, kr16, kr232, kr216 ── guest artefacts and VM binaries
     │                                    │
     ▼                                    ▼
krdbg  ── links the compiler package AND the four VM source trees
```

The rule behind the picture is D6 in `docs/build-cleanup-plan.md`: the compiler
never links a VM, a VM never builds guest artefacts by invoking itself, and the
debugger never compiles a VM's sources.

What each tree consumes:

| Repository | Needs | Notes |
|---|---|---|
| krcompiler | a host C++ compiler and CMake | no sibling required |
| kr32 | `find_package(krc-toolchain CONFIG REQUIRED)` + `krc`/`krld` | needs an installed prefix on `CMAKE_PREFIX_PATH` |
| kr16 | the installed package — `find_package(krc-toolchain)` and `find_package(krc-debug)`, required in both toolchain modes | default mode is `builtin`; `-DKRG_TOOLCHAIN=krc` uses the compiler |
| kr232 | `krc`/`krld` via `cmake/KrGuest.cmake` | resolves the sibling compiler build tree or `PATH` without an install |
| kr216 | `krc`/`krld` via `cmake/KrGuest.cmake` | guest tests are **off** unless `-DKR216_BUILD_GUEST_TESTS=ON` |
| krdbg | `find_package(krc-debug CONFIG REQUIRED)` and `find_package(krc-toolchain CONFIG REQUIRED)` — plus the four VM source trees named by `KRDBG_KR*_ROOT`, defaulting to the sibling checkouts | cannot configure without an installed compiler prefix |

**Two prefixes, not one.** A Release consumer linking a Debug package fails on
`__imp__CrtDbgReport`, so a Debug tree is built against a Debug prefix and a
Release tree against a Release prefix. The family gate does exactly this (§5).

`cmake/KrGuest.cmake` resolves `krc`/`krld` in a fixed order, and an explicit path
that does not exist is a fatal error rather than a hint:

1. `KRG_KRC` / `KRG_KRLD` (and each repository's legacy spelling)
2. the sibling `krcompiler`'s build trees, newest first
3. `krc` / `krld` on `PATH`
4. otherwise the configure stops with one sentence listing everything it tried

## 5. Presets: the names that actually exist

The plan's target is one vocabulary, `<host>-<config>`, in every tree. The trees
have **not** converged on it, so use this table rather than assuming a name:

| Repository | Debug preset | Release preset | Debug build tree |
|---|---|---|---|
| krcompiler | `windows-msvc-debug` / `linux-gcc-debug` | `windows-msvc-release` / `linux-gcc-release` | `build/windows-msvc-debug` |
| kr32 | `windows-msvc-debug` / `linux-gcc-debug` | `windows-msvc-release` / `linux-gcc-release` | `build/windows-msvc` |
| kr16 | `windows-ninja-debug` (gate default) or `windows-msvc-debug` / `linux-gcc-debug` | `windows-ninja-release`, `windows-msvc-release`, `linux-gcc-release` | `build/windows-ninja` |
| kr232 | `windows-msvc-debug` / `linux-gcc-debug` | `windows-msvc-release` / `linux-gcc-release` | `build/windows-msvc` |
| kr216 | `windows-msvc-debug` / `linux-gcc-debug` | `windows-msvc-release` / `linux-gcc-release` | `build/windows-msvc-debug` |
| krdbg | `windows-msvc` / `linux-wsl` / `linux-arm64` | `windows-msvc-release`, `linux-wsl-release`, `linux-arm64-release` | `build/windows-msvc` |

Notes that cost time when missed:

- **kr32's** `windows-msvc-debug` writes to `build/windows-msvc` (no `-debug`
  suffix on the directory). Its `AGENTS.local.md` still says `--preset
  windows-msvc`; no such configure preset exists.
- **kr16** has both a Visual Studio pair and a Ninja pair; its gate defaults to
  `windows-ninja-debug` and asserts 62 rows there.
- **kr232's** `windows-msvc-debug` uses the **Ninja** generator, not Visual
  Studio. `krdbg`'s Windows presets are Ninja too.
- **krdbg** is the outlier: `windows-msvc` and `linux-wsl`, not
  `windows-msvc-debug`/`linux-gcc-debug`.
- All Linux presets use Ninja, so `ninja` must exist in the Linux environment.
- Parallelism follows the **processor count**, in the gates rather than the
  presets: no preset carries a `jobs` value except `krcompiler`'s test presets,
  which pin 1 (§8). The gates pass the count to both the build and `ctest`.
  Two exceptions: the x86_64 Linux rows stay at **eight jobs** because GCC 15 in
  this WSL image intermittently segfaults while garbage-collecting (raised from
  two on 2026-11-26, watching for the ICE to return), and `kr32`'s sanitizer row
  caps at four because of its 600 s per-test watchdog.

## 6. Building

### 6.1 The whole family in one command

From a **Developer PowerShell**, in `kr232`:

```powershell
cd <family>/kr232
pwsh -File scripts/check-family.ps1
```

That is the family gate (`kr232/scripts/check-family.ps1`). It runs, from
`scripts/family-worker.ps1` and `scripts/family-linux.ps1`:

1. build `krcompiler` Debug, install it into `<family>/build/family-prefix`;
2. every consumer Debug in parallel, each handed the prefix and toolchain paths in
   the form it reads: `-DCMAKE_PREFIX_PATH` plus both toolchain spellings
   (`-DKRG_KRC`/`-DKRC_EXECUTABLE`, ditto for `krld`) for a preset row, and
   `-Krc`/`-Krld` for a repository that has its own gate;
3. `krcompiler`'s own suite with `-Fresh`, now that the VM binaries exist, with
   its row count asserted;
4. the same for Release, into `<family>/build/family-prefix-release`;
5. the Linux x64 wave in WSL, which starts before the Windows waves and runs
   underneath them.

Useful flags: `-Only kr32,krdbg`, `-Serial` (one repository at a time, for reading
a failure), `-SkipTests` (build only), `-Jobs <n>`, `-Prefix <dir>`, `-Arm64`
(delegates to `check-arm64.ps1`, §10). It prints one summary line per row and one
exit code.

Run it from a Developer shell: it calls bare `cmake`/`ctest`, unlike the
per-repository gates.

**Every test image is build output.** A clean tree needs no checked-in binaries:

| Repository | Generated at build time | Target |
|---|---|---|
| kr16 | the two golden containers | `<build>/golden`, from `tests/golden/generate.cpp` |
| kr32 | the KR32OS images | `<build>/os-images`, from `kr32os/build.ps1` |
| kr232 | the fixture images and the kernel | `<build>/fixtures` and `<build>/kernel.kr232`, from `cmake/BuildKr232Fixtures.cmake` |
| krcompiler | its two kr232 containers | `<build>/<preset>/kr232-images`, from `kr232-fixture.s` and `kr232-draw.s` |

The kr232 and compiler images need `krc` and `krld`, so a configure without the
toolchain registers only the rows it can build: kr232's plain suite has 2 rows, its
kernel tree 13.

**What a passing gate looks like.** 20 rows is a full pass. One row is expected
red today: `kr216`'s Debug preset, whose `density_benchmark` measures bytes per
instruction against the plan's 2.5 target. It reads 2.57 at the default `-O1`
(2.53 with the branch-relaxation overhead removed; 2.47 at `-O0`). It is a codegen
gap, not a build failure, so a clean run currently ends `19 of 20`; every other
row must be green.

### 6.2 The compiler alone

```powershell
pwsh -File .agents/doctor.msvc.ps1
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
# krc_tests is not a ctest row:
build\windows-msvc-debug\host\bin\Debug\krc_tests.exe
```

Or the gate, which does all of the above plus the staged-sysroot check:

```powershell
pwsh -File scripts/gate-windows.ps1            # -Preset <name>, -SkipTests, -Fresh
```

The staged-sysroot check reads `build/windows-msvc-debug/stage/sysroot` even when
`-Preset` names another row, so the debug tree has to have been configured once.

Install it into a prefix before `kr32`, `kr16` or `krdbg` configures (all three
call `find_package`, and a missing package is a configure error):

```powershell
cmake --install build\windows-msvc-debug --config Debug --prefix C:\krc-debug
```

A multi-config generator needs `--config`; a Ninja tree does not. The one-command
bootstrap does configure, build, install and a `--version` check:

```powershell
pwsh -File scripts\bootstrap.ps1 -Prefix C:\krc-debug          # -Config Debug|Release
```

On Linux: `sh scripts/bootstrap.sh --prefix /opt/krc [--config Debug]`.

### 6.3 Each consumer, manually

Debug, against the Debug prefix:

```powershell
# kr32
cd <family>\kr32
cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH=C:\krc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# kr16 (builtin toolchain is the default; add -DKRG_TOOLCHAIN=krc for the compiler path)
cd <family>\kr16
cmake --preset windows-ninja-debug -DKR16_KRC_PREFIX=C:\krc-debug
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug

# kr232 (finds krc/krld through cmake/KrGuest.cmake; pass them explicitly to be
# sure. Without them the kernel and fixture rows are not registered, because both
# are built from source by the toolchain.)
cd <family>\kr232
cmake --preset windows-msvc-debug -DKR232_BUILD_KERNEL=ON `
    -DKR232_KRC=C:\krc-debug\bin\krc.exe -DKR232_KRLD=C:\krc-debug\bin\krld.exe `
    -DCMAKE_PREFIX_PATH=C:\krc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# kr216 (guest rows are opt-in; krc/krld resolve the same way)
cd <family>\kr216
cmake --preset windows-msvc-debug -DKR216_BUILD_GUEST_TESTS=ON -DKRG_KRC=C:\krc-debug\bin\krc.exe -DKRG_KRLD=C:\krc-debug\bin\krld.exe
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
# or simply: build.cmd [preset]

# krdbg (needs the installed package and the four sibling VM trees)
cd <family>\krdbg
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:\krc-debug
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

Prefer the per-repository gates where one exists — they configure their own tree,
assert row counts, and run extra checks the bare preset does not:

| Repository | Gate | Notes |
|---|---|---|
| krcompiler | `scripts/gate-windows.ps1` | doctor, all presets, `krc_tests`, staged sysroot |
| kr32 | `scripts/run-gate.ps1` | `-Preset`, `-Krc`, `-Krld`, `-SkipTests` |
| kr16 | `scripts/gate-windows.ps1` | default preset `windows-ninja-debug`, asserts 62 rows |
| kr232 | `scripts/gate-windows.ps1` | `-Krc`, `-Krld`, `-Arm64`; runs the spec lint first (a named skip without `node`), then a plain tree (2 rows) and a kernel tree (13 rows, asserted) |
| krdbg | `scripts/gate-windows.ps1` | resolves MSVC itself via `vcvarsall` |

### 6.4 Give the compiler VM binaries for its executed rows

`krcompiler`'s suite runs linked images under a real VM and checks exit status and
output. Those rows are only registered when the VM binaries are found at
**configure** time, and the durable location is `krcompiler/vm/` — deliberately
outside `build/`, which a clean gate deletes:

```powershell
cd <family>\krcompiler
New-Item -ItemType Directory vm -Force
Copy-Item ..\kr32\build\windows-msvc\Debug\kr32.exe  vm\krc_kr32.exe
Copy-Item ..\kr16\build\windows-ninja\kr16.exe       vm\krc_kr16.exe
Copy-Item ..\kr232\build\windows-msvc\kr232.exe      vm\kr232.exe
cmake --preset windows-msvc-debug --fresh
```

The cache variables are `KRC_KR32_VM`, `KRC_KR16_VM`, `KRC_KR232_VM`. Resolution
order is `vm/`, then the legacy `build/vm/`, then — Windows only — the sibling
checkout's build tree (`../kr32/build/windows-msvc/Debug/kr32.exe`,
`../kr16/build/windows-ninja/kr16.exe`,
`../kr232/build/windows-msvc/kr232.exe`). **On Linux none of the sibling
fallbacks apply**, so a Linux host with no copies in `vm/` silently registers no
executed rows and still passes. Copy the binaries in before concluding a Linux
row is green.

## 7. Where the binaries and installed files go

### 7.1 Build trees

Everything a build produces stays inside that repository's `build/`. In
`krcompiler`:

```text
build/<preset>/host/bin/<Config>/   krc, krld, krc_tests
build/<preset>/stage/               staged target files: sysroot, linked test images
build/<preset>/kr232-images/        the two kr232 containers the dump and run rows read
```

In a VM tree or `krdbg`, the executables sit in the preset's own binary directory
(for example `build/windows-msvc/Debug/kr32.exe`, `build/windows-ninja/kr16.exe`,
`build/windows-msvc/krdbg_demo.exe`). Test images the tree builds for itself live
in the same `build/` directory and are never committed:

```text
kr16:   build/golden/                 exec_min.kr16, rel_min.o
kr32:   build/os-images/              bios.kr32, kernel.kr32 and the initrd
kr232:  build/fixtures/               the user and gate programs, halt.bin, kernel-draw.bin
        build/kernel.kr232            the kernel the user programs run under
```

Each has a source beside it: `tests/golden/generate.cpp`, `kr32os/build.ps1`,
`cmake/BuildKr232Fixtures.cmake` (with `tests/fixtures/halt.s` and
`kernel-draw.s`), and `tests/fixtures/kr232-fixture.s`. Delete the build directory
and the build recreates all of them.

### 7.2 An installed compiler prefix

```text
<prefix>/
  bin/krc.exe                 bin/krld.exe
  include/krc/...             the public headers
  lib/                        krc-toolchain and krc-debug archives + import libs
  lib/cmake/krc/              krc-targets.cmake, krc-targets-<config>.cmake, krc-stamp.cmake
  lib/cmake/krc-toolchain/    krc::toolchain
  lib/cmake/krc-debug/        krc::debug
  share/krc/sysroot/          target headers, libkrcrt.kro, kr32 crt0/fileio/eh
```

Only `krc` and `krld` are installed as executables; the internal tools are not.
The prefix describes itself in `lib/cmake/krc/krc-stamp.cmake` and through
`krc --version` (version, build configuration, revision, compiler).

### 7.3 Prefixes the family gate creates

| Prefix | Contents |
|---|---|
| `<family>/build/family-prefix` | Debug compiler, used by the Debug consumer wave |
| `<family>/build/family-prefix-release` | Release compiler, used by the Release wave |
| `~/krc-debug` (WSL) | Debug compiler installed by the Linux wave |
| `~/.local` (WSL) | the same, so `krc` is on the default WSL `PATH` |
| `~/krc-debug` (ARM64 host) | the prefix `check-arm64.ps1` exports as `CMAKE_PREFIX_PATH` |

The current dev machine also has `C:\Users\kevin\krc` (Release) and
`C:\Users\kevin\krc-debug` (Debug). Those are hand-made, not created by any
script; a new session should not assume them.

### 7.4 One directory on `PATH`

```powershell
pwsh -File scripts\install-tools.ps1 [-Dest <dir>] [-Config Debug|Release] [-AllowMissing]
```

Copies every family binary (`krc`, `krld`, four VM emulators, two disassemblers,
the kr216 bench, two krdbg programs) and the `SDL2.dll` that `kr32.exe` loads into
`$HOME/.local/bin` by default, reports what it could not find, and prints the
`PATH` line if the destination is not on it. It exits non-zero on a missing
binary, or on a `kr32.exe` copied without its DLL, unless `-AllowMissing`.

### 7.5 A release archive

```powershell
pwsh -File scripts\package.ps1 -Version 0.2.0 -Configuration windows-msvc-release
# writes dist\krc-0.2.0-windows-x64.zip and dist\SHA256SUMS
pwsh -File scripts\verify-release.ps1    # packages twice, compares, unpacks, compiles, links
```

The archive is deterministic (sorted entries, ZIP epoch), so two packages of one
tree are byte-identical. `verify-release.ps1` is the check that a release has ever
been opened.

VM trees install their own pieces only if asked: `kr32` installs `kr32.exe` to
`bin/` and `kr32-core` with headers; `kr16`, `kr232` and `kr216` install only
their core library and headers; `krdbg` has no install rules at all. The family
builds against source trees, so no VM install step is required.

## 8. Running the tests

Per preset, the standard trio:

```powershell
ctest --preset <preset>            # add --output-on-failure
ctest --preset <preset> -N         # list, and count rows
```

- `krc_tests` is a **binary**, not a ctest row; the gates run it separately.
- Row counts are asserted by the gates, because a suite that registers fewer rows
  still reports success. Current expectations from `check-family.ps1`:

| Repository | Preset in the family gate | Rows asserted |
|---|---|---|
| krcompiler | `windows-msvc-debug` / `-release` | 1012 each, plus `krc_tests` |
| kr32 | `windows-msvc-debug` | 43 |
| kr16 | `windows-ninja-debug` | 62 |
| kr216 | `windows-msvc-debug` + `-DKR216_BUILD_GUEST_TESTS=ON` | 58 Debug, 11 Release (guest rows are not enabled in the release wave) |
| kr232 | `windows-msvc-debug` with the toolchain | 13 in `build/gate-kernel`, asserted by the gate; the compiler-less plain tree has 2 |
| krdbg | `windows-msvc` | 3 |

If a count moves, the gate fails on purpose. Confirm the new suite is correct
before changing the number.

One row is expected red and is not a row count: `kr216`'s Debug preset runs
`density_benchmark`, which measures 2.57 bytes per instruction against the plan's
2.5 target. It is a codegen gap recorded in that repository's plan, not a build
failure; every other row must be green.

- Run **`krcompiler`'s** suite serially. Its test presets pin `"jobs": 1`, and
  with `"jobs": 4` the executed-image tests failed nondeterministically: four
  failures across five parallel runs, a different test each time, no diagnostic,
  and no failure in any serial run of the same build.
- Do **not** run two rows' test suites at once. Two `ctest` processes overlap on
  the same `vm/krc_kr32.exe`, and a single one-in-889 failure appears that passes
  on re-run. Build rows concurrently if you like; test rows one at a time.

## 9. Linux x64 under WSL

### 9.1 Never build on the Windows mount

A WSL build under `/mnt/c` carries host paths in its debug data and its
timestamps are too coarse for incremental work. Build from an ext4 mirror at
`~/mirrors/<repo>` instead:

```sh
sh ../krcompiler/scripts/kr-mirror.sh krcompiler
cd ~/mirrors/krcompiler && bash scripts/gate-linux.sh
```

The mirror tool excludes `build/`, `.git/` and `*.tmp`, plus anything listed in
the repository's `.kr-mirror-exclude`. There is no default exclusion for
`third_party/`: the vendored trees there (doctest, stb, kdbg's reader) are build
inputs, and an excluded path is also never deleted, so a default exclusion left
each mirror with whatever copy it had and a fresh mirror without it. Excluding
`.git` is safe for the build: `krcompiler` asks `git rev-parse` for the revision
and falls back to `unknown` when there is no checkout — so a mirror build's
`krc --version` names no commit, which is worth knowing when comparing two green
results. With no `--source`, the tool expects exactly one
`/mnt/c/Users/*/repos/<repo>` and refuses to guess between several. It takes a
per-mirror `flock`, so two agents either wait (`--wait <seconds>`) or stop with a
sentence naming the holder's pid — never a half-written tree.

Because `build/` is excluded, a refresh **keeps the mirror's build trees**: that is
where stale caches, half-configured directories and objects from an old layout
survive. When the tree has moved underneath them — presets renamed, fixtures
turned from checked-in files into build output, a compiler change that alters every
consumer's object — reset the mirrors instead of refreshing them:

```sh
rm -rf ~/mirrors/*
for repo in krcompiler kr32 kr16 kr232 kr216 krdbg; do
    sh /mnt/c/Users/kevin/repos/KRCompiler/scripts/kr-mirror.sh "$repo"
done
```

A fresh mirror is about 80 MB; the six with their build trees were 4.4 GB. Check
that `~/mirrors/krcompiler/third_party/doctest/doctest.h` arrived before building.

A gate started from a `/mnt/c` path does this itself: it prints the mirror it
used and `exec`s itself there. `--in-place` opts out for the rare intentional
mount build.

### 9.2 The Linux gate

```sh
bash scripts/gate-linux.sh                        # linux-gcc-debug + linux-gcc-release
bash scripts/gate-linux.sh --sanitize             # adds linux-sanitize
bash scripts/gate-linux.sh --preset linux-clang-debug   # one named row
```

The staged-sysroot check always reads `build/linux-gcc-debug/stage/sysroot`, so
run the default gate (or the gcc-debug row) at least once before a
`--preset`-only run, or that check fails for a tree that was never configured.

It runs the doctor, both (or the named) presets, `krc_tests`, and the
staged-sysroot check, then prints `GATE LINUX: PASS`. `linux-clang-debug` is
skipped with a printed line when `clang++` is absent; every skip is named.

The family gate's Linux wave runs all six trees from `~/mirrors`, installing the
compiler into `~/krc-debug` and `~/.local` before the consumer rows
(`kr232/scripts/family-linux.ps1`). The consumer gates then find `krc` and `krld`
on `PATH` (or in `KR232_KRC`/`KR232_KRLD`). Two of them need the toolchain
explicitly: kr232's gate builds the kernel and every fixture from source, and
krdbg's two-adapter step builds the sibling `kr232` as a subproject, whose
fixtures are guest code. Both stop with a sentence naming the missing tool rather
than failing on a file. kr232's spec lint needs `node`, which this image does not
install; the gate prints a named skip instead of failing on the interpreter.
One machine-specific detail: that script hardcodes
`/mnt/c/Users/kevin/repos/krcompiler/scripts/kr-mirror.sh`; on a different Windows
user, edit that line or set the path the gates read (`KRC_MIRROR_TOOL`).

### 9.3 The consumers on Linux

Same order as Windows (§4), against a compiler prefix installed on the Linux
side (`~/krc-debug`, as `family-linux.ps1` and `check-arm64.ps1` do):

```sh
# kr32
cmake --preset linux-gcc-debug -DCMAKE_PREFIX_PATH=$HOME/krc-debug
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug

# kr16
cmake --preset linux-gcc-debug -DKR16_KRC_PREFIX=$HOME/krc-debug
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug

# kr232 (like Windows: the kernel and the fixtures are built from source)
cmake --preset linux-gcc-debug -DKR232_BUILD_KERNEL=ON \
    -DKR232_KRC=$HOME/krc-debug/bin/krc -DKR232_KRLD=$HOME/krc-debug/bin/krld
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug

# kr216 (guest rows are opt-in)
cmake --preset linux-gcc-debug -DKR216_BUILD_GUEST_TESTS=ON \
    -DKRG_KRC=$HOME/krc-debug/bin/krc -DKRG_KRLD=$HOME/krc-debug/bin/krld
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug

# krdbg (its presets are linux-wsl, not linux-gcc-debug; SDL2 required)
cmake --preset linux-wsl -DCMAKE_PREFIX_PATH=$HOME/krc-debug
cmake --build --preset linux-wsl && ctest --preset linux-wsl
```

Gates exist for kr32 (`scripts/run-gate.sh`), kr232 (`scripts/gate-linux.sh`),
and krdbg (`scripts/gate-linux.sh`); kr16 and kr216 have no Linux gate script and
are built and tested by their preset. The kr232 and krdbg gates export the family
job share and build the guest fixtures, so they need `krc` and `krld` on `PATH`
(the family Linux wave installs them into `~/.local/bin` first).

### 9.4 WSL cost and stability

- Run the Linux presets one at a time on the dev machine. GCC 15 in this image
  intermittently segfaults while garbage-collecting, surfacing as
  `internal compiler error: Segmentation fault` inside standard headers — build
  with `ulimit -s unlimited` and at most `-j8`, and rebuild once before calling a
  build failed.
- With four presets running at once, `ctest` can read the staged `libkrcrt.kro`
  while another job rebuilds it, producing `krld: KRO CRC mismatch` in one link
  test that passes alone. The build policy's concurrency assumes headroom this
  machine does not have.

## 10. Linux ARM64

ARM64 builds are native on the ARM64 host; nothing is cross-compiled from
Windows.

### 10.1 From Windows, over SSH

```powershell
pwsh -File <family>\kr232\scripts\check-arm64.ps1 [-Target bitwhiz] [-Root ~/repos]
```

It copies a script to the host, fast-forward-pulls `kr232`, `krcompiler`, `krdbg`,
`kr216`, and `kr32`, configures and builds the compiler with
`linux-clang-debug`, installs it to `~/krc-debug` and puts that prefix on `PATH`,
builds `kr232` with the kernel rows, builds `kr32`, points the compiler's executed
rows at those binaries, then runs kr232's rows (13, asserted), the compiler's whole
suite (1012), `krc_tests`, krdbg's ARM64 gate, and the krdbg fixture drift row. It
ends with `ARM64 CHECKS: PASS` or throws. It requires all five checkouts to exist
under `-Root` and the SSH alias to be configured.

For a **clean** ARM64 run, delete the build trees on the host first — the script
configures with `--fresh` for the consumers but reuses the compiler's directory, so
only deleting makes it a from-scratch build:

```powershell
ssh bitwhiz 'for r in krcompiler kr232 kr32 krdbg kr216 kr16; do rm -rf ~/repos/$r/build; done'
pwsh -File <family>\kr232\scripts\check-arm64.ps1
```

### 10.2 On the ARM64 host

```sh
# in krdbg/
bash scripts/gate-linux-arm64.sh          # linux-arm64 + linux-arm64-release
bash scripts/gate-linux.sh --arch arm64   # same gate, explicit
# in kr32/, kr16/, kr232/ or kr216/
cmake --preset linux-arm64-release
```

The ARM64 gate records frame hashes separately (`goldens/*.arm64.hash`, written to
`build/gate/arm64-hashes.txt`) because rasterization may differ by architecture.
`gate-linux-arm64.sh` refuses to run on a non-ARM64 machine.

For the compiler on the Pi, follow `docs/arm64-tester.md`: build `master` with
`linux-gcc-debug` when the pushed change is more than documentation, and append
one `docs/arm64-test-log.md` entry per tested SHA. That log is where the ARM64
host's latest tested revision lives.

## 11. Clean builds

Per repository:

```powershell
cmake --preset <preset> --fresh     # discard the cache, keep the directory
# or the blunt form: remove <repo>\build\<preset>
```

The compiler gate exposes the same thing as `-Fresh`. Use it whenever sibling
binaries or a prefix changed, because the executed rows and toolchain paths are
resolved at configure time and a stale cache silently registers fewer rows.

Whole family:

1. Delete every repository's `build/` directory.
2. Delete `<family>/build/family-prefix*`.
3. In WSL, delete `~/mirrors/*` and the `~/krc-debug` and `~/.local` compiler
   prefixes, then recreate the mirrors (§9.1). A *refresh* will not do it: the
   mirror tool excludes `build/` from deletion, so the mirror's stale build trees
   survive. `rm -rf ~/mirrors/*` is the only thing that removes them.
4. Re-run `check-family.ps1`.

Two things to preserve first:

- **`vm/`** holds the VM copies the compiler's executed rows need. A gate deletes
  `build/`, so copies kept in `build/vm/` disappear and the rows silently vanish
  from the count. Copy them to `vm/` before deleting a build tree.
- **The compiler trees' `stage/` must not be deleted on its own.**
  `build/<preset>/stage/tests/` is not recreated by anything but a full configure;
  removing `stage` alone makes every link test fail with
  `cannot open object output ...\stage\tests\...kro` — and 740 rows fail at once.
  Delete the whole build directory, then configure.

Nothing else needs preserving: every test image (kr16's goldens, kr32's OS images,
kr232's fixtures and kernel, the compiler's two kr232 containers) is generated by
the build from a source beside it, so a deleted `build/` recreates it.

A clean-build pass is expected to end with every family row passing; the gate
builds what it tests and asserts counts so that a smaller suite cannot read as
green. Today the single known exception is `kr216`'s `density_benchmark` (§8).

## 12. Known traps, in the order they bite

1. **`cmake` is not on `PATH`.** Use a Developer PowerShell, or a gate script.
2. **A Debug consumer pointed at a Release prefix** (or the reverse) fails on
   `__imp__CrtDbgReport`. Match the prefix to the configuration.
3. **`kr32`, `kr16` and `krdbg` will not configure** without an installed
   compiler package: all three call `find_package` with `REQUIRED`.
4. **`kr16` builds its own in-tree compiler by default** (`KRG_TOOLCHAIN=builtin`).
   Pass `-DKRG_TOOLCHAIN=krc` to exercise the real compiler.
5. **`kr216`'s guest rows are off by default**; without
   `-DKR216_BUILD_GUEST_TESTS=ON` its suite is 11 rows, not 56.
6. **`kr232`'s gate needs `krc`/`krld`** for the kernel and the fixtures, and
   `node` for the spec lint. Without the tools it stops with a sentence naming
   them; without `node` it prints a named skip and continues. A configure without
   the toolchain registers only the plain suite (2 rows) — the 13 kernel rows
   exist only where the images can be built.
7. **A Linux suite with no VM copies in `vm/` is green with fewer rows.** Count
   rows (`ctest -N`) rather than trusting "100% tests passed".
8. **Two test suites at once** produce a one-row flake. Serialise rows.
9. **GCC 15 ICEs** in this WSL image; `ulimit -s unlimited`, `-j8`, rebuild once.
10. **The mirrors keep stale build trees.** `build/` is excluded from the mirror's
    deletion, so a refresh preserves whatever half-configured directory the last
    run left. Delete `~/mirrors/*` when the tree has moved underneath it (§9.1).
11. **A fixture recorded with an absolute source path is reproducible only on that
    machine.** Images embed the source path in their debug data; build them from a
    relative path against a named root (kr232's `BuildKr232Fixtures.cmake` and
    krdbg's fixture check both do), or the ARM64 host will fail a tree the x64 host
    calls clean.
12. **Do not edit a committed listing, fixture or image to make a row pass** —
    the drift rows exist to catch exactly that.

## 13. What this document does not cover

- Policy and required tiers per change: `AGENTS.md` and `AGENTS.local.md` in each
  repository.
- Instruction encodings, the guest ABI, and debugger behaviour: the ISA drafts,
  `docs/abi-*.md`, and `krdbg`'s own docs.
- The reasoning behind the build structure: `docs/build-cleanup-plan.md`.
- Milestone state and the record of past gates: `implementation.md`.

Update this document in the same change that alters a preset name, a gate flag, a
prefix, or an install location, because a stale build instruction is believed.
