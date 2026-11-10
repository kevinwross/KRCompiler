# 0018 — Global object construction at program entry

**Status:** Accepted

## Context

A namespace-scope class object with a constructor had no initialization path:
its storage was emitted as zero-initialized globals, and the constructor never
ran. The runtime entry point calls `main` directly, so the compiler needs a
place to run initialization before user code.

## Decision

- A global whose initializer calls a constructor is recorded and given
  zero-initialized storage.
- One internal `__krc_global_ctor_N` function per such object calls the selected
  constructor with its arguments.
- One internal `__krc_init_globals` function calls every constructor function
  in declaration order.
- The user's `main` is renamed `__krc_user_main`; a new external `main` wrapper
  calls `__krc_init_globals`, then `__krc_user_main`, and returns its result.
- The wrapper is emitted only when the translation unit has global
  constructors, so ordinary C and C++ programs keep their existing entry path.
- Global destruction, cross-translation-unit initialization order, and
  non-constant global initializers remain deferred.

## Consequences

- `Counter global(5)` is constructed before `main`; `global.get()` returns 5.
- The runtime entry symbol is still `main`, so the existing startup path needs
  no change.
- The wrapper approach is single-translation-unit only until the relocatable
  linker owns init arrays.
