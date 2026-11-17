# Handoff — KRCompiler

> **Status: superseded, 2026-11-16T16:13:04-08:00.** §1–§7 below describe the state
> *before* the session that closed the five-item goal: they say the suite is 679,
> that one commit is unpushed, and that `tuple` is open. All three are now false.
> Read **Session close** at the end of this file first.

**Written:** 2026-11-16T10:59:51-08:00, at the close of a session that took over from
the previous handoff and fixed the last recorded `tuple` defect.
**Repository state:** `origin/master` at `352230a` (the commit recording the
`2026-11-16T09:56` host gate). **One local commit is unpublished:** `cc0fa8c`,
"Expand a pack in a template-id and match arity one". The working tree is clean.

The published suite is **679/679** on Windows and **587/587** on Linux, counted from
the incremental tier-2 matrix this session ran on the committed tree — see §5 for why
the two numbers differ and why that matrix is not a gate.

`implementation.md` is the record. Its `## Gate verification` section holds one block
per gate with four configuration rows each; the running log's last entries are this
session.

---

## 1. The remaining objective: `tuple`

Five of the six original items are complete and published. The sixth is item 4's
`tuple`; its `functional` half shipped. **`tuple` is no longer blocked by a defect** —
it is blocked by two measured front-end gaps, listed in §3.

A recursive variadic class template with a partial specialisation over its pack now
compiles, links and runs:

```cpp
template <typename... Ts> struct Tup { };
template <typename T, typename... R>
struct Tup<T, R...> { T head; Tup<R...> tail; };
Tup<int, char> t;  t.head = 7;  t.tail.head = 'x';      // works
sizeof(Tup<>) == 0;  sizeof(Tup<char>) == 1;             // correct
```

---

## 2. What was wrong, and the fix that closed it

`sizeof(Tup<int, char>.tail)` was **0**; `Tup<char>` is one byte. Two coupled causes,
both measured with a print the previous handoff asked for:

1. **A member's pack was never expanded.** `Tup<R...>` inside `Tup<int, char>`
   reached type resolution as **one** argument spelled `R...`, which nothing
   resolved, so the resolver's `int` fallback made the member's type `Tup<int>` —
   the primary, which has no members.
2. **The selection rule required an extra argument for the pack** (`>` rather than
   `>=`), so arity one went to the primary even when named correctly. `>` had been
   chosen because `>=` overflowed the stack; the overflow came from cause 1, through
   the `int` stub naming the record being built, so **fixing the expansion makes
   `>=` safe**. That coupling is what four earlier rounds could not break.

Four changes in `src/frontend/sema.cpp`: `expand_template_arguments` rewrites
`name...` into one argument per pack element (`R0`, `R1`, …); `pack_parameter_index`
is the single reader of a pattern's pack position; the selection rule matches when
the fixed prefix fits and lets the pack absorb nothing; and `template_argument_type`
refuses an ellipsis spelling instead of answering `int`. Separately, both binding
maps are now **restored** after a nested instantiation rather than cleared — a member
that instantiates a shorter tuple used to leave the rest of the enclosing record
without bindings.

`tests/fixtures/c/recursive_variadic.cpp` pins it (`krc_link_kr16_recursive_variadic`
and `krc_run_kr16_recursive_variadic`, exit 42).

---

## 3. What `tuple` still needs, measured this session

Probes are `build/corpus/tp1.cpp` … `tp4.cpp`; each was compiled, not reasoned about.

| Probe | Shape | Result |
|---|---|---|
| `tp1` | specialization with `Tup(T first, R... rest) : head(first), tail(rest...) {}` | **compiles** |
| `tp3` | member accessors with pack-free signatures, `sizeof...(R)` in a body | **compiles, right values** |
| `tp2` | free `template <typename T, typename... R> T& tuple_head(Tup<T, R...>&)`, deduced | **fails**: *member access on a non-record value* at `value.head` |
| `tp4` | the same accessor with `<int, char>` spelled at the call | **fails** identically |

