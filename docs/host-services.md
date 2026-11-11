# Host services

**Status:** KR32 wrappers are implemented; KR16 file services are deferred by
the KR16 VM.

## KR32

The runtime reaches the host through the KR32 `SYSCALL` vectors. The C ABI
places arguments in `R0`–`R3`; the host result is returned in `R0`. Negative
results are host error codes.

### Wrapper layers

1. `targets/kr32/runtime/fileio.s` provides raw `krc_sys_*` stubs that execute
   the vector and return the host result unchanged.
2. `targets/kr32/runtime/libc.c` provides the public C wrappers. A negative host
   result becomes `-1`, and `errno` receives the negated code.
3. `sysroot/include/krc/host.h` declares the public wrappers and open flags.

### Services

| Wrapper | Vector | Arguments | Result |
|---|---:|---|---|
| `getchar` | 34 | — | byte or `-1` |
| `open` | 35 | path, flags | descriptor or `-1` |
| `close` | 36 | descriptor | `0` or `-1` |
| `read` | 37 | descriptor, buffer, count | bytes or `-1` |
| `write` | 38 | descriptor, buffer, count | bytes or `-1` |
| `seek` | 39 | descriptor, offset, whence | position or `-1` |
| `sys_time` | 40 | — | host seconds |
| `sys_argc` | 42 | — | argument count |
| `sys_arg` | 43 | index, buffer, size | copied length or `-1` |
| `unlink` | 52 | path | `0` or `-1` |
| `rename` | 53 | old path, new path | `0` or `-1` |
| `stat` | 54 | path, buffer, capacity | `0` or `-1` |
| `truncate` | 55 | path, length | `0` or `-1` |
| `getcwd` | 56 | buffer, capacity | length or `-1` |
| `chdir` | 57 | path | `0` or `-1` |
| `dup` | 58 | descriptor | descriptor or `-1` |
| `dup2` | 59 | source, target | target or `-1` |

`putchar` (vector 33) and `exit` (vector 32) remain in `crt0.s`.

### Open flags

| Flag | Value |
|---|---:|
| `KRC_OPEN_READ` | 0 |
| `KRC_OPEN_WRITE` | 1 |
| `KRC_OPEN_READ_WRITE` | 2 |
| `KRC_OPEN_CREATE` | 4 |
| `KRC_OPEN_TRUNCATE` | 8 |
| `KRC_OPEN_APPEND` | 16 |

### errno

`errno` is a macro expanding to `*__krc_errno_location()`. The runtime stores
the code in `krc_errno`. Codes are the positive host error values; the current
`errno.h` names `EDOM`, `ERANGE`, `EINVAL`, `ENOMEM`, and `ENOENT`.

### Sandbox

The KR32 VM roots file services at the host's current directory. Paths outside
that root are rejected unless obtained through a host dialog. Guest paths are
UTF-8 and are passed through unchanged.

## KR16

The KR16 VM exposes `SYS #n` with `R0` as the service block. Version 1 implements
only `SYS_VERSION`; `SYS_OPEN`/`SYS_CLOSE`/`SYS_READ`/`SYS_WRITE`/`SYS_SEEK`/
`SYS_STAT` are deferred. The current KR16 runtime therefore provides string,
memory, allocation, and console-output services only.

## Linking

`cmake/LinkRuntime.cmake` adds the KR32 host stubs to every runtime link by
default. `krc` and `krld` do not inject host services themselves; a target image
that calls a host wrapper must link the runtime objects.
