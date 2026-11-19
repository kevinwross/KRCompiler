# 0051 — Allocate on the recorded machine form

**Status:** proposed 2026-11-19. Not accepted: it changes every `-O1` output, and that is a
decision for the owner, not for a pass.
**Date:** 2026-11-19.
**Relates to:** `D-049` (selection quality is lowering), [ADR 0049](0049-register-allocation-wiring.md),
`docs/optimize.md` §7 row **R1**, `src/backend/machine_ir.h`, `src/backend/register_allocator.cpp`.

## Context

The KR32 backend selects and allocates in one pass over the IR. `register_allocator.cpp`
keeps a value in a register only when **every use is in the block that defines it**, because
that is the strongest rule the emitter can honour without knowing the frame layout or the
argument layout: three attempts to widen it (cross-block allocation, frame-slot reuse twice)
each miscompiled, and the reasons were all the same — the emitter addresses slots and writes
`phi` copies at points the IR's operand list does not describe.

Since then the backend **records** what it emits. `emit_machine` builds a
`machine::Instruction` and prints it, so the representation and the text cannot drift, and the
recording is proven: a simple function and a realistic one record 100%, the corpus records
**5,474 of 5,546 instructions (98.7%)**, and `kr32/libc.c` is byte-identical at `-O1` after
every one of the eleven conversions. A new test round-trips every emitted instruction line
through the representation at both levels, which is what makes the coverage figure meaningful.

The measurement that motivates acting now (`docs/optimize.md` §3.5): at `-O1`
`kr32/libc.c` still spends **35.8% of its instructions on memory** and 40.5% reference a frame
slot, while **`MOV` rises from 1,345 at `-O0` to 5,727 at `-O1`** — the shuffling the
single-block allocator emits. `kr16/libc.c` is **63.6% frame-addressed** because it allocates
nothing at all.

## Decision

Allocate over the **recorded machine function** rather than over the IR, in a second phase:

1. `store_value`/`load_value` record a **virtual register per IR value** instead of the
   physical register the current allocator chose, so the recorded form carries data flow
   rather than a finished assignment.
2. A new allocator assigns the physical registers to those virtual registers, with liveness
   over the recorded blocks, and handles calls by saving and restoring what is live across
   them.
3. The text is printed from the **allocated** machine function, not from the strings the
   emitter builds today. The emitter's string building is deleted as each site's allocation
   path is proven.

## Consequences

- **Every `-O1` output changes.** That is the point of the decision and the reason it needs one:
  the byte-identical guard that has made the last eleven rounds safe cannot survive it. Its
  replacement is three guards, all objective:
  - the suite at both levels — 889 tests, 58 executed at `-O1` across all three tiers;
  - the recorded-versus-emitted equality, which still holds by construction because printing
    is one call site, so a conversion that loses an instruction fails a test;
  - `--dump-stats` instruction and frame-byte counts, which must improve and are checked in.
- `-O0` is **unchanged**, as ADR 0049 requires: no allocation at `-O0` means the recorded form
  is the printed form, and the `krc_optimization_levels` byte-identity assertion still holds.
- The three failed attempts become possible rather than risky: cross-block allocation, slot
  reuse and phi-copy coalescing all need the slot-and-argument model this representation
  carries. S4's measured **−28.6% frame bytes** is available to whoever takes it.
- The backend grows a second representation for as long as both paths exist. The migration has
  to be one-directional — each site moves to the allocated path and its string code is deleted
  in the same change — or the two will drift, which is the failure this project has paid for
  twice.
- KR16 is **not assumed to benefit**. ADR 0049 measured its two-operand ALU as eating the
  gain; the allocator lands at `-O1` and KR16's row is measured and reported separately.

## Alternatives rejected

- **Widen the existing single-block allocator again.** Three attempts, three miscompiles, and
  the audit named why: the emitter owns slot writes the IR does not describe. A fourth attempt
  would be the same bet.
- **Write a second selection pass beside the emitter.** Two implementations of one decision,
  which is how the `LOAD8U` substitution and the backwards `FCVT.W.S` name happened in a single
  representation; two would be worse.
- **Keep the text as the interface and optimise it.** The plan's §7 layer 4 allows a bounded
  text pass (P1, P2 landed that way), but a text pass cannot see liveness across blocks, which
  is where the 35.8% is.

## What this does not decide

- Whether the allocator is linear-scan or graph-colouring. Linear scan over the recorded blocks
  is the recommendation: the values are already in one linear order per block and the register
  file is eight wide.
- Whether `-O1` keeps the frame slot for a value it cannot allocate. It must, until the slot is
  proven dead — that is S4, and it stays a separate change with its own measurement.
