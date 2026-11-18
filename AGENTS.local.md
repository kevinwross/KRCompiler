# AGENTS.local.md

Loaded immediately after `AGENTS.md`, and never overwritten by `DevTemplate` sync. This file is the repository's own layer: everything that is true of *this* project and not of every project.

Add entries here rather than editing the managed blocks in `AGENTS.md`; an edit inside a managed block is replaced on the next `--update` and will be lost.

**Keep this file current.** When a condition named here changes — a plan leaves DRAFT, a missing directory is created, a trap stops reproducing — update or delete the entry in the same change. A stale instruction is worse than a missing one: it is believed.

## Build policy

Three tiers. Run the smallest one the change needs; never run a larger one "to be safe".

**1. Inner loop — per edit and per local commit.** One build: the preset for **the operating system you are working in**, not the other one. On a Windows host that is `windows-msvc-debug`; in WSL or on Linux, `linux-gcc-debug`. **The WSL presets are the Linux row, not the Windows inner loop** — building them while working on Windows means MSVC compiles nothing until the gate, and a `/W4 /WX` failure surfaces commits later, on a change that may not be the cause. While iterating, do not configure or build a release preset, a second compiler, or another host. **A local commit is not a milestone and does not earn a matrix.**

Use the preset form: `cmake --build --preset <preset>`, then `ctest --preset <preset>`. A hand-rolled `cmake -S . -B <dir>` works only because that directory already holds the preset's cache; on a fresh one it silently produces an untyped build with whatever compiler is first on `PATH`.

**A change confined to documentation needs no build.** `implementation.md`, `docs/` and `README`-type edits are verified by reading. A change that alters code, a test or a fixture takes the inner loop.

**2. Boundary change — the matrix for this host.** Run the matrix when the change crosses a boundary that can break another configuration: `include/krc/`, `src/ir/`, the target descriptor, the object or symbol format, the ABI, `CMakeLists.txt`, or `CMakePresets.json` — the same categories that require an ADR. Run it **before committing** the change, and before starting further work in that area; a boundary change committed without the matrix is outside the policy. Build and test every configuration this host can reach:

| Host | Configurations |
|---|---|
| Windows | `windows-msvc-debug`, `windows-msvc-release`, plus `linux-gcc-debug`, `linux-gcc-release`, `linux-clang-debug`, `linux-sanitize` in WSL |
| Linux x64 | `linux-gcc-debug`, `linux-gcc-release`, `linux-clang-debug`, `linux-sanitize` |

Run only your host's row. Front-end, backend, runtime and test changes that cross none of those boundaries stay on the inner loop until the gate.

**Build the row concurrently.** Each preset has its own binary directory and they are independent, so launch them as separate background jobs and collect the results rather than running them one after another — four at once on Windows, two on Linux. Never point two presets at one build directory.

**3. Milestone gate — clean builds for this host.** Delete the build directories first; incremental output is not a gate.

**A gate is evidence for a revision, not a tax on every commit.** Run one before a milestone closes, before a tag, and at least once per session. It was previously required before every push, and that cost is measurable: about ten gates in one day, roughly twenty minutes each, and not one of them caught a defect the inner loop or a targeted `ctest -R` had not already surfaced. The configurations are still worth running; running all of them several times an hour is not.

**The routine row is the four configurations that differ in compiler or instrumentation:** `windows-msvc-debug`, `linux-gcc-debug`, `linux-clang-debug`, `linux-sanitize`. Those are the axes that find things — a second front end, and a memory and undefined-behaviour checker.

**A gate closing a milestone adds the two release builds:** `windows-msvc-release`, `linux-gcc-release`. Their distinct value is a warning-free optimised compile rather than a second opinion on behaviour, so run them build-only unless the milestone changes something the optimiser could miscompile. A Linux host runs two or four rather than six, because `windows-msvc-*` is not reachable there.

**A gate subsumes the matrix.** If the gate is next — because a milestone is closing — skip tier 2 and let the gate's clean builds cover it. Running the configurations incrementally and then deleting them to run the same ones again is twice the work for one answer.

**The gate closes on this host's row, not on every host.** A Windows gate passing says nothing about native Linux or ARM64, and those are expected to need fixing when first built. Do not wait for them, and do not imply they passed.

**Record the gate** in `implementation.md` under a `## Gate verification` heading, one block per gate, most recent first — a line per configuration and an explicit list of what remains unverified:

