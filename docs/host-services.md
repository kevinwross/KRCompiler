# Host services

**Status:** KR32 wrappers are implemented. The KR16 VM now implements the file
services; the KR16 wrappers are not written.

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

The KR16 VM exposes `SYS #n` with the service block in `R0`. The block holds
four 16-bit arguments followed by four 16-bit results. Service 0 is the version
call, which reports the ABI version and a capability mask; services 1–13 are
`OPEN`, `CLOSE`, `READ`, `WRITE`, `SEEK`, `STAT`, `UNLINK`, `RENAME`,
`MKDIR`, `READDIR`, `TIME`, `RANDOM`, and `FSINFO`. Open flags are
`0` read-only, `1` write-only, `2` read-write, or optionally `0x0100` create,
`0x0200` truncate, `0x0400` append, `0x0800` exclusive. A single transfer is
limited to 4096 bytes, a guest path to 255 bytes, sixteen files may be open at
once, and a directory entry is 64 bytes with a name of at most 59. The KR16
runtime provides string, memory, allocation, console output, and — through
`targets/kr16/runtime/fileio.c` over `host.s` — the standard stream functions.
`<stdio.h>` declares a KR16 branch because the stream record is sixteen-bit: the
profile gets `FILE`, `fopen`, `fclose`, `fread`, `fwrite`, `fgetc`,
`fputc`, `putc`, `fgets`, `fputs`, `ungetc`, `ftell`,
`fseek`, `feof`, `ferror`, `clearerr`, `remove`, `rename`, and
`stdout`/`stderr` as lazily initialised streams over descriptors 1 and 2. A seek
is absolute or relative to the stream's own position; `SEEK_END` is refused
because the host offers no size query through the seek service. A data pointer
whose initialiser needs a relocation is not emitted on this profile, which is why
`stdout` is a call rather than a global.

## Linking

`cmake/LinkRuntime.cmake` adds the KR32 host stubs to every runtime link by
default. `krc` and `krld` do not inject host services themselves; a target image
that calls a host wrapper must link the runtime objects.
