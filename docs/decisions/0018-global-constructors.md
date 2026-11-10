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
  in declaration order, then evaluates any non-constant scalar/pointer global
  initializer and stores it into its global.
- The user's `main` is renamed `__krc_user_main`; a new external `main` wrapper
  calls `__krc_init_globals`, then `__krc_user_main`, then
  `__krc_destroy_globals`, and returns the user result.
- One internal `__krc_global_dtor_N` function per destructible object calls its
  destructor; `__krc_destroy_globals` calls them in reverse construction order.
  Destructible function-local statics are registered here too and run after the
  user `main` result is computed.
- The wrapper is emitted only when the translation unit has global
  constructors or destructors, so ordinary C and C++ programs keep their
  existing entry path.
- Global destruction happens after the user `main` result is computed but before
  the wrapper returns. Cross-translation-unit initialization order remains
  deferred.

## Consequences

- `Counter global(5)` is constructed before `main`; `global.get()` returns 5.
- A global destructor runs after user `main` and before the wrapper returns.
- The runtime entry symbol is still `main`, so the existing startup path needs
  no change.
- The wrapper approach is single-translation-unit only until the relocatable
  linker owns init arrays.