```
<iso timestamp> — host gate (Windows)
Revision: <short sha the gate covers>

windows-msvc-debug    PASS <n>/<n>
linux-gcc-debug       PASS <n>/<n>
linux-clang-debug     PASS <n>/<n>
linux-sanitize        PASS <n>/<n>
not verified: windows-msvc-release, linux-gcc-release, native Linux x64, Linux ARM64
```

A gate closing a milestone adds the two release rows and drops them from the `not verified` list. Recording four when four ran is correct — the block names what ran, not what exists.

<One paragraph of prose: what the gate covers, why any row's count differs from the others, and anything that disturbed the run — a compiler crash that cleared on rebuild, a KRO CRC mismatch from a stale ctest, a row that had to be re-run. Say whether each was traced.>

**The timestamp is when the gate's builds started**, not when the record is written. Take it from the first build job's start line. `local-datetime` answers "what time is it now", which is the wrong question for a record of something that has already finished — a block stamped 14:45 for a gate that built at 14:41 points at a moment when nothing was built, and the timestamp exists to identify which gate this is.

A record saying "all configurations" or "six configurations" is not a record: name each one. Do not scatter the result into milestone evidence or a running log, and do not round the count.

**Target sources rebuild with the compiler.** `targets/**` and `sysroot/**` are compiled by `krc`, not by the host compiler, so a green host build says nothing about them. After any change under `src/`, rebuild the target artifacts and compare: an artifact that changes when only compiler internals changed is a regression signal — investigate it before committing rather than accepting the new bytes.

## Commits and pushing

**This section authorises local commits without asking, and authorises pushing when the tier the change needed is green.**

- Batch local commits rather than committing each change: hold a commit until five functional changes have accumulated, or the change to source and tests passes 180 lines, whichever comes first. Documentation (implementation.md, handoff.md, docs/) does not count toward that total. A batch is committed once the tier it required is green, and the regression tests covering the batch are run before the commit. Local commits need no permission.
- **Push when the tier your change needed is green.** Documentation alone: no build. Source or test changes: the inner loop for this host, and the matrix when the change crosses a boundary. A host gate is evidence for a revision rather than a precondition for every push — run one before a milestone closes, before a tag, and at least once per session, per tier 3 above. The trade is deliberate and worth restating: commits published between gates are verified on the inner loop only, so the once-per-session floor is a floor, not a target, and a period with no gate is a period with one toolchain's opinion.
- A tier 2 change may be published once its matrix is green; it does not wait for a gate. If the change touches an ABI, the object format, the symbol format or the target descriptor, prefer the gate anyway — those are the changes a second compiler or a sanitizer is most likely to disagree with.
- **Tidy unpublished history before pushing** if that produces a clearer record — squash fixups, reorder, reword. That rewrites only commits nobody has seen.
- **Never rewrite or force-push anything already pushed.** If CI fails on a pushed commit, fix forward with a new commit.
- One logical change per commit; subject imperative, at most 72 characters.

## Definition of done

- [ ] Builds clean with **no warnings** in **every configuration this host built**: `/W4 /WX` on MSVC, `-Wall -Wextra -Wpedantic -Werror` on GCC/Clang (plan §15.4).
- [ ] `ctest --preset <host-debug>` is green, and so is the test run for every other configuration this host built.
- [ ] The deterministic-output check passes: the same inputs built twice compare byte-for-byte (§15.6).
- [ ] `docs/` and `implementation.md` are updated in the same change — every merged change updates the implementation log (§15.7).
- [ ] An ADR under `docs/decisions/` exists when the change touches ABI, object format, symbol format, target description, or IR.
- [ ] An Appendix A entry exists when a `D-xxx` decision changes.
- [ ] No external runtime dependency added; doctest is the only vendored test dependency.
- [ ] No secrets, debug output, or unrelated edits in the diff.

## Platform targets

<!-- Generated by `sync.mjs --targets=...`; regenerated on `--update`. The Notes column is
     yours and is preserved. The universal portability rules in AGENTS.md apply whether or
     not anything is listed here. -->
