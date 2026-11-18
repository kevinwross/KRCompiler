# 0049 — Wiring the register allocator

**Status:** proposed. **Awaiting operator review before any implementation.**
**Date:** 2026-11-18.
**Relates to:** `D-049` (optimisation contract), [ADR 0016](0016-kr16-scalar-abi.md),
`src/backend/register_allocator.cpp`.

## Context

`src/backend/register_allocator.cpp` exists and is **deliberately unwired**: no backend calls it.
Frame slots are assigned for every value, so temporaries that could live in a register are stored and
reloaded instead. That was a reasonable place to stop while the compiler was being made correct, and it is
recorded as a deliberate decision rather than an oversight.

It is now the gate on the optimiser work. `docs/optimize.md`'s largest layer — R1, "machine IR and
allocation" — cannot start without it, and the headroom figures in that document's §3 assume it lands.
The operator has approved wiring it (decision 19).

Two constraints from the contract:

- `D-049` makes `--dump-stats` a **precondition** for any optimiser pass, and this is a pass in the sense
  that matters: it changes which values live in registers, so it changes what the programme computes only
  if it is wrong, and changes performance either way.
- `plan.md` §8.8 puts `-O0` at "all variables in frame slots; full debug info". Allocation must therefore
  be **absent at `-O0`**, not merely degraded there.

## Decision

Wire the allocator as follows.

1. **It is an `-O1` behaviour only.** At `-O0` every value keeps its frame slot and the emitted code is
   byte-identical to today's. A test asserts that: the same source compiled at `-O0` before and after this
   change must produce identical bytes, and that comparison is the guard against silent `-O0` drift.
2. **`-O1` is not the default.** `-O0` remains the default until the optimiser gate (OG5 in
   `docs/optimize.md`) is met. Nothing in the test suite's default configuration changes behaviour.
3. **It is measured before it is trusted.** `--dump-stats` lands first (`D-049`), and the allocator's effect
   is recorded as a before/after size and tick comparison from `benchmarks/`, per target. A change that
   makes code larger on KR16 — where the metric is image bytes on a 64 KiB target — is a regression
   regardless of what it does for speed.
4. **KR16 is not assumed to benefit.** Its register file is five registers with `R4`/`R5` as frame and stack
   pointers, so the allocator has less to work with than on KR32. It is enabled there only if the
   measurement shows a win; otherwise KR16 keeps frame slots at `-O1` too, and the ADR records that
   asymmetry rather than pretending the targets are alike.
5. **Correctness is the whole risk.** An allocator that mis-handles liveness produces wrong values, not slow
   ones, so the existing 828-test suite must pass at `-O1` as well as `-O0` before this is considered done —
   and the suite is run at `-O1` in CI from the first commit that enables it.

## Alternatives rejected

**Wire it and make it the default.** Faster to see a win, but it would ship an unmeasured change to every
user and put the `-O0` byte-identity guarantee at risk. The contract says default is a decision, not a
side-effect of implementation.

**Wire it only for KR32.** Superficially safer, but it makes the two backends diverge in what `-O1` means,
which is the kind of difference that produces "works on one target" bug reports. If KR16 does not benefit,
that is recorded as a measurement in point 4, not as a silent divergence.

**Skip allocation and take the text-level peepholes first.** Cheaper, and `docs/optimize.md` lists them, but
the review's finding stands: they are lowering quality and belong at `-O0`, so they do not substitute for
allocation in the `-O1` layer.

## Consequences

- An ABI-visible change to generated code at `-O1`. Object and image bytes differ from today at `-O1` and are
  identical at `-O0`; the `-O0` identity is asserted, not assumed.
- `--dump-stats` must exist and the benchmark runners must work **before** the first implementation commit —
  the size layer works today, and the tick layer does not, which is an open gap this ADR depends on.
- The `-O0`/`-O1` split becomes a real behavioural fork in the test suite: every configuration runs the suite
  twice, which roughly doubles test time on the four routine rows.
- If allocation is later found to regress KR16's image size, point 4's asymmetry is the recorded outcome
  rather than an unplanned divergence.
