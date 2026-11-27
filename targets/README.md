# Target sources

This directory contains source that is compiled **by `krc`**, not by the host compiler.

- `common/runtime/` — portable runtime pieces shared by every target.
- `kr32/runtime/` — `crt0.s`, `libc.c`, `fileio.s` (host services), and `eh.s` (exceptions).
- `kr16/runtime/` — `crt0.s`, `libc.c`, `fileio.c` (stream layer), and `host.s` (SYS wrappers).
- `kr216/runtime/` — `crt0.s`, `libc.c`, `eh.s`, and `syscalls.s`.
- `kr232/runtime/` — the kernel and user startup files, `libc.c`/`libc_full.c`/`libc_user.c`, the syscall stubs, and the console, framebuffer, and input drivers.

Host compiler sources live under `src/` and are built by MSVC/GCC/Clang. The target-facing
header and library tree is `sysroot/`; it is read by `krc` and never host-compiled.

Build output is staged under `build/<preset>/stage/` by the host build system; see
[docs/tree.md](../docs/tree.md).
