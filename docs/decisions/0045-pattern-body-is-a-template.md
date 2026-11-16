# 0045 — A pattern body is a template, not a program

- Status: proposed
- Date: 2026-11-15
- Decision: a template whose parameter list contains a pack is never served by
  the int-substituted pattern; its body is analysed per instance only
- Amends: 0031 (int substitution), 0042 (substitution is a fallback)
- Scope: front end

## Context

ADR 0031 substitutes `int` for a template's type parameters and analyses the
substituted definition as an ordinary program, so a call whose arguments are all
`int` resolves to the definition itself without an instantiation. ADR 0042 then
established that this substitution is a **fallback, not an authority** — a value
of `int` on an all-int call is not evidence that the parameter is `int`.

A pack breaks the fallback in a way ADR 0031 did not consider.
`template <typename... Ts> struct Pack { Pack() : count(sizeof...(Ts)) {} };`
is analysed as a substituted program where `Ts` is `int` — one `int`, so
`sizeof...(Ts)` has no arity to fold to, and the pattern pass reports *"sizeof...
names something that is not a parameter pack"* and fails the program. No
instance is ever reached, so the correctly-bound arity is never consulted. Three
attempts to fix the instantiation side could not help, because the diagnostic is
emitted before an instance exists.

The general shape, which is the reason this needs a decision rather than a patch:

> A template body is analysed to find type errors, but a diagnostic that depends
> on an instance's bindings is not a property of the program. Emitting it at
> pattern level reports a fact about a template as a fact about a program.

## Decision

- **A template whose parameter list contains a pack is not served by the
  int-substituted pattern.** The pack can absorb any number of arguments, so
  there is no substitution that represents it: one `int` is not the pack, and
  zero `int`s are not the pack either. Such a template is instantiated per call,
  as a template without a fallback.
- **This is a narrowing of ADR 0031’s scope, not a repeal.** The substitution
  remains the fallback for a template whose parameters are all fixed, where one
  `int` per parameter is at least a program.
- **A pack's arity is unknown in a pattern and known in an instance.** Code that
  asks for it before an instance exists has asked too early; it must be given a
  way to say so rather than folding to a number. Folding to zero is wrong and
  tempting, because zero is a legal arity.
- **The arity recorded for an instance travels with the instance**, not with a
  transient scope: a member body is analysed inside `record_type`, after which
  any instantiation-scoped map is gone. Recording it on the instance's own
  cloned subtree has the same shape as ADR 0043's rule that a label's position
  belongs in the line sequence it indexes.

## Consequences

- `Pack<int, char>` and `Pack<int, char, long>` become two instances with
  arities 2 and 1, and `sizeof...(Ts)` folds correctly in each.
- A template with a pack is never resolved by the all-int fallback, so an
  all-int call to one costs an instantiation it previously would not have. That
  is the correct trade: the previous behaviour did not compile at all.
- `tuple`, which is a class template whose parameter list is a pack, becomes
  expressible without further front-end work beyond the pack-absorbing
  instantiation this decision assumes.
- A pattern-level diagnostic that depends on an instance remains possible for
  templates outside this decision’s scope, so this does not create a general
  "defer this diagnostic" mechanism — deliberately, since that mechanism would
  be needed by exactly one caller.
