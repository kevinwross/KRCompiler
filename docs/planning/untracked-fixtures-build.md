# Building the checked-in images instead of tracking them

Working note, 2026-11-26. For each of the 27 checked-in binaries in the family:
its source, the command that produces it, and whether a generator already exists.
Not a decision record and not committed work.

Columns: artifact — source — recipe — generator today.

## kr32 — the embedded OS images (4)

Host-specific by construction: `krc` embeds source paths, so the `windows/` pair
is only reproducible on Windows and the `linux/` pair only on Linux.

| Artifact | Source | Recipe | Generator |
|---|---|---|---|
| `src/resources/os/windows/bios.kr32` | `kr32os/**` (guest C and asm) | `pwsh kr32os/build.ps1 -Krc <krc.exe> -Out <dir>` (bios base `0xFFFF0000`, default `-O1`), then `scripts/update-os.ps1` copies both images | `kr32os/build.ps1` |
| `src/resources/os/windows/kernel.kr32` | same | same build (kernel base `0x00020000`) | same |
| `src/resources/os/linux/bios.kr32` | same | `KRC=<krc> KR32OS_BUILD_DIR=<dir> bash kr32os/build.sh` | `kr32os/build.sh` |
| `src/resources/os/linux/kernel.kr32` | same | same build | same |

## krdbg — adapter and debugger fixtures (10)

| Artifact | Source | Recipe | Generator |
|---|---|---|---|
| `tests/fixtures/kr232/loaded.kr232` | `krcompiler/tests/fixtures/c/kr232_min.c` + `crt0.s` | `regenerate.ps1` (entry `_start`, default base) | `tests/fixtures/kr232/regenerate.ps1` |
| `tests/fixtures/kr232/nested_frames.kr232` | `krcompiler/.../kr232_nested_frames.c` | same | same |
| `tests/fixtures/kr232/session.kr232` | `krcompiler/.../kr232_session.c` + `cons.c` + `gfx.c` | same, with `-I <krcompiler>/sysroot/kr232/include` | same |
| `tests/fixtures/kr232/kernel.kr232` | `kr232/targets/kr232/kernel/kernel.s` | same, `--source-root=<kr232>` | same |
| `tests/fixtures/kr232/userprint.kr232` | local `userprint.s` | same, `--base=0x08000000` | same |
| `tests/fixtures/kr32/m3.kr32` | local `m3.c` + `getchar.s` + kr32 `crt0.s` | `regenerate.ps1` with `KRDBG_KRC/KRLD/SYSROOT/KR32_RUNTIME`; `-O0`, `--base=0x10000 --entry=_start --source-root=.` | `tests/fixtures/kr32/regenerate.ps1` |
| `tests/fixtures/kr16/m4.kr16` | local `m4.c` | `krc --target=kr16 -O0 -c` | `tests/fixtures/kr16/regenerate.ps1` |
| `tests/fixtures/kr16/legacy.kr16` | local `m4.c` | `kr16 cc -g -O0` then `kr16 ld` — **needs the kr16 VM binary**, not `krc` | same |
| `tests/fixtures/kdbg/fake.kr32` | `tests/fixtures/kdbg/fake.c` | `python tests/fixtures/kdbg/regenerate.py --krc … --krld …` | `regenerate.py` |
| `tests/fixtures/kdbg/fake.kdbg` | sliced from `fake.kr32` (magic to EOF) | same script writes it | same |

`kernel.kr232` here is byte-identical to kr232's copy (SHA-256
`7FB06A5E…4501EF2`), so one could be generated and the other read from it.

## kr232 — machine fixtures (9)

`pwsh tests/fixtures/regenerate.ps1 -Krc <krc> -Krld <krld> [-Runtime <krcompiler>/targets/kr232/runtime]`
rebuilds seven of the nine, running from the repository root with relative paths.

| Artifact | Source | Notes |
|---|---|---|
| `user-calls.kr232` | `user-calls.c` + `crt0_user.s` + `syscall6.s` | user mode, `--base=0x08000000` |
| `user-exit42.kr232` | `user-exit42.s` | same base |
| `user-echo.kr232` | `user-echo.s` | same base |
| `user-text.kr232` | `user-text.s` | same base |
| `blank-flip.kr232` | `blank-flip.s` | no base |
| `kernel-gate.bin` | `kernel-gate.s` | no base |
| `kernel.kr232` | `targets/kr232/kernel/kernel.s` | no base, entry `_start` |
| `kernel-draw.bin` | **none — hand-encoded, 295 bytes** | would need a `.s` written first |
| `halt.bin` | **none — hand-encoded, 6 bytes** (`ld r7,#0`, `sys`) | would need a `.s` written first |

## KRCompiler (2)

| Artifact | Source | Recipe | Generator |
|---|---|---|---|
| `tests/fixtures/kr232-draw.kr232` | `tests/fixtures/kr232-draw.s` | `krc -c --target kr232 tests/fixtures/kr232-draw.s` then `krld --entry=main --base=0x200` | none, but the recipe is one line |
| `tests/fixtures/kr232-fixture.kr232` | **none** | said to be "a two-instruction `.s`" that is not in the tree | none |

## kr16 (2)

| Artifact | Source | Recipe | Generator |
|---|---|---|---|
| `tests/golden/exec_min.kr16` | `golden/generate.cpp` | build target `kr16_golden_gen` (EXCLUDE_FROM_ALL), then run it | CMake target |
| `tests/golden/rel_min.o` | same | same | same |

## What untracking would involve

- **23 of 27 already have a generator**: kr32's two build scripts, krdbg's three
  `regenerate.ps1` and one `regenerate.py`, kr232's `regenerate.ps1`, and kr16's
  `kr16_golden_gen` target. Untracking them means running those in the build, not
  writing new recipes.
- **1 more has a source and a one-line recipe**: `kr232-draw.kr232`.
- **3 have no source at all** — `halt.bin`, `kernel-draw.bin`,
  `kr232-fixture.kr232`. They must stay committed, or their sources written first.
- **Wiring**: the generators need `krc`/`krld`, which are built in the same tree
  (KRCompiler) or a sibling (the rest). A CMake custom command that depends on
  the `krc` target works for the compiler's own fixture; the sibling repos
  already take `KRG_KRC`/`KRC_EXECUTABLE`, so the same command can call them.
  `legacy.kr16` additionally needs the kr16 VM binary (`KRDBG_KR16`).
- **kr32's four images are host-specific**: untracking both pairs would leave a
  host able to generate only its own, so the cross-host comparison in
  `CheckOsImages.cmake` would have nothing to compare on the other platform.
- **Staleness already present**: `user-calls.kr232` no longer reproduces (the
  ARM64 gate's drift row, confirmed on Windows), and `kr232-draw.kr232` does not
  either — same recipe, entry `0x200` reproduced, image 1074 bytes against the
  committed 955, first difference at offset 16 (a header field). So generating
  at test time would make that drift fail immediately rather than as a separate
  row, and the frame-hash and KDBG assertions in those tests would need
  re-validating against the regenerated images.
