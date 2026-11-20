# adopt-krc log — stage A of the krc toolchain adoption

Append-only. Every entry begins with the exact `iso` string from `local-datetime`,
called immediately before that entry was written. Entries are never rewritten; a
correction is a new entry. The plan is
`krdbg/docs/requests/krcompiler-toolchain-package.md` §4 items 4.1–4.9; the brief
is `krdbg/docs/requests/brief-krcompiler.md`.

## 2026-11-20T03:47:13-08:00

**Changed:** nothing in source. Recorded the revision and the output corpus this
stage is measured against, so the byte-identity claims in 4.2/4.5 are compared
with real bytes rather than with memory.

**Branch:** `adopt-krc` at `104f84f` (`Add ARM64 tester instructions`), based on
the pushed `master` `6d638c4`; `origin/adopt-krc` is also `104f84f` and the tree
is clean. No new branch was created and `master` was not touched.

**VMs:** `vm/krc_kr32.exe` (7,968,768 bytes) and `vm/krc_kr16.exe` (6,611,968
bytes) are both present, and the existing preset cache resolves `KRC_KR32_VM`
and `KRC_KR16_VM` to them. The executed tests are therefore registered and ran.

**Commands and real results:**

- `cmake --preset windows-msvc-debug` (inside `vcvarsall.bat x64`) — configure
  succeeded, generator `Visual Studio 18 2026`.
- `cmake --build --preset windows-msvc-debug --parallel` — build succeeded;
  `krc.exe`, `krld.exe`, `krc_tests.exe` and the staged KR32/KR16 runtimes were
  produced or up to date.
- `ctest --preset windows-msvc-debug --output-on-failure` — **910/910 passed,
  0 failed, 166.09 s**.

**Baseline output corpus** (produced by `build/windows-msvc-debug/host/bin/Debug`
at `104f84f`; SHA-256). `hashes.txt` is under `build/`, which a gate deletes, so
the hashes are repeated here:

| SHA-256 | Output |
|---|---|
| `CD68A6762927A52E4CA8B05EF5FF9760309BAF8E13E1D135DCD0AD9EE3FEA685` | `hello.kro` (KR32 `-c`) |
| `8C13AEAE831803355F65ED1D9F83B8049CD8E98FF90177F00A140AE609A0F176` | `debug_names.kro` (KR32 `-c`, C++) |
| `DD159744776DEFE94964999A3641360D1FD693E08909492A461C0A0F47858D0D` | `hello.k32` (KR32 link, no libc) |
| `550820EAF0105F50EA78F63E922648FF78D5B9AB9403B8BB69652E21DBA019AA` | `debug_names.k32` (KR32 link with `libkrcrt`) |
| `429CF24C2CC6200DFC0727E94CC97285C3DA0966F5B154411855AE3132E5CA64` | `hello.s` (`-S`) |
| `FCBC5703E6BDB0AA5A8F43BDBDFEE4F78FE52F37967D10D7600B766B3A59A3ED` | `debug_names_O0.s` (`-S`) |
| `A3060DE1674EB90D37B42B26F3DF78BF213C4CD895DA148DD5467D869B5B1A4F` | `debug_names_O1.s` (`-S -O1`) |
| `EB77B4FC5666A54BBF62014C189BFB996044542B1BB6051ACDA65A0B56BAF0CE` | `kr16_crt0.kro` (KR16 `--emit-object`) |
| `2D2B5A181E5619076B677D39B5A2F92C92DACDDFE494D174574A2584CF3A7200` | `kr16_arith.kro` (KR16 `--emit-object`) |
| `6B1C1D0C7CE6CD44C4802F83EB714B5148C94BA63FCF5795020F8960BAABAF0E` | `kr16_sum.kex` (KR16 single-image `-c`) |
| `EC32AAC8E3CE3BD10885AF1DB43AC5D42C0E4D535A6A965E1AD4CF4222E7F5F3` | `kr16_arith.kex` (KR16 link: crt0 + `libkrcrt` + user) |

**Commit:** none (baseline evidence only; no tracked file changed before this log
entry).

**Next step:** 4.1 (`include/krc/toolchain.h`) and 4.3 (`krc::toolchain` /
`krc::debug` CMake packages, install/export), then 4.2/4.4.

**Blocker:** none yet. One plan gap is flagged before 4.6: item 4.6's done-when
requires a `.kex` that decodes with sources, lines and functions, but no KR16
path emits `.debug_info` today — `Kr32Backend::debug_info()` is the only bridge
producer and `asmkr16::make_object` writes only `.text` — so `attach_kdbg`
finds nothing to convert for a KR16 link. The `.kex` extra-section and image-base
mechanism can be implemented as planned; the missing KR16 debug-info source is a
decision for the programme owner, not something this session will invent.