So a `tuple` header could expose `t.head` and `t.tail.head` plus member accessors
**today**, but not `tuple_head(pair)` and not `get<I>`. Two gaps stand in the way, and
they are independent:

1. **A function template whose signature names a pack-carrying class template-id is
   analysed as a pattern.** `analyze_function_definition` skips a body when a
   parameter carries a `pack` label, and a `template-arg:R...` carries none, so the
   body runs against the unparameterised stub. The skip needs to see a pack anywhere
   in the signature. It fails in `-fsyntax-only` **and** `-c`, so this is the
   analysis path, not IR generation — which is not the shape the recorded
   callable-parameter gap (`ADR 0042`) has.
2. **Deduction from a pack-carrying template-id parameter.** Even with the body
   skipped, nothing binds `T` and `R` from `pair`. `ADR 0042` records the same second
   wall for the callable case: the pattern is skipped, the call still targets it, and
   the link reports an undefined symbol.

`get<I>` additionally needs a member template or value-parameter partial ordering;
both are recorded as absent, and neither was measured against a tuple this session.

---

## 4. Found, recorded, deliberately not fixed

**`CIRGen::ir_size_of` overstates small records.** Its `Struct` case gives every
non-float field 4-byte alignment, so `sizeof` of an **expression** of a record type
whose fields are narrower than four bytes is too large. Plain C, no template:

```cpp
struct In { char a; }; struct Out { In inner; };
sizeof(outer.inner)   // 4; Sema's size_of says 1
```

This is why `recursive_variadic.cpp` asserts the layout through `sizeof(<type name>)`
rather than `sizeof(t.tail)`. `ir_size_of` also sizes allocations and copy lengths, so
correcting it is its own measurement; it is recorded in `implementation.md`, not
fixed, and no test depends on the wrong answer today.

---

## 5. Building, testing, and what may be pushed

Use the presets. `CMake` and `ctest` are **not on `PATH`**:

```
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
```

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug          # expect 679
```

Linux rows, in WSL, **one at a time**, `-j2`, raised stack:

```bash
wsl -e bash -lc "cd /mnt/c/Users/kevin/repos/KRCompiler && ulimit -s unlimited && cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j2 && ctest --preset linux-gcc-debug"
```

Linux counts **587** where Windows counts **679**: the 92 executed tests are
registered only on a Windows host with a sibling VM.

- This session's change touches `include/krc/`, so it needed the **tier-2 matrix**,
  which passed: Windows debug and release 679/679, Linux debug and release 587/587,
  each run on its own, no warnings. Those were **incremental** builds, so it is a
  matrix and **not** a gate.
- **A tier-2 change stays local until a host gate passes.** `cc0fa8c` is therefore
  unpushed and must not be pushed on the strength of §5's matrix.
- A **gate** means: delete the four build directories, then build and test each
  preset one after another (not concurrently — `ctest` can read a staged
  `libkrcrt.kro` while another job rebuilds it, which shows as
  `krld: KRO CRC mismatch`). Record it under `## Gate verification` with the
  timestamp of when the builds **started**, plus
  `not verified: native Linux x64, Linux ARM64`.
- Determinism: the six `krc_determinism_*` tests plus `krc_cli_determinism` are inside
  the suite and passed in every configuration this session.

---

## 6. Traps that still cost time

