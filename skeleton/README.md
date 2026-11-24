# A new VM tree, from this skeleton

Copy this directory to `../krNN` and rename three things: every `krNN` and `KRNN` in
the file *contents*, the `include/krNN/` directory name, and the file names that carry
the target. The recipe, from a shell in the repositories directory:

    cp -r krcompiler/skeleton krNN
    cd krNN
    git mv include/krNN include/kr64            # or the new target's name
    # replace krNN -> kr64 and KRNN -> KR64 in every file, then commit

Then configure with the compiler named explicitly, or with a sibling `krcompiler`
build in place, or with `krc` on `PATH`:

    cmake --preset linux-gcc-debug -DKRG_KRC=<krc> -DKRG_KRLD=<krld>
    cmake --build --preset linux-gcc-debug
    ctest --preset linux-gcc-debug

That sequence is verified: an instantiation of this skeleton configures, builds, runs
`kr64.exe` and passes its one row. What is here:

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | the library, the executable, the `::core` export, the gate target |
| `CMakePresets.json` | the two presets every tree has, named as the family gate expects |
| `cmake/KrGuest.cmake` | the one shared answer to "where is krc". Copy it unchanged |
| `include/krNN/machine.h` | the `::core` surface a consumer links: reset, step, pc |
| `src/machine.cpp`, `src/main.cpp` | placeholder bodies, replaced by the real machine |
| `tests/` | one row, so the guest-tests option means something the first time it is on |
| `scripts/gate-windows.ps1` | the gate a family run invokes, sized from `FAMILY_JOBS` |
| `AGENTS.local.md` | the standing orders a new tree starts from |

What the skeleton does not contain, because it is the work rather than the shape: the
machine, its assembler, its encoder, its guest runtime under `targets/krNN/`, and its
fixtures. `docs/new-target-guide.md` in `krcompiler` is the order to add them in, and
each of its steps ends with the check that says it is done.

Two rules the skeleton encodes rather than describes:

- **The compiler is found by `cmake/KrGuest.cmake`**, not by a hardcoded path and not
  by a second search. A tree that cannot find its compiler fails at configure.
- **The guest side is compiled by `krc --target=krNN`**, never by the host compiler.
  `src/` and `srcNN/` are separate trees for that reason.
