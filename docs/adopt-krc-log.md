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

## 2026-11-20T04:23:04-08:00

**Changed (items 4.1, 4.2, 4.3, 4.4, 4.5, 4.9):** the toolchain is a library and
the CLIs consume it.

- `include/krc/toolchain.h` is the build facade: `compile`, `preprocess`,
  `assemble`, `link`, `build`, `Options`, `ListingRow`, and diagnostics rendered
  to data (`Diagnostic` carries file/line/column/line text, so a host can print
  after the call). It includes no internal header; its only project includes are
  `krc/frontend/diagnostics.h`, `krc/frontend/lexer.h` and nothing under `src/`.
- `include/krc/debug/kdbg.h` is the KDBG reader/writer made public; the
  implementation is still `src/debug/kdbg.cpp`, whose only dependency is the
  standard library. The old `src/debug/kdbg.h` is gone, not forwarded.
- `src/toolchain/toolchain.cpp` implements the facade over today's internals. It
  owns the pipeline the CLI used to inline (parse, sema, IR, verify, the `-O1`
  pass order, backend selection, object assembly, link and image writing). All
  state is per call; an audit found no mutable static in the path, only
  function-local `static const` tables.
- CMake: `krc_impl` is an object library (not installed); `krc_toolchain` and
  `krc_debug` are the two static installable targets, exported as
  `krc::toolchain` and `krc::debug`; `krc-toolchain-config.cmake` and
  `krc-debug-config.cmake` are installed under `lib/cmake/`. The exported
  interface names only `${prefix}/include`, never `src/`.
- `krc` and `krld` are rebuilt on the facade: `krld` still reads files and
  archives and formats its map, but the link and image write are
  `toolchain::link`; `krc` keeps its argument parsing and output formatting and
  calls `compile`/`assemble`/`link`. `-E` uses `toolchain::preprocess`; the
  AST/IR/stats dumps are returned as data and printed by the CLI.
- Listing rows ship in both assemblers. A row carries section, address, encoded
  bytes, the statement text, and the source line a preceding `.Ldbg` label
  attributed it to. `assemble` returns section-relative addresses; `build`
  translates them to final addresses using the linked text address of each input
  object, which `link_objects` now records.
- KDBG header: `KdbgImage::image_base` exists and `decode_kdbg` reads offset 56,
  but `attach_kdbg` does not write it yet. That write is 4.6, deliberately not
  landed here so 4.2's byte-identity claim stays measurable.

**Commands and real results (Windows, `vcvarsall.bat x64`):**

- `cmake --preset windows-msvc-debug` — configure succeeded.
- `cmake --build --preset windows-msvc-debug --parallel` — build clean, no
  warnings (`/W4 /WX`).
- `ctest --preset windows-msvc-debug --output-on-failure` — **910/910 passed,
  0 failed, 172.51 s** (the baseline was 910/910 in 166.09 s).
- `krc_tests.exe` directly — **155 test cases, 155 passed, 1,444 assertions**.
- Standalone header compile: `cl /std:c++20 /W4 /WX /permissive- /EHsc /utf-8
  /I include /c header_check.cpp` where the TU includes only
  `krc/toolchain.h` and `krc/debug/kdbg.h` — clean.
- Baseline byte identity against the table in the `2026-11-20T03:47:13-08:00`
  entry: **11 of 11 outputs identical**, including both `.k32` images and the
  KR16 `.kex` from both the single-image and linked paths. `hello.s`,
  `debug_names_O0.s` and `debug_names_O1.s` are byte-identical, so the CLI's
  assembly text did not move.
- Package consumer at `build/consumer` (outside the source tree), configured with
  `-DCMAKE_PREFIX_PATH=build/install-krc` after `cmake --install ... --prefix
  build/install-krc`:
  `find_package(krc-toolchain CONFIG REQUIRED)` and
  `find_package(krc-debug CONFIG REQUIRED)` both resolve; `build_image` prints
  `kr32 image 1152 bytes entry 65536` and `kr16 image 163 bytes base 0x100 entry
  0x100`; `decode_kdbg` links only `krc::debug` and prints `decoded 1 source(s),
  1 function(s) from 333 bytes`.
- Boundary matrix, recorded in `implementation.md` under `## Gate verification`:
  `windows-msvc-debug` 910/910, `windows-msvc-release` 910/910,
  `linux-gcc-debug` 717/717, `linux-gcc-release` 717/717,
  `linux-clang-debug` 717/717, `linux-sanitize` 717/717.

**Commit:** `d3367a8` (`Add the krc::toolchain facade and publish it`). The gate
record and this entry follow in a documentation commit.

**Next step:** push `adopt-krc` for kr32 to pin `d3367a8`; then 4.6 (`.kdbg` in
`.kex`, image base at offset 56), which is the kr16 pin.

**Blocker:** none for 4.1–4.5/4.9. The 4.6 gap recorded in the baseline entry
still stands: no KR16 object carries `.debug_info`, so a KR16 `.kex` has nothing
for `attach_kdbg` to convert. The container and base mechanism can land; a `.kex`
that decodes with sources/lines/functions cannot until KR16 debug-info emission
exists, which no stage-A item authorises.

**Deviations from the plan's indicative shapes, recorded for the review:** the
facade's `Diagnostic` is a resolved value type rather than
`frontend::Diagnostic`, because a `SourceLocation` cannot be rendered after the
`SourceManager` dies and the plan requires diagnostics to be data. `Options`
carries `Stage`, `cxx`, `gc_sections` and `section_addresses` because the CLI
keeps `-fsyntax-only`, `-std=c++17` and `krld --gc-sections/--layout`; `Target`
also carries `Kr64` for front-end-only stages, because `--target=kr64
-fsyntax-only` worked before and still does. `LinkResult`/`BuildResult` carry
sections and symbols so `krld --map/--print-size` stays on the facade.
