# 0052 — Live ranges on KDBG `Variable` records

**Status:** proposed 2026-11-19. Not accepted, and not implementable inside this repository alone:
the reader is the sibling `KR32` VM, so accepting it needs the format change and the two readers
moving together.
**Date:** 2026-11-19.
**Relates to:** [ADR 0009](0009-kdbg-v1-freeze.md), `docs/symbols.md`, `docs/plan-o1-debugger.md`
W4/W5, `docs/optimize.md` §7 row **S4**, `src/backend/machine_liveness.h`.

## Context

A `Variable` record carries function, name, frame offset, size, type. Nothing in it says *where in
the function the variable is live*.

At `-O0` that is enough: one slot per variable for the whole function, so "the slot" and "the
variable" are the same thing from entry to exit. Every source variable keeps its slot at `-O1` as
well, for two independent and measured reasons — both promotion passes refuse the IR value named
`<variable>.addr`, and frame-slot omission drops a slot only for a value the allocator keeps in a
register, which a source variable never is (`docs/plan-o1-debugger.md`). So a debugger reading an
`-O1` image today is not wrong.

It becomes wrong the first time a slot is reused. S4 is measured at **−28.6% frame bytes** and has
been implemented and reverted twice, breaking 54 then 52 executed tests; the reuse itself was not
what broke them, but even once that is fixed a debugger would read whatever now occupies a reused
slot, with nothing to tell it that the variable is gone. The same applies to promoting a named
variable, which both passes refuse for this reason.

The format is frozen ([ADR 0009](0009-kdbg-v1-freeze.md)). A missing range is therefore not a
missing feature but a **silent wrong answer**: the debugger asks for `x` and is handed the bytes that
replaced it.

## Decision

Give every `Variable` record a **live range**: a start PC and an end PC, half-open, so the variable
is available at `start` and not at `end`. Outside the range a debugger reports the variable as
unavailable instead of reading its slot.

- The image's **minor version becomes 1.1** (offset 6 of the header), and a 1.0 reader **rejects** a
  1.1 image rather than skipping the extended payload. Ignoring a range is the failure being fixed —
  it turns "unavailable" into "always available". This is the same principle as the line policy in
  `docs/symbols.md`: an answer that cannot be given is refused, not approximated.
- The two addresses are stored as `u32`, like every other record, which already stores an address as
  `u32` on both targets rather than at the target's pointer width.
- The range is computed from the values the backend already records: `machine_liveness` derives live
  ranges from the recorded data flow, so this is not a second liveness implementation.

## Consequences

- **Every variable record grows by eight bytes.** The tier-1 `printf` image at `-O1` carries 693
  variables, so about 5.5 KB on a 1.28 MB image.
- **The VM's reader has to change, and it lives in the sibling `KR32` repository.** This repository
  can emit the ranges and freeze their layout; it cannot make a debugger honour them. That is why
  this is proposed rather than accepted: half of the change is out of reach from here.
- **S4 becomes worth a third attempt.** The two failures were about which slots the emitter writes;
  ranges address the other half of the problem, which slot means which variable over time.
- **W5 (`<optimized out>`) becomes expressible** — outside its range a variable is reported as
  optimized out rather than as a stale value.
- The `.debug_info` bridge's `V` record gains the two fields, because `krld` builds KDBG from it.

## Alternatives rejected

- **Infer liveness from the line table.** The line table maps addresses to source lines and says
  nothing about frame slots, so it cannot tell two variables that share an offset apart.
- **Leave the format alone and forbid slot reuse for named variables.** That is the status quo, and
  it costs S4's measured −28.6%.
- **A separate `.kdbg_range` section keyed by an id.** The record already identifies its variable; a
  second section adds an id and a join and no information.

## What this does not decide

- **Disjoint ranges for one variable.** v1.1 carries one interval. A variable live in two separate
  intervals is either two records or a later list form; not decided here.
- **How a variable that is never in memory reports.** If a named variable is ever promoted, its range
  would be empty or absent, and how that is spelled is W5's question.
- **Field positions.** Appending after the existing fields is the recommendation; the offsets are
  fixed in `docs/symbols.md` when the ADR is accepted, and are deliberately not written down before
  then.
