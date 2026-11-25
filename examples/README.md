# Examples

Small end-to-end examples will be added with the first working compiler at M2/M3. The primary corpus is the sibling KR32/KR16 code inventoried in `plan.md` §5 and [tests/fixtures](../tests/fixtures/README.md).

Do not add example binaries or generated images to this directory.

## krc-test-compiler

`krc-test-compiler.c` is a small C-to-assembly compiler written in C and built
by `krc` for the target that runs it. Its console and file output use
`printf`/`fprintf`. KR32 does not pass `argc`/`argv` to `main`, so under
`__kr32__` the source collects the guest arguments with `sys_argc`/`sys_arg`.

The compiler always emits KR16 assembly text, whichever target runs it. Only
the compiler is portable; its output is not expected to assemble for the other
VMs.

It compiles for `kr32`, `kr16`, `kr216`, and `kr232`. The KR32 image is the one
verified end to end here, from the repository root with a Windows Debug build:

```powershell
$krc   = "build\windows-msvc-debug\host\bin\Debug\krc.exe"
$krld  = "build\windows-msvc-debug\host\bin\Debug\krld.exe"
$stage = "build\windows-msvc-debug\stage\sysroot"
$vm32  = "..\kr32\build\windows-msvc\Debug\kr32.exe"

& $krc -c --target=kr32 examples\krc-test-compiler.c -o build\cfu.kro
& $krld targets\kr32\runtime\crt0.s build\cfu.kro `
        "$stage\kr32\lib\libkrcrt.kro" targets\kr32\runtime\fileio.s `
        targets\kr32\runtime\eh.s -o build\cfu.kr32
& $vm32 vm build\cfu.kr32 --max-steps 500000000 --fs-root . `
        --arg tests/fixtures/c/kr16_sum.c --arg -o --arg build/cfu.s
```

The image prints `; wrote build/cfu.s` and exits 0, and `build/cfu.s` holds the
generated assembly. The compiler accepts at most 4095 bytes and only the
subset named at the top of the source, so the recipe feeds it the small
`kr16_sum.c` fixture; `krc-test-compiler.c` itself is much larger and cannot
compile itself.

The CTest suite builds the program and the KR32 runtime each at `-O0` and
`-O1` (four images), runs all four on KR32, and compares the assembly each one
emits:

```powershell
ctest --preset windows-msvc-debug -R test_compiler
```