1. **Never write repository content through a PowerShell here-string.** A
   double-quoted here-string turns `` `n `` into a newline and splits string
   literals; a `Replace` that reports success without checking is the same trap. Use
   `edit`. This has cost the project nineteen broken files across sessions.
2. **Put an identity in every print.** Two rounds were spent this run reading a print
   as being about the thing under investigation when its node belonged to the caller.
   A guarded branch prints *nothing*, and nothing reads as "this path is not the
   issue".
3. **Verify a symbol before breaking on it.** `x krc!*align_of*` shows the real name is
   `Sema::align_of_type`; "no breakpoint hit" is ambiguous between "not called" and
   "never bound".
4. **`std::abort()` in a console app raises a Windows error dialog** on the operator's
   screen. Use `_exit`, return a value, or run the crashing input once under `cdb`.
5. **A stale `.kex` makes a failed link look like a wrong value.** Check the link
   line, not only the exit status.
6. `cdb.exe` is at `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`, and
   it truncates the symbol column in `kb`/`k`.
7. Never run `kr32.exe` without asking. Private VM copies live in `build/vm/`; KR16
   needs **both** `--fs-root` and `--fs-quota` (the default quota is 5 MiB counted
   recursively over the root).
8. `../kr16` and `../kr32` are read-only inputs except their `inbox/` directories.
9. `AGENTS.local.md` is an instruction file, not a log. Milestone state belongs in
   `implementation.md`.

---

## 7. Suggested first hour

1. Read this session's last ten entries in `implementation.md`; the chain is there.
2. Confirm the tree is green: `ctest --preset windows-msvc-debug` — expect **679**.
3. Decide whether `cc0fa8c` is worth a gate and a push now (it is the only
   unpublished change, and `tuple`'s next unit will need a gate anyway).
4. For `tuple`: start at gap 1 in §3, because it is the smaller of the two and both
   are needed. Measure `tp2` with `-fsyntax-only`, and print the label list the
   pattern's signature carries at `analyze_function_definition` — if `template-arg:R...`
   is there, the skip is one condition, which is the same shape as the value-parameter
   skip that already exists beside it.
5. Do not write the header until `tp2` passes. A `tuple` whose element access is a
   `.tail` chain is not a `tuple`, and this project has removed three headers rather
   than ship one that advertises more than it does.

---

# Session close — the five-item goal is complete

**Written:** 2026-11-16T16:13:04-08:00, at the end of the session that took over
from this file and finished the goal it named.
**State:** `origin/master` = `a930373`, working tree clean, **everything pushed**.
Suite **696/696** on Windows and **596/596** on Linux, from the clean-build gate
recorded in `implementation.md` under `### 2026-11-16T15:17:00-08:00`.

**This section supersedes §1–§7.** The defects those sections chase are closed; do
not re-litigate them. `docs/planning/tuple-and-layout-priorities.md` is likewise
history: its five items are done, and the four gaps below are what remains.

## What landed, with the commit that carries it

| Commit | Change | Fixture |
|---|---|---|
| `cc0fa8c` | A pack in a template-id now expands to the pack's elements, so a member written `Tup<Rest...>` names the tail of the pack and a specialization matches at arity one | `recursive_variadic.cpp` |
| `d3f29b2` | A call whose parameter is a pack-carrying class template-id instantiates (it silently called the `int`-stub pattern before), and one `Sema::is_pattern_signature` predicate now answers "is this a pattern or an instance?" for both Sema and IR generation | `pack_template_parameter.cpp` |
| `908072c` | One record size everywhere: `ir_size_of` and the KR16 backend use natural field alignment, and the target's `aggregate_rules` decides whether an aggregate goes in registers or as a hidden pointer ([ADR 0046](docs/decisions/0046-object-layout-and-aggregate-rules.md)) | `small_record_size.cpp`, `array_new_subword_record.cpp`, `small_aggregate_argument.cpp` |
| `2d465ac` | `sysroot/include/tuple` ships on the named API — `tuple_size`, `tuple_head`, `tuple_tail`, `.head`/`.tail`, constructed in place | `tuple_header.cpp` |
| `a930373` | The gate record above | — |

Two `sizeof` facts worth keeping: before the layout change, `sizeof(struct Three{a,b,c})`
was **3** from a type and **12** from an expression, and the same number decided an
aggregate's parameter class; on KR16 the backend laid that record out as **6** bytes.
After it, the type, the expression, the allocation and the element stride are one
number on both targets.

## What remains, each measured, with its reproduction

