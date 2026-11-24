# AGENTS.local.md

This tree's own layer: what is true of this VM and not of every repository. The
universal rules live in `AGENTS.md`; anything here overrides them for this tree.

**Keep this file current.** When a condition named here changes, update it in the
same change. A stale instruction is worse than a missing one: it is believed.

## Definition of done

- [ ] `pwsh -File scripts/gate-windows.ps1` (or the preset directly) is green
- [ ] A change to the machine has a row that fails without it
- [ ] The guest side is still compiled by `krc --target=krNN`, never by the host compiler
- [ ] No secrets, debug output, or unrelated edits in the diff

## Environment

**Build entry point.** `scripts/gate-windows.ps1`, which configures, builds and tests
with the job count `FAMILY_JOBS` supplies.

**Toolchain.** `krc` and `krld` come from the installed `krc-toolchain` package,
resolved by `cmake/KrGuest.cmake`: `KRG_KRC`/`KRG_KRLD` first, then the sibling
`krcompiler` build trees, then `PATH`. A tree that cannot find them fails at
configure, by design.

**Guest tests.** `KRNN_BUILD_GUEST_TESTS=ON` makes `krc` and `krld` required. With it
OFF no guest row is registered and the configure says so, because a suite that
registers nothing reports success.

## Architecture boundaries

- `src/` is host code, built by the host compiler.
- `srcNN/` is guest code, compiled by `krc --target=krNN`. Nothing in the host build
  includes it.
- `targets/krNN/` holds the runtime the compiler links into a guest image: `crt0.s`,
  the syscall glue and the guest headers. The compiler owns those paths; this tree
  does not name them in a script.

## Known traps

- A guest object built by an older compiler is still valid input, so a stale build
  directory compiles a mixture. Delete it after a compiler change rather than
  trusting an incremental build.
- The `.gitattributes` must pin `* text=auto eol=lf`; without it a Windows checkout
  and a WSL checkout differ and the drift rows compare different bytes.
