# 0038 — KR32 has exceptions; the descriptor said otherwise

- Status: accepted
- Date: 2026-11-14
- Decision: `kKr32.has_eh` is true
- Scope: target description

## Context

`src/target/target.cpp` gave `kr32` `has_eh = false`, and `kr16` the same. That
looked harmless until a change gated cleanup landing pads on it: a target without
exceptions has nothing to unwind into, so `note_scope_cleanup` stopped creating
pads, and the KR16 backend stopped being handed an `invoke` it cannot lower.

The KR32 side of that gate was wrong. ADR 0026 and ADR 0027 build exception
handling on KR32 — `invoke`, `landingpad`, chained `_Unwind_Resume`, and the
`krc_run_exception_*` fixtures — and `src/backend/kr32_backend.cpp` lowers all of
it. With `has_eh` false, every KR32 cleanup pad vanished, and
`krc_run_exception_cleanup` failed: a guard's destructor no longer ran while the
exception propagated.

The failure could not be seen until the KR32 executed tests could be run at all,
which took a private copy of the VM (see `implementation.md`, 2026-11-14).

## Decision

`kKr32.has_eh` is true. `kKr16.has_eh` stays false, and that remains the reason a
KR16 translation unit is not given cleanup pads: its backend rejects `invoke`.

## Consequences

- KR32 exception cleanup works again: destructors run while an exception
  propagates, and `krc_run_exception_cleanup` passes.
- A profile's capability flags are load-bearing: a wrong bit silently changes
  code generation rather than failing. The KR32 executed tests are the check, and
  they now run.
- `kKr32.has_rtti` is still false while `typeid` and `dynamic_cast` work on KR32
  and `krc_link_rtti` passes. Nothing currently reads that flag, so it is left
  alone here and recorded as a discrepancy rather than changed in the same
  commit.
