# Review — `docs/optimize.md` against `plan.md` §8.8

**Reviewed:** 2026-11-18. **Reviewer:** the session that implemented the KR16 backend fixes.
**Scope:** does the document's proposal fit the contract it proposes to amend, and what must be
answered before any of it is written. This reviews the document; it decides nothing.

**Verdict:** the analysis is sound and the ordering is defensible, but the proposal is **wider
than the contract it says it amends**, and its largest layer rests on a subsystem this
repository has deliberately left unwired. Four of the six open questions can be answered now;
two cannot until the register-allocator decision is made.

## Findings

1. **FIX-BEFORE — R1 depends on a subsystem recorded as deliberately unwired.** §7's table has
   R1 ("Machine IR + allocation") as the layer that removes frame traffic for temporaries, and
   §3's headroom figures assume it lands. `AGENTS.local.md` records the register allocator as
   *deliberately unwired*, and item 19 of the current list is that decision, still open. Until
   it is answered, R1 is unexecutable and every percentage that depends on it is a projection
   of work that cannot be started. The document should name this dependency rather than
   presenting R1 as the next step after E7.

2. **FIX-BEFORE — the pass list exceeds §8.8's definition of `-O1`.** §8.8 fixes `-O1` as
   *"mem2reg, constant folding, dead code elimination, common subexpression elimination, copy
   propagation"* plus invariants. §7 adds five **selection** changes (S1–S5), global CSE (E7),
   and two **text** passes (P1–P2), none of which §8.8 mentions, and §8.8's `-O0` says
   *"straightforward lowering"*, which S1–S5 arguably contradict. Q1 already says a `D-xxx` is
   needed; the stronger point is that until that entry exists, the list proposes work the
   contract does not authorise. Either amend §8.8 first, or mark S1–S5 and P1–P2 as `-O0`
   quality work outside the optimisation levels.

3. **ERRATA — §3 conflates cheaper lowering with removed work.** The headroom table measures at
   `-O0` shape and the document says honestly that the numbers will not survive the allocator.
   But S1–S5 *are* lowering, so part of that headroom is available at `-O0` without any pass at
   all, and mixing the two makes the table uninterpretable: a reader cannot tell which fraction
   is "the compiler emits worse code than necessary" and which is "the optimiser removes work".
   Two tables, or a column naming the layer, would fix it.

4. **ERRATA — Q3 option A contradicts E6.** E6 is mem2reg with `phi`, which §8.8 lists for
   `-O1`. mem2reg's whole effect is promoting named locals out of frame slots. Q3 recommends
   option A — *do not* promote named locals — which would make E6 a no-op for exactly the
   variables §8.8 says it should promote. One of the two has to move: either E6 is deferred
   with the debug-info cost stated, or Q3's recommendation is B.

5. **ERRATA — Q6 should gate the others, not sit last.** `--dump-stats` is what makes "no
   regression" enforceable, and the document says so. Without it every claim in §3 is a
   one-off script result that cannot be re-run by the next reader, which is the failure mode the
   session that wrote this review kept hitting: a measurement that cannot be reproduced is a
   claim. Land `--dump-stats` before the first pass.

## The six questions, with my answer where one is defensible

| Q | Answer |
|---|---|
| Q1 — becomes a plan change? | **Yes.** Findings 2 means it must: the proposal is wider than §8.8. |
| Q2 — structural or fast path? | **Structural**, per the document's own reasoning. The text-pass route buys speed with a representation you then keep. |
| Q3 — promote named locals? | **Cannot answer until finding 4 is resolved.** A and E6 are in direct conflict. |
| Q4 — does KR16 get the passes? | **Agree as written**: selection and frame-slot reuse yes, allocation only if measured to win, never by transferring KR32 numbers. KR16's metric is image bytes on a 64 KiB target, not speed. |
| Q5 — where does `-O` live, what default? | **Agree**: `-O0` default, `-O1` non-default until its gate, suite runs both. |
| Q6 — measure or write first? | **Measure first**, and specifically `--dump-stats` first — finding 5. |

## What this review does not claim

- It ran **no build** and measured nothing. Every statement about the document is a reading of
  it against §8.8 and `AGENTS.local.md`.
- It does not dispute §3's numbers; it disputes whether they can be attributed to the layers
  as presented.
- It does not decide Q3, Q1's `D-xxx` wording, or anything else that is an operator decision.
