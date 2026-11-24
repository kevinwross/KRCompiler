# A new VM tree, from this skeleton

Copy this directory to `../krNN`, rename `krNN` and `KRNN` throughout, and work
through `docs/new-target-guide.md` section 8. What is here:

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | the library, the executable, the `::core` export, the gate target |
| `CMakePresets.json` | the two presets every tree has, named as the family gate expects |
| `cmake/KrGuest.cmake` | the one shared answer to "where is krc". Copy it unchanged |
| `scripts/gate-windows.ps1` | the gate a family run invokes, sized from `FAMILY_JOBS` |
| `AGENTS.local.md` | the standing orders a new tree starts from |

What the skeleton does not contain, because it is the work rather than the shape:
the machine, its assembler, its encoder, its guest runtime under `targets/krNN/`,
and its fixtures. The guide's order is the order to add them in, and each step ends
with the check that says it is done.

Two rules the skeleton encodes rather than describes:

- **The compiler is found by `cmake/KrGuest.cmake`**, not by a hardcoded path and not
  by a second search. A tree that cannot find its compiler fails at configure.
- **The guest side is compiled by `krc --target=krNN`**, never by the host compiler.
  `src/` and `srcNN/` are separate trees for that reason, and a boundary check keeps
  them separate.