1. **A class member's pack parameter is expanded for one element only.** This is the
   one that costs most: `tuple<int, char> p(41, 'x')` **compiles and then fails to
   link** with `undefined symbol 'tuple_ctor_0_value'`, because the call binds to the
   pattern's constructor rather than an instance's. The same root cause makes
   `tuple<>` as a *local* fail to link, and it is why `make_tuple` is absent. One
   change would give all three. Reproductions: `build/corpus/tp1.cpp` (compiles),
   `build/corpus/tup6.cpp` (a default constructor links), `build/corpus/th-c.cpp`
   (the empty local does not). The header states the limit.
2. **A memory-class aggregate argument is copied as one word.** `create_call_instruction`
   materialises the caller's copy with a whole-record load and store, and the back
   ends move a whole-record value as one word — four bytes on KR32, **two on KR16**.
   `small_aggregate_argument.cpp` therefore runs on KR32 and only *links* on KR16,
   with the reason beside the test. Two fixes were tried and reverted; the copy needs
   the argument's address and that path does not have it.
3. **A reference-returning call passed as an argument crashes the compiler**, with no
   template involved: `int& head(Item&)` called as `head(pick(item))` faults the back
   end. The callee is never emitted at all, so the comparison consuming it builds an
   instruction with a null operand. Reproduction: `build/corpus/tp10.cpp`. This is
   why the tuple header tells readers not to chain `tuple_head(tuple_tail(x))`.
4. **`get<I>` needs a language feature that does not exist.** A member template is
   rejected at parse (`expected declarator`); a partial specialisation over a non-type
   argument fails at its use site (`element<0, T>::value` → `expected expression`).
   Either would unblock it.

Also open in M10, unchanged by this work: dynamic `thread_local`, the EH record
encoding ADR, and the `v1.1` tag. ADR 0046's option B — moving IR-level layout into
the target descriptor and deleting the remaining copies — is deferred, not rejected.

## Traps this session added or re-confirmed

- **`/tmp` does not exist on Windows.** A `>/tmp/x.log` redirect fails in the shell
  *before* CMake runs, so the next command reports a missing build directory and it
  reads exactly like a build failure. It cost a gate-row restart. Use no redirect, or
  a path under the workspace.
- **A `sizeof` probe must distinguish the type path from the expression path.** They
  used different code (`Sema::size_of` versus `CIRGen::ir_size_of`) and disagreed for
  months precisely because every test took the type path.
- **ADR 0002 freezes the descriptor *shape*.** `docs/targets/schema.md` already
  declares the ABI `aggregate_rules`, so implementing a v1 field needs no schema
  bump; *adding* a field would, and would need a new ADR.
- **I broke the here-string rule once**, writing the CMake block for the layout
  fixtures through a single-quoted PowerShell here-string. It was safe because
  single-quoted strings do not interpolate, and I read the result back — but the rule
  is `edit` for repository content, and it exists because this has cost the project
  nineteen broken files.
- **kr32's `P` debug record has no consumer here yet.** `src/backend/register_allocator.cpp`
  is referenced only by itself, so no local is ever register-held and every `V` record
  is accurate. The reply with that measurement is in `../kr32/inbox/`.

## Suggested first hour

1. Read this section, then the last twenty entries of `implementation.md` — they are
   this session's chain.
2. Confirm the tree is green: `ctest --preset windows-msvc-debug` — expect **696**.
3. Take gap 1, because it is the highest-value and self-contained: expand a class
   member's pack parameter the way `instantiate_template_call` already expands a
   function template's, then give `tuple` its element constructor and `make_tuple`.
   Verify with `tp1`, `tup6`, `th-c` and the tuple fixture.
4. Then gap 2, the argument copy, because it is the only one that is silently wrong
   on a target we test.
5. Gap 3 is the smallest to *diagnose* — the function is never emitted, so start at
   `collect_function` and the `functions_` lookup rather than at the call site.

