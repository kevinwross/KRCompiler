# Repository tree contract

Two roots define what is host code and what is target code.

| Path | Contains | Built by |
|---|---|---|
| `src/`, `include/` | host compiler, linker, assembler, IR, tools | MSVC/GCC/Clang |
| `targets/`, `sysroot/` | target source, headers, prebuilt libraries | `krc`/`krld` (or not built at all) |
| `tests/unit/` | host C++ doctest and compiler unit tests | MSVC/GCC/Clang |
| `tests/fixtures/` | target fixtures and golden expected output | `krc`/`krld` through CMake |
| `build/<preset>/host/` | host binaries and libraries | host build |
| `build/<preset>/stage/` | install-shaped target sysroot and test images | target build steps |

Rules:

- A `.c` or `.s` file under `src/` that is meant for `krc` is a boundary violation.
- Host CMake never adds `targets/` or `sysroot/` files to a host executable.
- Tests and the build pass explicit output paths under `build/<preset>/stage/`; the source
  tree is not an output directory.
- `stage/sysroot` has the same layout as an installed sysroot.

The rationale and migration phases are in
[source-tree-reorganization.md](planning/source-tree-reorganization.md).
