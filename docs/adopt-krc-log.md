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

## 2026-11-20T04:23:35-08:00

**Changed:** published the first stage-A milestone and corrected a SHA the
previous entry named.

**Pushed:** `f936155` on `adopt-krc`. This is the SHA kr32 pins for items 4.5 and
4.9; it carries the facade, both packages, the CLI rebuild and the listing rows.

**Correction to the `2026-11-20T04:23:04-08:00` entry:** that entry named
`d3367a8` as the facade commit. While it was being written the ARM64 tester
pushed four commits to `adopt-krc` (`c4b6cfe` was the tip), so the required
`git pull --rebase` rewrote the three unpublished commits. Their content is
unchanged; their SHAs are:

- `01b1ce3` — baseline log entry
- `b2eae12` — `Add the krc::toolchain facade and publish it`
- `f936155` — `Record the toolchain facade gate and log`

`implementation.md`'s gate block now names `b2eae12`, the rebased revision whose
tree the six rows built. The earlier entry is not rewritten.

**Next step:** 4.6 — write `image_base` in `attach_kdbg`, carry `.kdbg` in
`.kex`, and reject a base mismatch in the reader.

**Blocker:** the 4.6 gap recorded in the baseline entry: no KR16 object carries
`.debug_info`, so a KR16 `.kex` has nothing to convert. The container/base
mechanism proceeds; a `.kex` that decodes with sources, lines and functions needs
a decision on KR16 debug-info emission.

## 2026-11-20T04:53:18-08:00

**Changed (items 4.6, 4.7, 4.8):**

- **4.6.** `attach_kdbg` writes the image base into the KDBG header's eight
  reserved bytes at offset 56 as a little-endian `u64`; `write_kr16_image`
  carries `.kdbg` beside `.text` and `.debug_str` in a `.kex`, and
  `decode_kdbg_image` rejects a payload whose recorded base differs from the
  container's (0 means "not recorded" and is accepted, so older images stay
  readable). `docs/symbols.md`'s header row and ADR 0009 are amended; no version
  bump and no record type changes. `krc --dump-symbols` now takes its image as an
  ordinary input, so the plan's documented `krc --dump-symbols --json IMAGE`
  order works (the previous parser consumed `--json` as the file name).
- **4.7.** No further code: the package consumer built in the previous entry
  already links `krc::debug` alone and decodes a fixture through the installed
  package (`decoded 1 source(s), 1 function(s) from 333 bytes`).
- **4.8.** `docs/toolchain-embedding.md` is written and indexed in
  `docs/README.md`: requiring the packages, a ten-line build, the
  compile/assemble/link sequence, the `Options` table, ownership and error rules,
  the listing rows and the KDBG reader.

**Commands and real results:**

- `cmake --build --preset windows-msvc-debug --parallel` — clean.
- `ctest --preset windows-msvc-debug --output-on-failure` — **910/910 passed**,
  0 failed, 175.16 s.
- `krc_tests.exe` — **159 test cases, 159 passed, 1,484 assertions** (four new
  KDBG tests: base round-trip, `.kex` carry and decode, `.kex` mismatch rejection,
  `.k32` carry and mismatch rejection).
- Byte delta on a `.k32` against the pre-4.6 baseline: **9 bytes differ** in a
  4,169-byte image — offsets 56–63 (the base) and the header CRC at 52. Nothing
  else moved, which is the intended consequence of writing the field.
- `krc --dump-symbols --json hello.k32` — exit 0, 442 bytes of JSON.
- `krc --dump-symbols fixture.kex` (a `.kex` manufactured through
  `attach_kdbg` + `write_kr16_image`, 674 bytes) — prints
  `KDBG target=2 abi=1 sources=1 lines=1 functions=1`, `source 0 main.c`,
  `line 0x100 source=0 line=3 column=1`, `function main 0x100-0x110`.
  The JSON form prints sources and functions; the frozen JSON schema has never
  carried lines, so the line assertion uses the text form.
- Boundary matrix, recorded in `implementation.md` under `## Gate verification`
  for `0ca73e8`: `windows-msvc-debug` 910/910, `windows-msvc-release` 910/910,
  `linux-gcc-debug` 717/717, `linux-gcc-release` 717/717,
  `linux-clang-debug` 717/717, `linux-sanitize` 717/717.

**Commits:** `0ca73e8` (`Carry .kdbg in .kex and record the image base`),
`ba1fbb8` (`Document how a host embeds the toolchain`). The gate record and this
entry follow in a documentation commit.

**Next step:** push `adopt-krc` for kr16 to pin the resulting tip, then report that
SHA.

**Blocker:** 4.6's done-when says a `.kex` decodes "with sources, lines and
functions that match the source". The container and reader do; what is missing is
a `.kex` that a KR16 compile produced, because no KR16 object carries
`.debug_info`. `Kr32Backend` is the only producer of the bridge records and
`asmkr16::make_object` writes `.text` only. The `.kex` above is manufactured
through the linker API to prove the mechanism, not compiled from a KR16 source.
Making it real needs KR16 debug-info emission, which no stage-A item authorises;
that is a decision for the programme owner, not something this session will
invent. Everything else in stage A is landed.

## 2026-11-20T04:53:45-08:00

**Changed:** published the kr16 pin and corrected the SHAs the previous entry
named.

**Pushed:** `c30793b` on `adopt-krc`. This is the SHA kr16 pins for 4.6; it also
carries 4.7's verification and the 4.8 document. The tester had logged `f936155`
and pushed one commit while the stage-A work was local, so `git pull --rebase`
rewrote the four unpublished commits. Their content is unchanged; the SHAs are:

- `b5740c8` — `Carry .kdbg in .kex and record the image base` (was `0ca73e8`)
- `631e680` — `Document how a host embeds the toolchain` (was `ba1fbb8`)
- `c30793b` — `Record the 4.6 gate and stage-A status`

`implementation.md`'s second gate block now names `b5740c8`, the code revision
its six rows built; `631e680` and `c30793b` are documentation. The earlier entry
is not rewritten.

**Next step:** none in stage A; the outstanding decision is the KR16
`.debug_info` source named in the previous entry.

**Blocker:** unchanged — 4.6's container/reader half is done and published, and
its "a compiled KR16 `.kex` decodes with sources/lines/functions" half waits on a
decision about KR16 debug-info emission.
