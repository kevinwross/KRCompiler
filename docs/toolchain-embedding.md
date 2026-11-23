# Embedding the KR compiler

`krc::toolchain` compiles KR32 and KR16 code inside the host process. It writes
no file and starts no subprocess: a host supplies source text and receives
assembly, object bytes, image bytes, structured diagnostics and listing rows.
The host reads its own inputs and writes its own outputs; the only files the
facade reads are headers a `#include` names, through the supplied include paths.
`krc` and `krld` are two consumers of the same library.

The facade header is [`include/krc/toolchain.h`](../include/krc/toolchain.h).
KDBG is a separate package: [`include/krc/debug/kdbg.h`](../include/krc/debug/kdbg.h)
and [symbols.md](symbols.md).

## Requiring the package

Both libraries are static and installed together:

```cmake
find_package(krc-toolchain CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE krc::toolchain)
```

`krc-debug` provides `krc::debug` for a host that only decodes images:

```cmake
find_package(krc-debug CONFIG REQUIRED)
target_link_libraries(my_reader PRIVATE krc::debug)
```

Configure with `-DCMAKE_PREFIX_PATH=<install prefix>`. The installed interface
exposes `include/krc` and nothing under `src/`; the compiler implementation is
inside `libkrc-toolchain.a` / `krc-toolchain.lib`.

## One configuration per prefix

The installed libraries are static and built with the dynamic CRT, and one install
exports exactly one configuration: a Debug install writes `krc-targets-debug.cmake`,
a Release install writes `krc-targets-release.cmake`, and both name the same file.
A host built for one configuration therefore has to link a package of that
configuration — a Debug host against a Release archive fails with `LNK2038`, naming
`_ITERATOR_DEBUG_LEVEL` and `RuntimeLibrary`. Since `krc-toolchain_DIR` is cached,
one build tree can only carry one of them, so a machine that builds both keeps two
prefixes and points each tree at the matching one. The development machine here
uses `C:/Users/kevin/krc` for Release and `C:/Users/kevin/krc-debug` for Debug,
installed from the same source with `cmake --install build\windows-msvc-debug
--config <Debug|Release>`.

## Ten lines to an image

```cpp
#include <fstream>
#include <iostream>
#include <vector>

#include "krc/toolchain.h"

int main() {
    std::vector<krc::toolchain::Source> sources{
        {"hello.c", "int main() { return 42; }"}};
    const auto built = krc::toolchain::build(sources, krc::toolchain::Options{});
    if (!built.ok) {
        std::cerr << krc::toolchain::format_diagnostics(built.diagnostics)
                  << built.error << '\n';
        return 1;
    }
    std::ofstream image("hello.k32", std::ios::binary);
    image.write(reinterpret_cast<const char*>(built.image.data()),
                static_cast<std::streamsize>(built.image.size()));
}
```

`build` compiles every source in order, assembles each one and links the objects;
the result is a `.k32` for `Target::Kr32` and a `.kex` for `Target::Kr16`. It
links exactly the sources it was given. A runnable image also needs the target
runtime and startup object: read those bytes, wrap them in
`krc::toolchain::Object`, and call `link`, or assemble `targets/kr32/runtime/crt0.s`
with `assemble` first. The facade never finds a runtime on its own.

## The call sequence

`compile` → `assemble` → `link` exposes the stages; `build` is the three calls in
a row and returns the listing and the linked sections. Each result has `ok`:

- `compile` returns assembly (`assembly`), the textual `.debug_info` bridge
  (`debug_info`), and, when requested, `ast_dump`, `ir_dump` and `stats`.
- `assemble` takes assembly text and returns `object` bytes and `listing` rows.
- `link` takes object bytes and returns `image`, `sections`, `symbols`, `entry`
  and `base`. It infers the target from the objects.

`Options`:

| Field | Meaning |
|---|---|
| `target` | `Kr32` or `Kr16`; `Kr64` is accepted only up to `Stage::Syntax` |
| `cxx` | C++ mode; named explicitly rather than inferred from a file name |
| `debug` | emit the `.debug_info` bridge into a produced KR32 object |
| `optimize` | `0` or `1`, matching `-O0` / `-O1` |
| `stage` | `Syntax`, `Ir` or `Assembly`; a syntax check never reaches the backend |
| `dump_ast`, `dump_ir`, `dump_stats` | fill the matching result string |
| `base` | `0` selects `0x10000` for KR32 and `0x100` for KR16 |
| `entry` | empty selects `main` |
| `include_paths`, `defines` | passed to the preprocessor; a define is `NAME` or `NAME=VALUE` |
| `source_root` | applied by the linker's debug source-path rewrite |
| `gc_sections` | drop objects the entry symbol cannot reach |
| `section_addresses` | `NAME`/`ADDRESS` placements, as a `krld` layout file holds |

## Ownership and errors

An input's text is borrowed for the duration of the call; every output is owned
by the caller. A user error returns `ok == false`, diagnostics with
`file`/`line`/`column`/`line_text` already resolved, and, where there is no
source location, a non-empty `error`. `format_diagnostics` renders them in the
compiler's canonical form. A violated internal invariant still throws; no
exception is turned into an empty message.

The facade keeps no mutable global state. Source ids, type canonicalisation and
the backend's debug buffer are per call, so two builds in one process do not
observe each other. The same request produces identical bytes regardless of
working directory, time or environment.

## Listing

A [`ListingRow`](../include/krc/toolchain.h) carries a section name, the
statement's address, its encoded bytes, its text and the source line a preceding
`.Ldbg` label attributed it to. `assemble` reports the address within the section;
`build` reports the final address in the image. The facade does not choose a
`.lst` format — KR32 and kr16 keep their own — so the host renders the rows.

## The installed sysroot, runtime and tools

`cmake --install` (with the default `KRC_BUILD_TOOLS=ON`) installs `krc` and
`krld` under `<prefix>/bin`, and the target sysroot and runtime under
`<prefix>/share/krc/sysroot`. `find_package(krc-toolchain CONFIG REQUIRED)`
exports:

- `KRC_SYSROOT` — `<prefix>/share/krc/sysroot`
- `KRC_KR32_RUNTIME_DIR` — `${KRC_SYSROOT}/kr32/lib`

```text
bin/
  krc[.exe]                the compiler driver
  krld[.exe]               the linker
share/krc/sysroot/
  include/                 the shared C/C++ headers, including krc/host.h
  kr32/include/bits/       stdint.h and types.h generated for the profile
  kr32/lib/                libkrcrt.kro, crt0.s, fileio.s, eh.s
  kr16/include/            the KR16 profile headers
  kr64/include/            the KR64 profile headers
```

`krc`'s compiled-in default sysroot is its build-time path, so a host that runs
the installed tool passes `--sysroot <prefix>/share/krc/sysroot` (the package's
`KRC_SYSROOT`). With that, `krc -c` and `krld` build and link from the install
alone: no KRCompiler build tree is needed on `PATH`.

To build a runnable KR32 image, compile with `KRC_SYSROOT/include` and
`KRC_SYSROOT/kr32/include` on `Options::include_paths`, assemble the three
runtime sources, read `libkrcrt.kro` as an `Object`, and link in the order the
CLI uses — user object, `libkrcrt.kro`, `fileio.s`, `eh.s`, `crt0.s` — with
`entry = "_start"`. The installed tree is self-contained: no KRCompiler source
or build tree is needed.

## Reading KDBG

A KR32 or KR16 compile produces the textual `.debug_info` bridge (`debug_info`),
and `link` converts it to `.kdbg` for the image. `krc::debug::find_kdbg_section`
locates `.kdbg` in a `.k32` or `.kex` and reports the container's load base;
`decode_kdbg_image` finds, decodes and rejects a payload whose recorded image
base differs from that base. `encode_kdbg`, `decode_kdbg` and the text/JSON
dumpers work on bytes and need no toolchain library.

`KdbgImage::minor_version` is `1` for a payload this writer produces and `0` for
a v1 payload; a decoder accepts both, and the v1.1 fields (`locations`, `scopes`,
`variable_info`, and a type's `base`/`element`/`element_count`/`target`) are
empty for a v1 payload. `require_kdbg_feature` reports "newer KDBG" when a caller
needs a v1.1 fact from a payload this reader cannot interpret, rather than
guessing. The JSON schema is in [symbols.md](symbols.md).