<!-- project:targets:start — generated from .dev-template.json targets; edit the Notes column, not the rows -->
| Target | Architecture | Verified by | Notes (yours — preserved on update) |
|---|---|---|---|
| Windows | x64 | <windows-latest in CI, or name the dev machines> | MSVC 19.51 Debug/Release on the local dev machine. CI workflow committed; whether it runs is unverified. |
| Linux | x64 | <ubuntu-latest in CI> | WSL 2, Ubuntu 26.04.1 LTS. GCC 15.2 and Clang 21.1.8, Debug/Release and ASan/UBSan: all four Linux presets are built and tested here. Clang's ASan/UBSan runtimes come from `libclang-rt-21-dev`, a separate package from `clang`. |
| Linux (Raspberry Pi) | ARM64 | <manual: machine name, last verified revision> | Raspberry Pi 5, Debian 13 (trixie), GCC 14.2.0 and Clang 19.1.7, Debug/Release and ASan/UBSan. The routine ARM64 row is `linux-gcc-debug`, `linux-clang-debug`, `linux-sanitize`; a milestone-closing gate adds `linux-gcc-release`. **This host's polling loop is a gatekeeper, not an author: it deliberately gates every incoming code change, so the once-per-session floor above does not apply to it.** Latest gate 2026-11-18T05:53:20 at `37d60e2`: 821/821 on all three routine rows. The same host recorded the `std::strlen` build failure at `1229297` before it was fixed, which is why the second-compiler trap above exists. `vm/` holds the arm64 VM copies outside `build/`; their hashes are in `implementation.md`. |

Target-specific notes:

- **Windows:**
  - Long paths beyond 260 characters fail unless long-path support is enabled.
  - Use the language path API even though Windows accepts `/`; the tool that rejects a slash is usually native, not the runtime.
- **Linux:**
  - Filename case is significant here. An import that mismatches on-disk case resolves on Windows and fails here.
- **Linux (Raspberry Pi):**
  - Confirm every native dependency has a prebuilt artifact for ARM64, or document the build-from-source requirement.
  - Do not assume word size, byte order, or the presence of SIMD.
  - No hosted runner here by default: record the machine and the last verified revision, and keep it current.
<!-- project:targets:end -->

**Host, not VM.** This table is where the toolchain is *built and tested*. The compiler's output targets are KR32 and KR16 — they are not host platforms and do not belong here. ARM64 host CI is deliberately deferred (§15.4); the compiler must nevertheless stay portable C++20, so do not introduce host-specific code without an ADR.

## Architecture boundaries

Dependencies flow one way: **front end → IR → backend → assembler → linker**, with shared services (diagnostics, source manager, target descriptors) available to all and depending on none of them.

- `src/frontend/` — preprocessor, lexer, parser, Sema. Must not depend on any backend, linker, or runtime code.
- `src/ir/` — typed SSA and IR validation. Must not depend on a specific target; target-specific choices belong in `src/backend/` and the target descriptor.
- `src/backend/` — instruction selection, register allocation, frames, asm printing, per-target descriptors. Depends on IR, never the reverse.
- `src/asm/`, `src/linker/`, `src/debug/` — object format, `krld`, symbol/debug format. The linker must not reach back into the front end.
- `targets/` — target sources compiled by `krc`: `common/runtime/`, `kr32/runtime/`, `kr16/runtime/`, `kr64/runtime/`. Never built by the host compiler.
- `sysroot/` — target headers and prebuilt libraries. Read by `krc`; never host-compiled.
- `include/krc/` — the public surface. Keep it minimal; internal headers stay under `src/`.
- One compiler driver: `krc` orchestrates; `krld` remains a standalone binary. Do not add a third entry point.

## Environment assumptions

