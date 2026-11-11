# Target sources

This directory contains source that is compiled **by `krc`**, not by the host compiler.

- `common/runtime/` — portable runtime pieces shared by every target.
- `kr32/runtime/` — KR32 `crt0.s`, libc implementation, syscall/FP helpers.
- `kr16/runtime/` — KR16 runtime sources (added with the KR16 runtime work).
- `kr64/runtime/` — placeholder until the KR64 backend exists.

Host compiler sources live under `src/` and are built by MSVC/GCC/Clang. The target-facing
header and library tree is `sysroot/`; it is read by `krc` and never host-compiled.

Build output is staged under `build/<preset>/stage/` by the host build system; see
[docs/tree.md](../docs/tree.md).
