# Priority list — finishing `tuple` and the `ir_size_of` defect

**Written:** 2026-11-16T12:59:48-08:00 (this session, after the pack-expansion fix `cc0fa8c`).
**Scope:** the five items below, in the order recommended in §3. Each item states the
evidence it rests on, the change it implies, what verifies it, and its consequence
label: **BLOCKS** (the next step cannot proceed), **FIX-BEFORE** (workable now, must be
resolved before the milestone closes), **ERRATA** (correctness or clarity, no schedule
impact).

Every claim here is measured this session unless it is marked *recorded*. Probes are
`build/corpus/tp1.cpp` … `tp7.cpp`, compiled with
`build\windows-msvc-debug\host\bin\Debug\krc.exe`.

---

## 1. Ground truth: what a `tuple` can and cannot express today

| Probe | Shape | Result |
|---|---|---|
| `tp1` | specialization ctor `Tup(T first, R... rest) : head(first), tail(rest...)` | **compiles** |
| `tp3` | member accessors, pack-free signatures, `sizeof...(R)` in a body | **compiles, right values** |
| `tp7` | `template <typename T, typename... R> int arity_of(Tup<T, R...>&)`, pack-free body | **compiles — but calls the stub pattern** |
| `tp2` / `tp4` | same, body uses `value.head`, deduced or spelled arguments | `member access on a non-record value`, in `-fsyntax-only` **and** `-c` |
| `tp5` | same, body uses `sizeof...(R)` | `sizeof... names something that is not a parameter pack` |
| `tp6` | partial specialization on a non-type argument (`element<0, T>`) | `expected expression` at the use site |

`tp7` is the important one, and its IR is the whole story:

```
define weak i32 @arity_of(ptr<i32> %value) { … }
%call = call i32 @arity_of(ptr<i32> %bitcast)
```

No instance is created (`@arity_of`, not `@arity_of__int__char__1`), the parameter is
the **`int` stub**, and the argument is bitcast to it. It compiles only because the
body never uses `value`; anything that does fails (`tp2`). That is ADR 0031's int
substitution acting as an authority instead of a fallback, which ADR 0045 already
ruled out for a template whose parameters include a pack.

---

## 2. The five items

### Item 1 — **BLOCKS**. A call with a pack-carrying template-id parameter never instantiates

The call never reaches an instance: `tp7` emits `@arity_of` and a call to it, with
nothing named `@arity_of__int__char__1` anywhere in the module. Deduction must match
the parameter's template-id against the argument's record instance — arity, element
types, and the pack's tail — and `instantiate_class_template` already computes all
three, so the work is matching, not layout. *Recorded* precedent for the failure mode:
ADR 0042's callable case ends in `undefined symbol` for the same reason; here the
pattern **is** emitted, so the symptom is a silently wrong call rather than a link
error — worse.

**Verifies:** `tp7` emits `@arity_of__int__char__1`, not `@arity_of`; a probe that
reads `value.head` through the instance returns the stored value.

### Item 2 — **BLOCKS**. The pattern body of such a function is still analysed

Measured from `--dump-ast`: the function template's definition carries
`[template-param:T] [template-param:R] [template-pack-param:R]`, and its parameter's
specifier carries `[template-arg:T] [template-arg:R...]` — **no `pack` label
anywhere**. The skip in `analyze_function_definition` looks only for a `pack` label on
a nested child (`src/frontend/sema.cpp`, the `pack_pattern` loop), so it never fires
for this shape, while `value_dependent_pattern` beside it already reads the same
`definition.labels` list for `template-value-param:`. The fix is likely one condition
in that pair, not a new pass — which is also why the earlier attempt at "skipping a
pack template's member bodies" did nothing: it was aimed at class members, and the
label it needed to see is on the function definition.

It fails in both `-fsyntax-only` and `-c`, so this is analysis, not IR generation.

