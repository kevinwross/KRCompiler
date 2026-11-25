# krc and krld user manual

This manual is for programmers who write C or C++ that runs on a KR-family
virtual machine. It covers two host programs:

- **`krc`** — the compiler driver. It preprocesses, parses, checks, lowers, and
  emits target assembly or a relocatable object. It can also link, through the
  `krc image` command.
- **`krld`** — the linker. It combines relocatable objects, assembly sources, and
  archives into the image a VM loads and runs.

The tools are host programs; KR32, KR16, KR216, and KR232 are output targets.
This manual describes the command line of `krc 0.2.0` and `krld 0.2.0`. Run
`krc --version` and `krld --version` to see the version and build of the copy you
are using.

Contents:

1. [The build model](#1-the-build-model)
2. [Before the first build](#2-before-the-first-build)
3. [Hello, world for KR32](#3-hello-world-for-kr32)
4. [Choosing a target](#4-choosing-a-target)
5. [KR32](#5-kr32)
6. [KR16](#6-kr16)
7. [KR232](#7-kr232)
8. [KR216](#8-kr216)
9. [KR64](#9-kr64)
10. [C++](#10-c)
11. [Command reference](#11-command-reference)
12. [Files, names, and output](#12-files-names-and-output)
13. [Diagnostics and troubleshooting](#13-diagnostics-and-troubleshooting)
14. [What the tools do not provide](#14-what-the-tools-do-not-provide)
15. [Where the detail lives](#15-where-the-detail-lives)

## 1. The build model

A program is built in two steps, exactly as with a conventional toolchain:

```
source.c ──krc -c──> source.kro ──krld──> program.kr32
   │                                          ▲
   └──────────── krc -S ──> source.c.s ───────┘
      (assembly; krld assembles it during the link)
```

- `krc -c` writes a **KRO object**: relocatable code, data, symbols, and debug
  information. It does not resolve external symbols.
- `krld` places sections at their final addresses, applies relocations, resolves
  the entry symbol, and writes the **image** the VM loads.
- `krld` also accepts `.s` assembly sources and assembles them itself. That is
  how the startup file joins the link without a separate assembler program.
- `krc image` performs the same link as `krld`, from the same linker code, with
  target-aware output naming and an optional listing. It exists so a build with a
  single driver can link without a second executable; `krld` is the standalone
  linker and the command this manual uses.

There is no separate assembler binary and no host linker involvement. Object
and image formats are specified in [object.md](object.md); the symbol and debug
payload is specified in [symbols.md](symbols.md).

## 2. Before the first build

### The tools

The tools are host programs built by CMake. Their build output, for a multi-config
generator, is:

```
build\<preset>\host\bin\<Debug|Release>\krc.exe
build\<preset>\host\bin\<Debug|Release>\krld.exe
```

A single-config generator (Ninja on Linux) puts them in `build/<preset>/host/bin/`.
Building the toolchain itself is covered in [../README.md](../README.md); this
manual begins with the tools already built or installed.

The examples below define the paths once, for a Windows Debug build. Substitute
the preset and configuration you built, and drop `.exe` on Linux:

```powershell
$build = "build\windows-msvc-debug"
$krc   = "$build\host\bin\Debug\krc.exe"
$krld  = "$build\host\bin\Debug\krld.exe"
$stage = "$build\stage\sysroot"
```

The runtime paths in the examples (`$rt = "targets\..."`) are relative to the
repository root, so run the commands from there or point `$rt` at the checkout.
The source file (`hello.c`) lives in the directory you are working in.

An installed prefix has a different layout:

```
<prefix>\bin\krc.exe
<prefix>\bin\krld.exe
<prefix>\share\krc\sysroot\        headers, generated bits, and staged runtime
```

`krc` compiled into a build tree knows its staged sysroot by absolute path, so
the examples below need no `--sysroot`. An installed `krc` keeps that build-time
path, so pass the installed sysroot explicitly:

```powershell
& $krc --sysroot C:\krc\share\krc\sysroot -c hello.c
```

### The sysroot

The sysroot supplies the target headers and the prebuilt runtime:

| Path under the sysroot | Contents |
|---|---|
| `include/` | Target-independent C and C++ headers: `stdio.h`, `stdlib.h`, `string.h`, `<vector>`, `<string>`, and the rest of the shipped subset |
| `<target>/include/` | Per-target headers, including the generated `bits/types.h`, `bits/stdint.h`, and `bits/float.h` |
| `kr32/lib/`, `kr16/lib/`, `kr232/lib/` | Staged runtime objects and startup assembly |

Include search order is: the directory of the including file (for `"..."` only),
then each `-I` directory in command-line order, then the sysroot's `include/`,
then the sysroot's `<target>/include/`. `--sysroot` replaces the whole sysroot;
with a wrong path, an `#include <stdio.h>` fails with `header not found`.

### The runtime

No runtime is linked implicitly by `krld`, or by `krc -c` for KR32, KR216, and
KR232. A program that calls `printf`, `malloc`, or a host service needs the
matching objects on the link line or `krld` reports the symbol as undefined. The
one exception is the KR16 image that `krc -c --target=kr16` produces, which
carries a `puts` stub (section 6).

| Target | Startup | C library | Host services | Exception runtime |
|---|---|---|---|---|
| KR32 | `targets/kr32/runtime/crt0.s` | `stage/sysroot/kr32/lib/libkrcrt.kro` | `targets/kr32/runtime/fileio.s` | `targets/kr32/runtime/eh.s` |
| KR16 | `targets/kr16/runtime/crt0.s` | `stage/sysroot/kr16/lib/libkrcrt.kro` | `libkrcrt_host.kro` and `libkrcrt_fileio.kro` in the same directory | not applicable |
| KR232 | `targets/kr232/runtime/crt0.s` | built from `targets/kr232/runtime/libc.c` or `libc_full.c` | `targets/kr232/runtime/fileio_full.s` for the full library | not applicable |
| KR216 | `targets/kr216/runtime/crt0.s` | built from `targets/kr216/runtime/libc.c` | `targets/kr216/runtime/syscalls.s` | `targets/kr216/runtime/eh.s` |

`crt0.s` defines `_start`, which is the linker's default entry symbol. The
startup calls `main` and passes the returned status to the VM's exit service.
`main` therefore has the usual `int main(void)` shape, and the value it returns
becomes the program's exit status on every target.

### The VM

The VMs are separate programs, not part of this repository. The CTest suite
finds them through `KRC_KR32_VM`, `KRC_KR16_VM`, and `KRC_KR232_VM`; a sibling
checkout typically builds them at:

```
..\kr32\build\windows-msvc\Debug\kr32.exe
..\kr16\build\windows-ninja\kr16.exe
..\kr232\build\gate-kernel\kr232-emu.exe
```

The command shape used by the test suite, and by this manual, is:

```powershell
$vm32  = "..\kr32\build\windows-msvc\Debug\kr32.exe"
$vm16  = "..\kr16\build\windows-ninja\kr16.exe"
$vm232 = "..\kr232\build\gate-kernel\kr232-emu.exe"

& $vm32 vm program.kr32 --max-steps 20000000 --fs-root .
& $vm16 vm program.kr16 --max-cycles 20000000 --fs-root .
& $vm232 vm program.kr232 --max-steps 200000 --transcript
```

The KR32 VM prints a register dump and `HALTED exit=N`; the KR16 VM returns the
program's exit status and prints only the program's console output. The KR232
emulator prints `halted status N` and, with `--transcript`, the console grid the
program wrote. `--fs-root` roots the guest's file services at a host directory;
without it the VM defaults to `./sandbox`, where opens fail.

## 3. Hello, world for KR32

`hello.c`:

```c
#include <stdio.h>

int main(void) {
    puts("Hello from krc");
    return 0;
}
```

Compile and link (the backtick continues a PowerShell line; a POSIX shell uses
`\`):

```powershell
$rt = "targets\kr32\runtime"
& $krc -c hello.c
& $krld "$rt\crt0.s" hello.kr32.kro "$stage\kr32\lib\libkrcrt.kro" `
        "$rt\fileio.s" "$rt\eh.s" -o hello.kr32
```

The link prints:

```
krld: linked 5 object(s) to hello.kr32 (entry 65536)
```

`entry 65536` is `0x10000`, the KR32 image base, where this link placed `_start`.

Run it:

```powershell
& $vm32 vm hello.kr32 --max-steps 20000000 --fs-root .
```

The program's output is the first line; the VM then prints its register dump
(abridged here) and `HALTED exit=0`:

```
Hello from krc
R0=0 R1=134217728 R2=0 ... PC=0x00010028 SP=0x0800FFF0 FLAGS=0x00000025 ticks=499
HALTED exit=0
```

Why each input is on the link line:

- `crt0.s` supplies `_start`; without it the linker reports
  `entry symbol '_start' is not defined`.
- `hello.kr32.kro` is your object. `krc -c` without `-o` named it
  `hello.kr32.kro`: the source stem, the target name, then `.kro`.
- `libkrcrt.kro` supplies `puts`, `printf`, `malloc`, the string functions, the
  C++ ABI support, and the 64-bit helpers.
- `fileio.s` supplies the `krc_sys_*` host-service stubs. `libkrcrt.kro`
  references them, so it is required even when the program does no file I/O.
- `eh.s` supplies the exception runtime. `libkrcrt.kro` references
  `__krc_current_exception`, so it is required for the same reason.

Linking a subset is a link error, not a smaller image: leaving out `fileio.s`
gives `krld: undefined symbol 'krc_sys_getchar'`, and leaving out `eh.s` gives
`krld: undefined symbol '__krc_current_exception'`.

The same image can be linked with `krc` itself. `krc image` assembles `.s` inputs
and takes the target from `--target`:

```powershell
& $krc image --target=kr32 hello.kr32.kro "$rt\crt0.s" `
    "$stage\kr32\lib\libkrcrt.kro" "$rt\fileio.s" "$rt\eh.s" -o hello-image.kr32
```

Both commands use the same linker and produce a runnable image. `krc image`
appends the target's image extension when `-o` has no suffix and refuses an `-o`
whose suffix names a different target.

## 4. Choosing a target

`--target=NAME` selects the output target. The default is `kr32`. Ask the
compiler what it accepts:

```powershell
& $krc --list-targets
```

```
kr32   register  pointer=32  data=ilp32  backend=yes
kr16   register  pointer=16  data=ip16   backend=yes
kr64   register  pointer=64  data=lp64   backend=no
kr216  register  pointer=16  data=ip16-long32  backend=yes
kr232  register  pointer=32  data=ilp32  backend=yes
```

| Target | Data model | `int` / `long` / pointer | 64-bit integer | `float`/`double` | Address space | Code generation |
|---|---|---|---|---|---|---|
| `kr32` | ILP32 | 32 / 32 / 32 | yes | 32 / 64 | 4 GiB | C and C++ |
| `kr16` | IP16 | 16 / 16 / 16 | no | no | 64 KiB | C and C++ within the profile |
| `kr232` | ILP32, no 64-bit type | 32 / 32 / 32 | no | 32 / 64 | 4 GiB | C and C++ |
| `kr216` | Ip16Long32 | 16 / 32 / 16 | yes | 32 / 64 | 64 KiB | C and C++ |
| `kr64` | LP64 (profile placeholder) | 32 / 64 / 64 | yes | 32 / 64 | — | none |

Other widths follow the profile: `char` is signed and 8 bits on KR32, KR216, and
KR232; it is unsigned and 8 bits on KR16. KR16's `short`, `int`, and `long` are
all 16 bits, which is a deliberately non-conforming profile. The normative
tables are [targets/kr32.md](targets/kr32.md), [targets/kr16.md](targets/kr16.md),
[abi-kr216.md](abi-kr216.md), and [abi-kr232.md](abi-kr232.md).

A target is chosen once per `krc` invocation; every input in that invocation is
compiled for it. Compiling the same source for several targets is several
invocations with different output paths. The default object name carries the
target (`hello.kr32.kro`, `hello.kr16.kro`), so the outputs can share a directory.

## 5. KR32

KR32 is the most complete target: 32-bit ILP32, floating point, `long long`,
exceptions, RTTI, and C++ classes with virtual dispatch.

### Compiling

```powershell
# Object for the linker
& $krc -c hello.c                       # writes hello.kr32.kro

# Assembly instead of an object
& $krc -S hello.c -o hello.s            # writes exactly hello.s

# Check only, no code generation
& $krc -fsyntax-only hello.c
```

`-c` accepts only C and C++ sources for KR32; it rejects `.s` input with
`error: target 'kr32' does not accept assembly input for -c`. Assembly files go
to `krld` or to `krc image`, which assemble them during the link.

### Linking

The complete C link line is the one in section 3. The runtime objects can be
taken from the staged sysroot and the assembly from the target sources:

```powershell
$rt = "targets\kr32\runtime"
& $krld "$rt\crt0.s" hello.kr32.kro "$stage\kr32\lib\libkrcrt.kro" `
        "$rt\fileio.s" "$rt\eh.s" -o hello.kr32
```

Useful link options:

```powershell
# Deterministic map: base, entry, every section and symbol
& $krld ... -o hello.kr32 --map=hello.map

# Section sizes on stdout
& $krld ... -o hello.kr32 --print-size

# KR32 symbol sidecar: the image sections plus the CRC of the flat form
& $krld ... -o hello.kr32 --sym=hello.sym

# Raw image with no container header, for a loader that reads a flat image
& $krld ... -o hello.bin --flat

# Section-level garbage collection (objects with .text.<name> sections)
& $krld ... -o hello.kr32 --gc-sections
```

`--sym` writes a sidecar for KR32 only; another target reports
`--sym is defined for kr32 images; this target wrote none`.

### Running

```powershell
& $vm32 vm hello.kr32 --max-steps 20000000 --fs-root .
```

### C++ on KR32

C++ mode is selected by the source extension (`.cpp`, `.cc`, `.cxx`) or by
`-std=c++17` (also accepted: `-std=c++11`, `-std=c++14`). The compiler and the
runtime provide classes, single inheritance, virtual dispatch, RTTI, exceptions
with cleanup, `new`/`delete`, function and class templates within the recorded
subset, and references.

```cpp
#include <stdio.h>

class Shape {
public:
    virtual ~Shape() {}
    virtual int sides() const = 0;
};

class Triangle final : public Shape {
public:
    int sides() const override { return 3; }
};

int main() {
    Triangle triangle;
    Shape* shape = &triangle;
    printf("a shape has %d sides\n", shape->sides());
    return 0;
}
```

```powershell
& $krc -c hello.cpp                       # writes hello.kr32.kro
& $krld "$rt\crt0.s" hello.kr32.kro "$stage\kr32\lib\libkrcrt.kro" `
        "$rt\fileio.s" "$rt\eh.s" -o hello.kr32
& $vm32 vm hello.kr32 --max-steps 20000000 --fs-root .
```

Output:

```
a shape has 3 sides
HALTED exit=0
```

The same link line as C works because `libkrcrt.kro` contains the C++ ABI
support. Exceptions additionally use `eh.s`, which is already on the line.

## 6. KR16

KR16 is a 16-bit embedded profile: 16-bit `int` and `long`, 16-bit pointers, a
64 KiB address space, no `long long`, and no floating point. `char` is unsigned.
A type the profile cannot represent is rejected where it is named:
`'long long' is not supported by the kr16 profile`, and the same form for
`float`, `double`, and `long double`. An aggregate return works through the
hidden stack argument [ADR 0048](decisions/0048-kr16-aggregate-return-destination.md)
defines; a variadic function that returns an aggregate is rejected.

### The single-command image

For a program whose only library call is `puts`, `krc -c` produces a complete
image. It prepends a small `_start` stub and a `puts` that writes the console
page at `0xF000`:

```powershell
& $krc -c --target=kr16 hello.c           # writes hello.kr16, an image
& $vm16 vm hello.kr16 --max-cycles 20000000 --fs-root .
```

```
Hello from krc
```

For KR16, `-c` writes an image; `--emit-object` writes a relocatable object
instead:

```powershell
& $krc -c --target=kr16 --emit-object hello.c    # writes hello.kr16.kro
```

The stub defines only `_start` and `puts`. A call to `printf`, `malloc`, or any
other runtime function needs the full link in the next section.

### The full runtime

The staged KR16 runtime is three objects: the C library, the host layer, and the
file layer. Link them with the startup:

```powershell
$rt = "targets\kr16\runtime"
& $krc -c --target=kr16 --emit-object hello.c -o hello.kr16.kro
& $krld --target=kr16 "$rt\crt0.s" hello.kr16.kro `
        "$stage\kr16\lib\libkrcrt.kro" `
        "$stage\kr16\lib\libkrcrt_host.kro" `
        "$stage\kr16\lib\libkrcrt_fileio.kro" -o hello-full.kr16
& $vm16 vm hello-full.kr16 --max-cycles 20000000 --fs-root .
```

`--target=kr16` is needed on `krld` because `crt0.s` is an assembly input; the
objects themselves carry their target. The link prints
`krld: linked 5 object(s) ... (entry 256)`; `256` is `0x100`, the KR16 image base.

The same program can pass `crt0.s` as an object instead; assembling it first with
`krc -c --target=kr16 --emit-object` lets the link omit `--target`.

### C++ on KR16

The C++ front end compiles for KR16 within the profile: classes, inheritance,
virtual dispatch, constructors and destructors, templates, and `new`/`delete`
work; exceptions and RTTI do not exist on this target. The manual's C++ example
links and runs on KR16 with the full runtime:

```powershell
& $krc -c --target=kr16 --emit-object hello.cpp -o hello-cpp.kr16.kro
& $krld "$rt\crt0.s" hello-cpp.kr16.kro "$stage\kr16\lib\libkrcrt.kro" `
        "$stage\kr16\lib\libkrcrt_host.kro" "$stage\kr16\lib\libkrcrt_fileio.kro" `
        -o hello-cpp.kr16
& $vm16 vm hello-cpp.kr16 --max-cycles 20000000 --fs-root .
```

```
a shape has 3 sides
```

Whether C++ is a supported language on KR16, rather than one that compiles, is an
open decision; the profile's limits are the binding constraint.

## 7. KR232

KR232 is 32-bit ILP32 without any 64-bit integer type: `long long`, `__int64`,
and anything that needs one are diagnosed. `float` and `double` share a binary64
datapath. Unlike KR16, `krc -c` writes an object, and the startup and library are
linked separately.

### Console output through the device page

The smallest complete program writes the console page directly through
`<kr232/cons.h>`:

```c
#include <kr232/cons.h>

int main(void) {
    cons_puts("Hello from krc\n");
    return 0;
}
```

```powershell
$rt = "targets\kr232\runtime"
& $krc -c --target=kr232 console.c -o console.kr232.kro
& $krc -c --target=kr232 "$rt\crt0.s" -o crt0.kr232.kro
& $krc -c --target=kr232 "$rt\cons.c" -o cons.kr232.kro
& $krld --entry=_start console.kr232.kro crt0.kr232.kro cons.kr232.kro `
        -o console.kr232
& $vm232 vm console.kr232 --max-steps 200000 --transcript
```

```
halted status 0 after 895 steps
console:
Hello from krc
```

`<kr232/cons.h>` is found through `sysroot/kr232/include`, which `krc` searches
automatically. `cons.c` writes the console page directly; link it when the
program may access that page. The staged `stage/sysroot/kr232/lib` also carries
`cons_user.kro`, the same interface over the kernel's syscall path, for a
user-mode build.

### The full C library: `printf`, `malloc`, strings

`libc.c` for KR232 is the small library (`putchar_`, `puts_`, `strlen`,
`malloc`, and the like). The full C library is `libc_full.c`, and it needs the
host layer `fileio_full.s`. Both are in `targets/kr232/runtime`:

```powershell
& $krc -c --target=kr232 --far-calls hello.c -o hello.kr232.kro
& $krc -c --target=kr232 "$rt\crt0.s" -o crt0.kr232.kro
& $krc -c --target=kr232 --far-calls "$rt\libc_full.c" -o libc_full.kr232.kro
& $krc -c --target=kr232 --far-calls "$rt\fileio_full.s" -o fileio_full.kr232.kro
& $krld --entry=_start hello.kr232.kro crt0.kr232.kro `
        libc_full.kr232.kro fileio_full.kr232.kro -o hello.kr232
& $vm232 vm hello.kr232 --max-steps 200000 --transcript
```

```
halted status 0 after 681 steps
console:
Hello from krc
```

`--far-calls` is required here, on the program and on both library objects. KR232
calls use a two-byte displacement by default, and the full library is large
enough that a call cannot reach its target. Without the flag the link stops
with:

```
krld: link relocation overflows its width (value 0x2290C, width 2, offset 48);
a call or branch this far from its target needs --far-calls (krc -c --far-calls)
```

The flag is a compile-time decision because the call encoding is fixed when the
object is written; adding it to `krld` afterwards cannot widen an emitted call.

### Entry and base

`krld`'s default entry `_start` and the KR232 startup work together: `crt0.s`
calls `main` and exits with the status in `R2`. The default image base is
`0x200`, above the reset word and interrupt vectors. `--entry=NAME` and
`--base=ADDR` override them.

## 8. KR216

KR216 has 16-bit pointers with 32-bit `long` and 64-bit integers, and it has
floating point. The backend, assembler, and `K216` image container are
implemented; this repository's test suite covers KR216 at the assembly and
object level, and no KR216 VM is present to execute a program here.

Compile and link a C program:

```powershell
$rt = "targets\kr216\runtime"
& $krc -c --target=kr216 hello.c -o hello.kr216.kro
& $krc -c --target=kr216 "$rt\crt0.s" -o crt0.kr216.kro
& $krc -c --target=kr216 "$rt\libc.c" -o libc.kr216.kro
& $krc -c --target=kr216 "$rt\syscalls.s" -o syscalls.kr216.kro
& $krc -c --target=kr216 "$rt\eh.s" -o eh.kr216.kro
& $krld --target=kr216 crt0.kr216.kro hello.kr216.kro libc.kr216.kro `
        syscalls.kr216.kro eh.kr216.kro -o hello.kr216
```

`--target=kr216` is needed because `crt0.s` is an assembly input. The default
base is `0x200` and the default entry is `_start`.

The `.kr216` container carries KDBG symbol and debug data, so the image can be
inspected without running it:

```powershell
& $krc --dump-symbols hello.kr216
```

C++ compiles for KR216 as well; the object and link steps are the same. Treat C++
execution on KR216 as untested in this tree: there is no VM here to run it.

## 9. KR64

KR64 has a target profile (`lp64`, 64-bit pointers) and no code generator. The
front end works: preprocessing, parsing, semantic analysis, AST dumps, IR, and
syntax checks.

```powershell
& $krc --target=kr64 -fsyntax-only hello.c     # succeeds
& $krc --target=kr64 -E hello.c                # succeeds
& $krc --target=kr64 -S hello.c                # fails
```

```
error: target 'kr64' has no code generator yet
```

## 10. C++

### Selecting C++

- A source ending in `.cpp`, `.cc`, or `.cxx` is compiled as C++.
- `-std=c++17` (or `c++11`, `c++14`) selects C++ for any source, including a
  `.c` file.
- `-std=c11` is the C default. No other dialect is accepted:
  `only -std=c11 and the C++ subset are supported`.

### The language subset

The front end implements a C++17 subset: classes, single inheritance, virtual
functions and dispatch, RTTI, exceptions on KR32 (class types only),
constructors, destructors with block-scope cleanup, `new`/`delete` including
arrays, default copy construction and assignment, references, `nullptr`, `auto`,
`decltype`, range-`for`, scoped enums, `constexpr`, and function, class, and
non-type template parameters. [release-notes.md](release-notes.md) lists what the
release leaves out; [cxx-front-end-gaps.md](cxx-front-end-gaps.md) records the
rejected constructs with reproducers.

Known limits, each with a reproducer in
[cxx-front-end-gaps.md](cxx-front-end-gaps.md):

- `throw` of a scalar is rejected; throw a class type.
- A `new` expression with a runtime bound is rejected; the bound must be a
  constant.
- Zero-argument functional casts such as `T()` are rejected; use `T x = 0;` for
  arithmetic types.
- `get<I>` and the parts of `<tuple>` that need it are absent.
- Member templates, callables as template parameters, and `<threads.h>` are
  absent. The VM is single-threaded; `thread_local` is static storage with a
  guarded initializer.
- KR16 has no exceptions, RTTI, `long long`, or floating point.
- KR232 has no 64-bit integer type.

### The standard library

`sysroot/include` ships a freestanding subset, not a full C++ standard library:
`<array>`, `<exception>`, `<functional>`, `<memory>`, `<stdexcept>`, `<string>`,
`<tuple>`, `<typeinfo>`, `<utility>`, and `<vector>`. These headers implement
what the compiler supports; a program that needs a facility the subset omits
fails at compile time or at link, not silently.

Exceptions use the C++ ABI runtime in `libkrcrt.kro` plus `eh.s` on KR32. A
program that throws must link both, exactly as the C link line does.

## 11. Command reference

### `krc`

```
krc [options] files...
krc image [options] objects... -o image
```

| Option | Meaning |
|---|---|
| `--version` | Print `krc <version>` and the build line |
| `--help`, `-h` | Print usage |
| `--list-targets` | Print each target's family, pointer width, data model, and whether a backend exists |
| `--target=NAME`, `--target NAME` | Select `kr32` (default), `kr16`, `kr216`, `kr232`, or `kr64` |
| `-std=c11` | C11, the default |
| `-std=c++11`, `-std=c++14`, `-std=c++17` | Compile as C++ |
| `-E` | Preprocess only; tokens go to stdout |
| `-fsyntax-only` | Preprocess, parse, and check; no code generation |
| `--dump-ast` | Print the parsed AST |
| `--dump-ir` | Lower to typed SSA IR and print it |
| `--dump-stats` | Print per-function IR instruction and block counts |
| `-O0` | No optimiser passes; the default |
| `-O1`, `-O` | Run the optimiser passes documented in [optimize.md](optimize.md) |
| `-S` | Emit target assembly |
| `-c` | Emit a relocatable object, or a complete image on KR16 |
| `--emit-object` | KR16, KR216, KR232: write a `.kro` object |
| `--far-calls` | KR232: emit calls in the wide form; needed for a large runtime |
| `-o FILE` | Output path |
| `-I DIR`, `-IDIR` | Add an include directory, before the sysroot |
| `--sysroot DIR`, `--sysroot=DIR` | Replace the target sysroot |
| `-l [FILE]`, `--listing=FILE` | Write a listing; the default name derives from the output name or the source |
| `--dump-symbols FILE` | Decode KDBG from a `.kr32`, `.kr16`, `.kr216`, or `.kr232` image |
| `--json` | JSON output for `--dump-symbols` |

With several inputs, `krc` compiles each independently. Without `-o`, every
output is named from its own source, so several objects can be produced in one
invocation. With `-o`, all inputs write the same path; pass one input per
invocation when `-o` is used.

`krc` exits `0` on success, `1` when a diagnostic was reported, and `2` for a
usage error (unknown option, missing option value, unknown target).

### `krc image`

```
krc image [options] objects... -o image
```

| Option | Meaning |
|---|---|
| `--target=NAME` | Target for `.s` inputs and for the output-extension check (default `kr32`) |
| `--entry=NAME` | Entry symbol (default `_start`) |
| `--base=ADDR` | Image base; `0` uses the target's default |
| `--flat` | Write a raw image instead of a container |
| `--source-root=DIR` | Store debug source paths relative to `DIR` |
| `--map=FILE` | Write the deterministic map |
| `--sym=FILE` | Write the KR32 symbol sidecar |
| `-l [FILE]`, `--listing=FILE` | Write a linked listing |
| `-o FILE` | Output image |

Inputs may be KRO objects, `.s` assembly, or KRO archives. With `-o` and no
suffix, `krc image` appends `.kr32`, `.kr16`, `.kr216`, or `.kr232` (or `.flat`
under `--flat`). A suffix naming another target is refused:

```
krc image: 'program.kr16' says kr16 but --target is kr32
```

### `krld`

```
krld [options] objects... -o image
```

| Option | Meaning |
|---|---|
| `--version` | Print the version and build |
| `--help`, `-h` | Print usage |
| `--target=NAME` | Target used to assemble `.s` inputs (default `kr32`); the linked objects determine the image target |
| `--entry=NAME` | Entry symbol (default `_start`) |
| `--base=ADDR` | Image base; default is per target: `0x10000` KR32/KR64, `0x100` KR16, `0x200` KR216/KR232 |
| `-o FILE` | Output image; the name is used exactly, with no suffix appended |
| `--flat` | Write the raw image: allocated sections at their addresses, no header |
| `--map=FILE`, `--map FILE` | Write the deterministic image map |
| `--sym=FILE`, `--sym FILE` | Write the KR32 symbol sidecar |
| `--layout=FILE` | Place named sections from `NAME ADDRESS` lines |
| `--source-root=DIR` | Emit debug source paths relative to `DIR` |
| `--print-size` | Print each section's address, size, and alignment |
| `--gc-sections` | Drop named `.text.<name>` sections the entry cannot reach |

Inputs may be KRO objects, `.s` assembly, or archives. Content decides, not the
suffix: an object named `program.s` still links. Mixing targets in one link is
refused:

```
krld: objects have different target IDs
```

`krld` exits `0` on success and `1` on any error.

## 12. Files, names, and output

### File kinds

| Extension | Produced by | Consumed by |
|---|---|---|
| `.c`, `.cpp`, `.cc`, `.cxx` | you | `krc` |
| `.s` | `krc -S`, hand-written startup | `krc` (KR16/KR216/KR232 `-c`), `krld`, `krc image` |
| `.kro` | `krc -c`, `krc -c --emit-object` | `krld`, `krc image` |
| `.kr32`, `.kr16`, `.kr216`, `.kr232` | `krld`, `krc image` | the matching VM |
| `.flat` | `krld --flat`, `krc image --flat` | a loader that reads a raw image |
| `.lst` | `krc -l`, `krc image -l` | you |
| `.map` | `krld --map`, `krc image --map` | you |
| `.sym` | `krld --sym` (KR32 only) | the KR32 OS symbol loader |

### Default names

| Command | Output without `-o` |
|---|---|
| `krc -S hello.c` | `hello.kr32.s` (the source's filename with the target inserted), in the current directory |
| `krc -c hello.c` | `hello.kr32.kro` |
| `krc -c --target=kr16 hello.c` | `hello.kr16` (a complete image) |
| `krc -c --target=kr16 --emit-object hello.c` | `hello.kr16.kro` |
| `krc -c --target=kr232 hello.c` | `hello.kr232.kro` |
| `krc -c --target=kr216 hello.c` | `hello.kr216.kro` |
| `krc image ... -o hello` | `hello.kr32` (the target extension is appended) |
| `krld ... -o hello` | `hello` (nothing is appended) |

For every target with a backend, `-S` names the assembly file after the source's
filename, not its path: `src/hello.c` writes `hello.kr32.s` in the working
directory.

### Objects, symbols, and determinism

A `.kro` object carries sections, symbols, relocations, and a `.debug_info`
bridge. `krld` converts the bridge into a `.kdbg` section in the image. Both
formats are documented in [object.md](object.md) and [symbols.md](symbols.md).

`krc --dump-symbols IMAGE` prints the KDBG records as text; `--json` prints them
as JSON. A statically linked image includes the runtime's symbols, so the dump
of a program linked with `libkrcrt.kro` is dominated by `printf`, `malloc`, and
the other runtime functions. The image's own functions appear with their source
ranges and scopes.

The same inputs produce byte-identical objects and images. There are no
timestamps in the output; recorded source paths are the ones passed to `krc`,
optionally rewritten by `--source-root` at link time. A build without `-O` and
one with `-O0` are byte-identical; `-O1` changes instructions, not the ABI.

### Listings and maps

```powershell
& $krc -c hello.c -l hello.lst -o hello.kr32.kro
& $krld hello.kr32.kro ... -o hello.kr32 --map=hello.map
```

The listing is an annotated assembly document: section, offset, encoded bytes,
and the statement as written. The map is the linked view: base, entry, each
section with its address and size, and every symbol with its address.

## 13. Diagnostics and troubleshooting

`krc` reports source diagnostics as `file:line:column: error: message`, quotes
the line, and marks the column. Linker errors are one-line `krld:` messages. A
failing compile or link does not write its own output path.

| Message | Cause | Action |
|---|---|---|
| `header not found: 'stdio.h'` | Wrong or missing sysroot, or no `-I` | Check `--sysroot`; the path must contain `include/stdio.h` |
| `undefined symbol 'puts'` | The runtime object was not linked | Add `libkrcrt.kro` (and the target's other runtime objects) |
| `undefined symbol 'krc_sys_getchar'` | `fileio.s` is missing on a KR32 link | Add `targets/kr32/runtime/fileio.s` |
| `undefined symbol '__krc_current_exception'` | `eh.s` is missing on a KR32 link | Add `targets/kr32/runtime/eh.s` |
| `duplicate symbol 'main'` | The same object was listed twice, or two sources define the symbol | Link each object once |
| `entry symbol '_start' is not defined` | No startup object, or a custom entry name | Add `crt0.s` or pass `--entry=NAME` |
| `objects have different target IDs` | A KR32 object and a KR16 object in one link | Link one target at a time |
| `link relocation overflows its width ... needs --far-calls` | A KR232 call cannot reach its target | Recompile the program and runtime with `--far-calls` |
| `KR16 branch displacement does not fit in 8 signed bits` | A conditional branch is too far; unconditional branches are promoted automatically | Restructure the code, or split the function |
| `target 'kr32' does not accept assembly input for -c` | `krc -c` was given a `.s` file for KR32 | Pass the `.s` file to `krld` or `krc image` |
| `-S and -c both write to -o; run them separately` | Both output-selecting flags with one `-o` | Run two invocations |
| `unknown target 'kr128'` | Misspelled target | Use `--list-targets` |
| `error: target 'kr64' has no code generator yet` | Code generation requested for KR64 (`-S` or `-c`) | Use `-fsyntax-only`, `-E`, `--dump-ast`, or `--dump-ir` |

A missing file input is reported before compilation:
`unable to open 'hello.c'`. A diagnostic exits `1`; a usage error exits `2`.

## 14. What the tools do not provide

- **No `-D`, `-U`, or `-include`.** Preprocessor definitions belong in the
  source or in a wrapper header. (The library API has a `defines` field; the
  `krc` command line does not expose it.)
- **No `-g`.** Debug information is always produced and always linked; there is
  no level to turn it off. `--source-root` and `--dump-symbols` are the controls
  that exist.
- **No library search path.** `-L` and a library `-l` do not exist; every runtime
  object is named on the link line. (`krc -l` is a listing request, not a
  library.) `krld` reads KRO archives if one is given, but the toolchain ships no
  archiver.
- **No separate assembler.** `krld` and `krc` assemble `.s` inputs themselves.
- **No incremental build system.** `krc` compiles translation units; dependency
  tracking and rebuild decisions belong to the build system around it.
- **No host C library.** The target sees only the sysroot headers and the
  runtime objects it links; the host's headers and libraries are not visible.
- **No implicit runtime.** `krc -c` never links; the only implicit link is the
  KR16 image built by `krc -c --target=kr16`, and it carries only the `puts`
  stub.

## 15. Where the detail lives

- [../README.md](../README.md) — what the project is and how to build the tools.
- [tree.md](tree.md) — host/target boundary and build output layout.
- [object.md](object.md) — the KRO object and image container.
- [symbols.md](symbols.md) — KDBG symbol and debug data.
- [abi.md](abi.md) — KR32 C/C++ ABI, calling convention, mangling.
- [abi-kr16.md](abi-kr16.md), [abi-kr216.md](abi-kr216.md),
  [abi-kr232.md](abi-kr232.md) — per-target ABI details.
- [targets/kr32.md](targets/kr32.md), [targets/kr16.md](targets/kr16.md) —
  target profiles.
- [host-services.md](host-services.md) — KR32 and KR16 host service calls.
- [optimize.md](optimize.md) — what `-O1` runs.
- [cxx-front-end-gaps.md](cxx-front-end-gaps.md) — C++ constructs the compiler
  rejects, with reproducers.
- [toolchain-embedding.md](toolchain-embedding.md) — using the compiler as a
  library instead of running `krc`.
