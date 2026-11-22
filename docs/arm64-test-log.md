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

## 2026-11-20T09:15:19-08:00 — adopt-krc @ 797dfac

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 5956bbe..797dfac, 3 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`cmake/CheckKr16Kdbg.cmake` added, so it was re-run; VM paths retained)
- Build: linux-gcc-debug — ok, 44 s, 23 targets
- Test: linux-gcc-debug — 911/911 passed, 269 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

Count rises 910 → 911: the KR16 backend now emits debug info so a `.kex` carries `.kdbg`, and a new
check registers against that (the KR16 pin SHA is reported alongside). This is the first time the KR16
debug path runs here — the 191 executed rows include KR16 images, so a regression that broke KR16 object
emission would have shown up as a failing run rather than only a compile error. `include/krc/toolchain.h`,
`src/toolchain/toolchain.cpp` and the linker also changed. No disturbance, no retry.

## 2026-11-20T10:35:11-08:00 — adopt-krc @ 0defcce

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 3ba93c0..0defcce, 3 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`cmake/AssembleKr16.cmake` changed; VM paths retained)
- Build: linux-gcc-debug — ok, 36 s, 16 targets
- Test: linux-gcc-debug — 913/913 passed, 273 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

Count rises 911 → 913 with the KR16 directives `kr16`'s libc needs: `src/asm/kr16_assembler.cpp`,
`src/asm/kr16_object.cpp` and `src/linker/kr16_image.cpp` changed, and a `bss_zero.s` fixture was added
alongside new assembler and object unit cases. This is the KR16 object path, so the executed KR16 images
are the meaningful coverage and they all ran. No disturbance, no retry.

## 2026-11-20T10:59:46-08:00 — adopt-krc @ 47fa4e5

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 77fd893..47fa4e5, 3 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`CMakeLists.txt` and the toolchain config template changed; VM paths retained)
- Build: linux-gcc-debug — ok, 11 s (incremental; only the staged runtimes relinked)
- Test: linux-gcc-debug — 913/913 passed, 274 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`b680815` installs the target sysroot and runtime through the toolchain package, so `CMakeLists.txt` and
`cmake/krc-toolchain-config.cmake.in` carry the change while the only compiled difference is small — hence
the 11 s build. Count is unchanged at 913: the install rules add no test, and the executed rows still run
the same binaries from `vm/`. No disturbance, no retry.

## 2026-11-20T13:45:02-08:00 — adopt-krc @ 7d005d5

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 0951c6e..7d005d5, 2 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`cmake/LinkKr16ShippedFileio.cmake` added; VM paths retained)
- Build: linux-gcc-debug — ok, 31 s, 10 targets
- Test: linux-gcc-debug — 915/915 passed, 270 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

Count rises 913 → 915 with `__host_call` in the KR16 backend plus a shipped-fileio link path and a
`shipped_fileio_crt0.s` fixture. The new intrinsic reaches the host through the VM, so the executed KR16
images are exactly what exercises it, and both new rows ran and passed here. No disturbance, no retry.

## 2026-11-20T14:14:41-08:00 — adopt-krc @ 937b2f4

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: ae69da6..937b2f4, 2 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, <1 s (`CMakeLists.txt` changed; VM paths retained)
- Build: linux-gcc-debug — ok, 11 s (incremental; only the staged runtimes relinked)
- Test: linux-gcc-debug — 915/915 passed, 268 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`402cfaf` installs `krc` and `krld` with the package, so only `CMakeLists.txt` changed and the compiled
code is unchanged — hence the 11 s build and the same 915 count. The install rules were exercised by the
configure step here; they do not add a test of their own. No disturbance, no retry.

## 2026-11-20T14:49:57-08:00 — adopt-krc @ b2d12dc

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 0eee39c..b2d12dc, 2 commits
- Docs-only: no
- Configure: linux-gcc-debug — not re-run, `CMakeLists.txt` unchanged
- Build: linux-gcc-debug — ok, 30 s, 11 targets
- Test: linux-gcc-debug — 915/915 passed, 265 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`57182f6` fixes KR16 line-table attribution for declarations in `src/backend/kr16_backend.cpp`, with a
`test_toolchain` case alongside. Line tables are debug-only, so the compile-and-run rows are the ones that
show the fix did not disturb code generation: all 915 passed, executed images included. Count is unchanged
at 915 because the new case lives inside an existing binary. No disturbance, no retry.

## 2026-11-20T22:47:54-08:00 — adopt-krc @ d2be672

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: b2d12dc..d2be672, 2 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, 1 s (`tests/CMakeLists.txt` changed; VM paths retained)
- Build: linux-gcc-debug — ok, 49 s, 23 targets
- Test: linux-gcc-debug — 915/915 passed, 386 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`5983f90` adds KR16 function sections and section-level GC across `src/asm/kr16_object.cpp`,
`src/linker/linker.cpp` and the assembler, with a new `test_kr16_sections` unit file and
`krc_link_kr16_gc_sections` / `krc_run_kr16_gc_sections` coverage. All 915 passed, but the suite took
386 s against a 265–274 s band on the previous nine runs of this preset: the added time sits almost
entirely in the link and run tests (their summed time 251.6 s → 358.9 s, +43%), spread across every test
rather than concentrated in one outlier. That is consistent with the linker doing section-level GC on
each link, but I did not trace it, so treat it as an unexplained slowdown rather than a confirmed cost.

## 2026-11-20T23:14:48-08:00 — adopt-krc @ 75aee3b

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: d2be672..75aee3b, 1 commit
- Docs-only: no
- Configure: linux-gcc-debug — ok, 1 s (`CMakeLists.txt` and `tests/CMakeLists.txt` changed; VM paths retained)
- Build: linux-gcc-debug — ok, 16 s, 7 targets
- Test: linux-gcc-debug — 915/915 passed, 270 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

`75aee3b` releases 0.2.0 and versions the package configs. The suite is back in its usual band at 270 s;
the 386 s recorded for `d2be672` did not persist across this SHA, so that slowdown was not a lasting cost of
the section-level GC and I am leaving it as an unexplained one-off rather than reopening it.

## 2026-11-21T16:00:28-08:00 — master @ 69cb4dc

- Host: BitWhiz (Raspberry Pi 5, Debian 13 trixie, aarch64)
- Range: 6d638c4..69cb4dc, 51 commits
- Docs-only: no
- Configure: linux-gcc-debug — ok, 0 s (`CMakeLists.txt` and `cmake/` changed; VM paths retained)
- Build: linux-gcc-debug — ok, 116 s, 48 targets
- Test: linux-gcc-debug — 915/915 passed, 310 s
- Skipped: nothing — executed VM rows registered
- Next: nothing pending

This is the first gate on the default branch after `adopt-krc` was merged into it: the merge brought the
toolchain facade, `.kdbg` in `.kex`, KR16 directives, KR16 function sections and section-level GC, and the
package install rules, and masters them all in one range. The suite is 915 tests, the same count the last
`adopt-krc` gate ran, and the wall time (310 s) is back in the 265–310 s band.