**Land items 1 and 2 together.** Either alone is worse than neither: skipping the body
without deduction leaves the pattern emitted and called (`tp7`'s bug); deduction
without the skip still rejects the program at pattern analysis. The previous session
paid for this exact lesson with the value-parameter pair, where each half made the
other necessary and testing them separately looked like two failures.

**Verifies:** `tp2` and `tp5` compile, link and exit 42 on KR16; `tp7`'s symbol is the
instance.

### Item 3 — **FIX-BEFORE**. `get<I>` is not reachable; decide the API before writing a header

Both routes are measured absent: a member template is rejected at parse
(`expected declarator`), and a partial specialisation on a non-type argument is
rejected at its use site (`tp6`). So a header can offer `t.head` / `t.tail.head`,
member accessors (`tp3`), and — once items 1 and 2 land — free
`tuple_head` / `tuple_tail`. It **cannot** offer `get<I>` without new front-end work
(one of those two features), which is a larger item than this list.

**Decision required from the operator:** is a *named* tuple (`tuple_head`, member
accessors) enough for M10, or is `get<I>` in scope? Everything below is unchanged
either way; only the size of the item moves.

### Item 4 — **FIX-BEFORE**. KR16 rejects aggregate returns, so no by-value tuple factory

`make_tuple(...)` returning `tuple<T, R...>` is an aggregate return, which KR16
refuses. `sysroot/include/memory:11-13` states the same limit for `make_unique`:
"KR16 does not support aggregate returns, so on that target make_unique does not
compile -- the same limit that stops any function returning a record by value." Two
outcomes: (a) construct in place — `tuple<int, char> p(41, 'x');` works today per
`tp1` — and record the KR32-only factory; or (b) implement aggregate returns on KR16,
which is an ABI item needing an ADR.

**Verifies:** whichever is chosen, a fixture asserts the supported form and says in a
comment what is not asserted and why.

### Item 5 — **FIX-BEFORE**. `CIRGen::ir_size_of` is a fourth, wrong layout rule

`ir_size_of`'s `Struct` case gives every non-float field 4-byte alignment
(`src/irgen/c_irgen.cpp`, the `field_alignment` lambda). There are **four** layout
rules in the tree and `ir_size_of` is the only one that disagrees:

| Rule | Where | Struct layout |
|---|---|---|
| `Sema::size_of` | `src/frontend/sema.cpp` | per-member alignment (authoritative for `sizeof(type)`) |
| `Kr32Backend::size_of` / `align_of` | `src/backend/kr32_backend.cpp:114,155` | per-field alignment |
| `type_size` | `src/backend/kr16_backend.cpp:45` | 2-byte alignment (correct for KR16) |
| `CIRGen::ir_size_of` | `src/irgen/c_irgen.cpp:10549` | **4 for every non-float field** |

Reproduction in plain C, no template: `struct In { char a; }; struct Out { In inner; };`
— `sizeof(outer.inner)` returns **4**, Sema says **1**.

Blast radius, from its call sites: `new T` size, `new T[n]` memset length, `p - q`
scaling for record pointers, exception allocation size, `sizeof(<expression>)`, and —
the one that raises this above an error of arithmetic — `is_memory_class_argument`
(`src/irgen/c_irgen.cpp:1411-1421`), which uses the same number to decide whether an
aggregate parameter is passed as a hidden pointer (`size > 16`), in registers
(`size <= 8`), or by one of two FP paths. So the wrong size can change a **calling
convention**, and that is measured rather than deduced: a struct of three `char`s is
**12** by `ir_size_of` and **3** by Sema (`build/corpus/size3.cpp`), so it lands on
opposite sides of the eight-byte register rule. Caller and callee classify identically,
so no program is miscompiled against itself — but the classification deviates from what
`docs/abi.md` describes, and correcting it changes which aggregates are memory-class,
which changes emitted images.

**Two options, and they are not exclusive:**
- **A (small in code, not in process):** make `ir_size_of`'s `Struct`/`Union` cases use
  per-field alignment derived from the IR kind, with pointers from
  `profile_.pointer_bits` (ADR 0037's rule). ~15 lines. Because it is ABI-visible it
  needs an ADR amendment, the four-row matrix, and a determinism run, not just the
  inner loop.
- **B (the one-home fix):** move IR-level layout into the target descriptor — a shared
  service both IRGen and the backends may use — and delete the three copies. Touches
  the target description, so it needs an ADR of its own.

Recommendation: **A now**, with B recorded as an ADR. A removes the wrong answers and
is verifiable this milestone; B is the correct home and should be done when the next
layout change already touches both backends, not as a refactor for its own sake.

**Verifies A:** `sizeof(outer.inner)` is 1 on both targets; a fixture pins `sizeof` of
a nested small record; a fixture pins the parameter classification of a small aggregate
against `docs/abi.md`; the suite, the four-row matrix, and the determinism tests stay
green — the change moves constants and can move a calling convention, so a determinism
failure would mean the IR is not stable.

---

## 3. Recommended order

1. **Items 1 + 2 as one change.** This is the whole of what `tuple` is blocked on, and
   it is the objective; nothing in item 5 blocks it. Start with `tp7`'s IR, because it
   is the cheapest assertion in the list: one symbol name.
2. **Item 5 (A), on its own.** Independent of the compiler work, and worth taking
   before the tuple fixtures are written: any tuple test that checks a size through a
   member expression would otherwise measure `ir_size_of`, not the tuple. It carries
   the matrix and an ADR amendment because it can move a calling convention.
3. **Item 4's decision, then the header.** In-place construction needs no new work.
4. **Item 3's decision.** Only if `get<I>` is in scope; otherwise record it as the
   limitation it is and close M10's tuple item on the named API.

The first two can proceed in either order or in parallel; if only one can be taken,
items 1 + 2 close the objective and item 5 closes a correctness defect nobody is
blocked on.

---

## 4. Process constraints that bind this work

- Items 1–2 touch `src/frontend/` and are inside the boundary list's exclusions, so
  the inner loop applies: `windows-msvc-debug` build and `ctest`.
- Item 5 touches `src/irgen/`, which is also outside the boundary list, **but** it can
  change an aggregate's memory class, and that is an ABI behaviour. Treat it as a
  boundary change: ADR entry, the four-row matrix, determinism run. Option B touches
  the target descriptor, which is in the list without argument.
- Commits batch at five functional changes or 180 lines of source and tests; the
  current published state is `origin/master` = `352230a`, and `cc0fa8c` (the
  pack-expansion fix) is **local and unpushed** because a tier-2 change waits for a
  host gate.
- Every item lands with a fixture, and the fixture says in a comment what it does not
  assert and why. Three headers and one fixture have already been removed rather than
  shipped advertising more than they deliver.
- Determinism is byte-exact: items 1, 2 and 5 change emitted IR, so
  `krc_determinism_*` must be run, not assumed.
