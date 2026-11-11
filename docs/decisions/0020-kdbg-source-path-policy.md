# 0020 — KDBG source-path policy

**Status:** Accepted

**Date:** 2026-11-11

## Context

KDBG v1 records source paths in its `.kdbg_src` section. The compiler bridge
records the path by which the source was opened, which is usually a host
absolute path. That violates the portable-output rule in `plan.md` D-035 and
makes a debug image depend on the machine that produced it.

KDBG v1 is frozen: its header, section directory, record encoding, and string
table cannot gain a new mandatory field without a version break. The open item
asks for a source-root decision before debug-path work.

## Decision

- KDBG v1 keeps its record layout; there is no new mandatory table.
- `krld --source-root=DIR` rewrites every `Source` path under `DIR` to a
  `/`-separated path relative to `DIR` before encoding KDBG. Paths outside
  `DIR` are copied unchanged.
- Without `--source-root`, `krld` preserves the recorded path. This keeps
  existing images and debugger sessions working.
- The build passes the project root for its KR32 runtime links, so repository
  sources appear as `tests/fixtures/c/hello.c`, `targets/...`, and so on.
- Relative paths are resolved by the debugger against the image or a configured
  source root. A KDBG-side root table and a v1.1 path-remap record are deferred;
  they would require a format version decision.
- `--source-root` is a link-time transformation, not a compiler option, because
  KDBG is owned by `krld` and the linker already owns final addresses.

## Consequences

- Project sources produce deterministic, machine-independent KDBG images when
  the root is supplied.
- External headers outside the root keep their recorded paths; a future root
  table can address those without changing existing v1 images.
- `krc --dump-symbols` shows relative paths for rooted links.
- No KDBG version bump, no new record, and no change to the KRO object format.

## Alternatives considered

- **Add a source-root table to KDBG v1.** Rejected: v1 is frozen and a new
  mandatory section breaks readers that validate the known section set.
- **Normalize in the compiler front end.** Rejected: the linker owns debug
  emission and the same object may be linked with a different root.
- **Store only basenames.** Rejected: collisions between `src/a.c` and
  `tests/a.c` would make the debug source list ambiguous.