- **Doctor:** `pwsh -File .agents/doctor.msvc.ps1` reports whether this machine can build with MSVC, and exits 2 naming each component that is missing. Run it before a host gate, and whenever a build cannot find its compiler. No doctor exists for WSL yet; there, configuring `linux-gcc-debug` is the check. Resolved toolchains and their verification dates live in the Platform targets table above — do not restate them here.
- CMake 3.25 or newer; C++20; no external runtime dependencies.
- The Linux presets use the Ninja generator, so `ninja` must exist on the Linux host. A WSL image may ship without it; configuring `linux-gcc-debug` then fails on the missing generator, which is a provisioning gap rather than a broken preset.
- MSVC on Windows and GCC/Clang on Linux, both with warnings as errors. Sanitizers (ASan/UBSan) are configured on Linux only.
- GCC 15 in this WSL image segfaults while garbage-collecting its own trees, which surfaces as `internal compiler error: Segmentation fault` inside standard headers. It is intermittent and hits different files, so build the Linux rows with `ulimit -s unlimited` and at most `-j2`, and rebuild once when it happens before treating a build as failed. With all four presets running at once it also fails on small files, so run the four Linux presets one at a time.
- Run the gate rows one at a time on this machine, and run each row's test suite with nothing else building. With four rows running at once, `ctest` reads the staged `libkrcrt.kro` while another job is rebuilding it, which surfaces as `krld: KRO CRC mismatch` in a single link test that passes on its own; the concurrency the build policy asks for assumes headroom this machine does not have.
- doctest is vendored for tests — do not replace it with a fetched framework.
- The executed tests run a linked image under a VM and check its exit status and, where it is meaningful, its output, which a link-only test cannot do. Two cache variables name the binaries: `KRC_KR32_VM` and `KRC_KR16_VM`. Each is resolved at configure time from the first of `vm/krc_kr<32|16>[.exe]`, the legacy `build/vm/krc_kr<32|16>[.exe]`, and — on Windows only — the sibling checkout's build output that exists. `vm/` is deliberately outside `build/`, which a clean gate deletes. The run tests are not registered otherwise, so a host without a runnable VM still passes.
- Current milestone state lives in `implementation.md`; read it before starting work. Do not restate it here.
- Sibling repositories `../kr32` and `../kr16` are **read-only inputs** (their docs, corpus and conventions are cited throughout `plan.md`). Never edit them from here.

## Known traps

- **`AGENTS.local.md` is an instruction file, not a log.** Update it when a rule it states becomes false; never record progress, status, or milestone state here — that belongs in `implementation.md`. This file was rewritten with milestone state in 117 of 135 commits, so the rules governing the work changed almost every commit and nothing reviewed those changes.
- **The plan is approved (2026-11-09).** Implement under §18.2; `implementation.md` is the live tracker. A `D-xxx` change needs an Appendix A entry; ABI, object, symbol, target, or IR changes also need an ADR under `docs/decisions/`.
- **"Target" is two words here.** Host platforms (Windows, Linux) versus VM targets (KR32, KR16). Confusing them produces wrong CI matrices, wrong ABI assumptions, and target-descriptor code checked into the front end.
- **Warnings-as-errors cuts both ways.** A change verified only on MSVC can fail the Linux build on `-Wpedantic`, and vice versa. When a warning appears, fix the code; do not add a pragma, and do not narrow the warning set.
- **A transitive include hides a missing header, and MSVC is the one that supplies it.** `706109b` called `std::strlen` with no `<cstring>`: MSVC compiled it, GCC and Clang both refused, and the tree was unbuildable on Linux until `37d60e2` — while the MSVC inner loop and all 821 tests were green. Warnings-as-errors cannot catch this, because the failure is a missing declaration rather than a diagnostic. **A change that adds a standard-library call or includes a new header must be built by a second compiler before it is pushed** — `cmake --build --preset linux-clang-debug` from Windows, or `linux-gcc-debug` in WSL — even though it crosses none of the boundaries the matrix names. This is the one case where the inner loop is not sufficient evidence on its own.
- **Do not delete `build/<preset>/stage/` on its own.** It holds `stage/tests/`, which nothing recreates, so every link test fails with `cannot open object output ...\stage\tests\...kro` — a message that names the output file rather than the missing directory, and 740 tests fail at once. A gate deletes the whole build directory and configures, which recreates it; a bare removal of `stage` does not. The same cause produces `krc -c failed with no such file or directory` when `cmake -P cmake/LinkRuntime.cmake` is invoked by hand.
- **Determinism is byte-exact.** Sorted symbols, stable section order, no timestamps, build-root remapping (§15.6). A new field in an object, symbol table, or IR dump can silently break the double-build comparison — run it, do not assume it.
- **Every merged change updates `implementation.md` and the owning doc.** The plan names it as the live tracker; a change without the log update is incomplete.
- **ABI, object, symbol, target and IR changes need an ADR first.** Not a note in the commit message — a file under `docs/decisions/`.
- **`D-xxx` identifiers are stable.** A changed decision takes an Appendix A entry; renumbering or deleting a decision breaks every reference that cites it.
- **The compiler is one program with many outputs** (`-c` object, `-S` assembly, `-o` linked image). Do not grow a second driver to handle a case; extend `krc`.
- **The CI workflow covers the full matrix; whether it runs is unverified.** `.github/workflows/ci.yml` has a job per preset, including the Clang and sanitizer ones, which are built and tested on both hosts here. A golden or differential test that passes locally on one host proves nothing about the other; report which toolchain you ran.
