# Examples

End-to-end programs compiled by `krc`, not by a host compiler. The primary corpus is the sibling KR32/KR16 code inventoried in `plan.md` §5 and [tests/fixtures](../tests/fixtures/README.md).

Do not add example binaries or generated images to this directory.

## Games

Three single-file, text-mode games exercise the C++ front end and the KR32
runtime together: containers, classes, recursion, formatted output, integer
math, and console input. They are KR32 examples; KR16 is not a target.

| Source | Game | Controls |
|---|---|---|
| [startrek.cpp](startrek.cpp) | A Super Star Trek-style simulation: an 8x8 galaxy, Klingons, starbases, phasers, torpedoes, warp, shields, damaged systems, and the library computer. | `nav` `srs` `lrs` `pha` `tor` `she` `dam` `com` `xxx`; `q` quits. Course 1-8 is N, NE, E, SE, S, SW, W, NW; warp 0-8. |
| [tictactoe.cpp](tictactoe.cpp) | Tic-tac-toe against a computer using the win/block/centre/opposite-corner/corner/side strategy. Cells are numbered 1-9. | `1`-`9` for a move; `y`/`n` to play again. |
| [blackjack.cpp](blackjack.cpp) | Blackjack with a $100 bankroll, a $10 opening bet, $5 wagering steps, 3-to-2 blackjack, and a dealer who stands on every 17. | `+`/`-` change the bet by $5, `n` deals, `h` hits, `d` doubles down, `g` stands, `q` quits at the betting prompt. |

The games read commands one byte at a time and ignore spaces and newlines, so
the same source works from a terminal (press the key, then Enter) and from the
VM's scripted `--input` text. End of input exits cleanly. Each game seeds
`rand()` with a constant, which makes the deck, the galaxy, and the computer's
moves reproducible for a test; change the `srand` call for variety.

### Build and run

From the repository root with a Windows Debug build:

```powershell
$krc   = "build\windows-msvc-debug\host\bin\Debug\krc.exe"
$krld  = "build\windows-msvc-debug\host\bin\Debug\krld.exe"
$stage = "build\windows-msvc-debug\stage\sysroot\kr32\lib"
$vm32  = "..\kr32\build\windows-msvc\Debug\kr32.exe"

foreach ($game in "startrek", "tictactoe", "blackjack") {
    & $krc -c "examples\$game.cpp" -o "build\$game.kro"
    & $krld "build\$game.kro" "$stage\libkrcrt.kro" `
            targets\kr32\runtime\crt0.s targets\kr32\runtime\fileio.s `
            targets\kr32\runtime\eh.s -o "build\$game.kr32"
}

& $vm32 vm build\startrek.kr32  --max-steps 50000000 --input "srs`nlrs`nxxx`n"
& $vm32 vm build\tictactoe.kr32 --max-steps 20000000 --input "1`n9`n7`n2`nn`n"
& $vm32 vm build\blackjack.kr32 --max-steps 20000000 --input "n`nd`nq`n"
```

The `--input` string is one command per line. A longer session can be kept in
a file and read by the test harness; the CTest rows do exactly that.

### Tests

Six rows compile and link each game with `krc`, then run a deterministic
scripted session on KR32 and check its output:

```powershell
ctest --preset windows-msvc-debug -R "krc_(link|run)_(tictactoe|blackjack|startrek)"
```

The input scripts are under `tests/fixtures/games/`. `cmake/RunKr32Image.cmake`
gained an optional `INPUT_FILE` for these rows; its contents are passed to the
VM as `--input`.

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
