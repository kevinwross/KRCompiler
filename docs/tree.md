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
| `build/<preset>/kr232-images/` | the two kr232 containers the kr232 rows read | target build steps |
| `<prefix>/bin/` | installed `krc` and `krld` | `cmake --install` |
| `<prefix>/share/krc/sysroot/` | installed target sysroot and KR32 runtime | `cmake --install` |

Rules:

- A `.c` or `.s` file under `src/` that is meant for `krc` is a boundary violation.
- Host CMake never adds `targets/` or `sysroot/` files to a host executable.
- Tests and the build pass explicit output paths under `build/<preset>/`: most
  staging is under `stage/`, and the kr232 test containers are built in
  `build/<preset>/kr232-images/`. The source tree is not an output directory.
- `stage/sysroot` has the same layout as an installed sysroot, and
  `krc-toolchain-config.cmake` exports `KRC_SYSROOT` (the installed sysroot) and
  `KRC_KR32_RUNTIME_DIR` (its `kr32/lib`), so a consumer does not need this build
  tree.
- `cmake --install` also writes `bin/krc` and `bin/krld`; a host that runs the
  build chain uses those and the installed sysroot. The tools are not exported as
  CMake targets. `krc`'s default sysroot is its build-time path, so an installed
  tool is given `--sysroot <prefix>/share/krc/sysroot` (the package's
  `KRC_SYSROOT`).

The rationale and migration phases are in
[source-tree-reorganization.md](planning/source-tree-reorganization.md).
