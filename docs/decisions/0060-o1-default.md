# 0060 — `-O1` is the default optimisation level

**Status:** accepted 2026-11-24.
**Date:** 2026-11-24.
**Decision:** `D-052`.
**Relates to:** `D-049` (optimisation contract), [ADR 0049](0049-register-allocation-wiring.md),
[ADR 0051](0051-allocate-on-machine-form.md), `docs/optimize.md`.

## Context

ADR 0049 wired the register allocator at `-O1` and deliberately left `-O1` non-default until the
optimiser gate was met: a miscompiled optimiser would otherwise become the path every user gets.
That gate exposed three defects, all now fixed and covered by tests.

- The `-O1` runtime miscompiled `lldiv`. A 64-bit `sdiv`/`srem` lowers to a helper call, but
  `keep_local_allocations` treated only IR `Call`/`Invoke` as clobbering registers, so it kept the
  result address in a register across `CALL __krc_sdivdi3`. The filter now takes the target's
  call-lowering instructions as `call_like`. It also allocates only the caller-saved `R12`-`R15`;
  the previous `R8`-`R15` set included callee-saved registers and `R11`, the thread pointer, which
  the backend does not save.
- A `phi` of `f64`, `i64`, or aggregate type copied one word. Phi copies now use the floating-point
  or wide path, and copy aggregates word by word.
- A peephole-removed branch could leave a KDBG line record at a function's end address, outside the
  half-open function range. `attach_kdbg` now drops a line that belongs to no function.

With those fixed, a build with no `-O` flag, the staged KR32 runtime, and the full suite all work at
`-O1`: **991 of 991 tests pass** with `-O1` as the default.

KR216 and KR232 do not lower `phi`, so `promote_cross_block_allocas` and `split_critical_edges`
still run only for KR32 and KR16. Their other `-O1` passes run as before; they keep the alloca round
trip until their backends gain phi lowering.

## Decision

A build with no `-O` flag is `-O1`. `-O0` remains available as an explicit level. Both the public
`toolchain::Options::optimize` default and the `krc` CLI local default are `1`, and the level
identity test asserts that no flag equals `-O1`, that `-O1` is deterministic, and that `-O0` differs.

## Consequences

- Every target's default build uses the optimiser, including the staged runtimes and the target
  artifacts the build system produces.
- `-O0` stays the debugging and comparison level, and its output is unchanged for a given source.
- KR216 and KR232 run the subset of `-O1` that does not create `phi`; the asymmetry is explicit in
  `src/toolchain/toolchain.cpp` rather than a silent difference in what `-O1` means.
- The three fixed defects are pinned by `krc_run_div_and_vsprintf_o1_runtime`,
  `krc_run_template_functions`, and `krc_debug_invariants_o1`.
- A debug line on an instruction the peephole removes is lost. That line pointed at a function's end
  address, where no breakpoint can be placed; no executable byte changes.

## Alternatives rejected

- **Keep `-O0` as the default.** It was only necessary while the optimiser miscompiled the runtime;
  keeping it now would leave the optimiser unproven on the path every user takes.
- **A per-target default.** Two contracts to document, test, and explain in build scripts, and the
  level-identity test would have to become target-dependent for no measurable benefit.
- **Make `-O1` the default before fixing the runtime.** This was already tried and recorded: it
  makes a miscompiled runtime the normal path, which is the outcome this ADR exists to prevent.
