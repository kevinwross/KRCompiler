# ARM64 test log — KRCompiler

Append-only. One entry per tested SHA, newest last, following [arm64-tester.md](arm64-tester.md).

## 2026-11-20T03:39:28-08:00 — master @ 6d638c4

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: first run
- Docs-only: no
- Configure: linux-gcc-debug — ok (not separately timed)
- Build: linux-gcc-debug — ok (not separately timed; `build/` deleted first)
- Test: linux-gcc-debug — 910/910 passed, 235 s
- Skipped: nothing — `KRC_KR32_VM` and `KRC_KR16_VM` resolved to `vm/krc_kr32` and `vm/krc_kr16`
- Next: nothing pending

First run under this protocol, and the first time this host's count includes the executed VM rows
alongside the newer debug fixtures: the run predates it by minutes but is the same tree, preset and
host, so it is the evidence for this SHA. `linux-clang-debug` (910/910, 234 s) and `linux-sanitize`
(910/910, 946 s) also passed on the same SHA; they are recorded here as extra evidence, not as part of
the required row. Configure and build were part of that three-preset sequence and were not timed
separately.

## 2026-11-20T03:49:13-08:00 — adopt-krc @ ffd25c3

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: first run
- Docs-only: no
- Configure: linux-gcc-debug — not re-run, `CMakeLists.txt` unchanged; the retained cache has `KRC_KR32_VM`/`KRC_KR16_VM` at `vm/`
- Build: linux-gcc-debug — ok, 9 s (incremental; only the staged runtimes relinked)
- Test: linux-gcc-debug — 910/910 passed, 239 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

The tip's code tree is identical to `master` @ `6d638c4`, which passed 910/910 forty minutes earlier; this
branch adds one documentation commit on top, so the build had nothing to compile. Recorded because
`adopt-krc` had no entry and its tip is therefore untested under this protocol, not because the code differs.

## 2026-11-20T04:31:31-08:00 — adopt-krc @ f936155

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: c4b6cfe..f936155, 3 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`CMakeLists.txt` and `cmake/` changed, so it was re-run; the cache kept `KRC_KR32_VM`/`KRC_KR16_VM` at `vm/`)
- Build: linux-gcc-debug — ok, 121 s, 78 targets
- Test: linux-gcc-debug — 910/910 passed, 264 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

First real change to reach this branch: the `krc::toolchain` facade (`include/krc/toolchain.h`, `src/toolchain/toolchain.cpp`), the `kdbg` surface extended for the debugger work, preprocessor and assembler/linker changes, and two unit suites (`test_toolchain.cpp`, `test_kdbg.cpp`). The count is unchanged at 910 because the two new unit files add cases inside the existing binaries rather than registering new tests at top level. No disturbance: no compiler warning, no KRO CRC mismatch, no retry.

## 2026-11-20T05:00:19-08:00 — adopt-krc @ d576c24

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 85eb9fb..d576c24, 6 commits
- Docs-only: no
- Configure: linux-gcc-debug — not re-run, `CMakeLists.txt` unchanged
- Build: linux-gcc-debug — ok, 48 s (incremental)
- Test: linux-gcc-debug — 910/910 passed, 268 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`b5740c8` carries the `.kdbg` debug container inside `.kex` and records the image base, which touches
`src/linker/linker.cpp` and the `krc` driver, so the executed-image rows are the ones that matter here:
every linked image keeps its debug payload and still runs. Also present are the `kdbg` v1 freeze ADR, the
host-embedding guide, the `include/krc/toolchain.h` read corrections and more `test_kdbg` cases. Count
stays 910 because the additions are cases inside existing test binaries. No disturbance, no retry.
