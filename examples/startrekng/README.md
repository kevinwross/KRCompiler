# startrekng

An object-oriented Star Trek for the KR32 and KR232 virtual machines. It is a
multi-file C++ program compiled by `krc` and linked by `krld`.

## Layout

| File | Role |
|---|---|
| `console.cpp` | Console input and output, including the 4096-byte script buffer |
| `galaxy.cpp` | The 8x8 galaxy, its quadrants and objects |
| `ship.cpp` | The Enterprise, its devices and repairs |
| `scanner.cpp` | `Scanner` interface, short- and long-range scanners |
| `weapons.cpp` | `WeaponSystem` interface, phasers, torpedoes, Klingon batteries |
| `computer.cpp` | `Computer` interface and the library reports |
| `game.cpp` | Turn order, navigation, docking, combat and the command loop |
| `geometry.cpp` | Course and distance |
| `main.cpp` | Interactive console entry point |
| `main_script.cpp` | Scripted entry point for tests |
| `script.hpp` | The deterministic 108-byte session, under the 4096-byte cap |

The interfaces are abstract classes; the game asks each device for its report
or orders it to fire without knowing which implementation it holds.

## Build for KR32

```sh
for f in console geometry galaxy ship scanner weapons computer game main; do
    krc -c examples/startrekng/$f.cpp -o build/startrekng/$f.kro
done
krld targets/kr32/runtime/crt0.s \
     build/startrekng/console.kro build/startrekng/geometry.kro \
     build/startrekng/galaxy.kro build/startrekng/ship.kro \
     build/startrekng/scanner.kro build/startrekng/weapons.kro \
     build/startrekng/computer.kro build/startrekng/game.kro \
     build/startrekng/main.kro \
     build/windows-msvc-debug/stage/sysroot/kr32/lib/libkrcrt.kro \
     targets/kr32/runtime/fileio.s targets/kr32/runtime/eh.s \
     -o build/startrekng/startrekng.kr32
```

Run with a command script on the VM console:

```sh
kr32 vm build/startrekng/startrekng.kr32 --max-steps 50000000 \
     --input "srs\nlrs\nq\n"
```

## Build for KR232

Compile every unit with `--target=kr232 --far-calls`, compile `crt0.s`,
`libc_full.c` and `fileio_full.s`, then link with `--entry=_start`. The KR232
VM's console queue holds only 16 input bytes, so use `main_script.cpp` for a
long session and `--transcript` to read the console output.

```sh
for f in console geometry galaxy ship scanner weapons computer game main_script; do
    krc -c --target=kr232 --far-calls examples/startrekng/$f.cpp \
        -o build/startrekng/kr232/$f.o
done
krc -c --target=kr232 --far-calls targets/kr232/runtime/crt0.s \
    -o build/startrekng/kr232/crt0.o
krc -c --target=kr232 --far-calls targets/kr232/runtime/libc_full.c \
    -o build/startrekng/kr232/libc.o
krc -c --target=kr232 --far-calls targets/kr232/runtime/fileio_full.s \
    -o build/startrekng/kr232/fileio.o
krld --entry=_start build/startrekng/kr232/crt0.o \
     build/startrekng/kr232/console.o build/startrekng/kr232/geometry.o \
     build/startrekng/kr232/galaxy.o build/startrekng/kr232/ship.o \
     build/startrekng/kr232/scanner.o build/startrekng/kr232/weapons.o \
     build/startrekng/kr232/computer.o build/startrekng/kr232/game.o \
     build/startrekng/kr232/main_script.o \
     build/startrekng/kr232/libc.o build/startrekng/kr232/fileio.o \
     -o build/startrekng/kr232/startrekng_script.kr232
```

```sh
kr232 vm build/startrekng/kr232/startrekng_script.kr232 \
      --max-steps 200000000 --transcript
```

## Commands

`nav`, `srs`, `lrs`, `pha`, `tor`, `she`, `dam`, `com`, `xxx`; `q` quits.
Numeric prompts accept one number per line.

## Script mode

`Console::setScript` accepts up to 4096 bytes and replays it as the console.
`main_script.cpp` installs `script.hpp`, which scans, reports damage, raises
shields, runs all six computer reports, navigates, fires phasers and torpedoes,
and ends the mission.
