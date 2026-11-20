# Handoff — 2026-11-19

Written 2026-11-19 at revision `085a8f5` by the session that completed the optimisation
programme. Every number in it was true at that revision. **Re-run §9 before trusting it.**

---

## 0. Read in this order

1. **`plan.md`** — the design contract. §8.8 (optimisation levels), §16 (milestones), Appendix A
   (the decision log; `D-049` authorises the optimisation pass list).
2. **`AGENTS.md`**, then **`AGENTS.local.md`** — standing orders. `AGENTS.local.md` is the one
   that will actually stop you making a mistake here: build tiers, the routine gate row, the two
   serial-execution traps, and the read-only status of the sibling repos.
3. **`implementation.md`** — the live tracker, ~6,900 lines and 2,000 dated entries. **It is
   written by more than one session and its entries are not in chronological order**, because
   merges interleave them. Search by timestamp (`Select-String '2026-11-19T'`); do not trust the
   tail as "newest".
4. **`docs/optimize.md`** — the optimisation plan and its measurements (§3.4/§3.5 measurements,
   §7 pass list with per-row status, §11 gates with a Status column).
5. **`docs/plan-o1-debugger.md`** — the plan for making `-O1` usable with the debugger. This is
   the main forward-looking document and §6.1 below summarises it.
6. **`docs/decisions/`** — `0049` (allocator, accepted), `0050` (type canonicalisation, accepted),
   `0051` (allocate on the machine form, **proposed, not accepted**).

---

## 1. What this project is

KRCompiler is a retargetable C11/C++17 compiler for the KR-family VMs: `krc` (driver, front end,
IR, backends), `krld` (linker), an object/symbol format, and a runtime. Two meanings of "target"
are in play and conflating them causes real errors: **host** platforms (Windows MSVC, Linux
GCC/Clang in WSL, ARM64 Pi) build the toolchain; **VM targets** (KR32, KR16) are what it compiles
for. `docs/object.md` describes the container: `krc -c` writes a relocatable `.kro`, `krld` writes
a `.k32` executable carrying `.text`, `.data`, `.cpp_except`, `.debug_info`, `.kdbg`.

The VM (`vm/krc_kr32.exe`, `krc_kr16.exe`) has a debugger interface: `vm debug <file>`,
`vm dap <file>` (Debug Adapter Protocol server), `vm gui`.

---

## 2. State at handoff

- **Code revision `085a8f5`.** Commits after it, up to `d05bd08` when this was last checked, are
  **documentation only** — verified with
  `git diff --stat 085a8f5..HEAD -- src include targets sysroot tests CMakeLists.txt CMakePresets.json benchmarks`,
  which is empty. So `git log -1` showing a later sha is expected and does not mean the numbers
  below have moved.
- Tree clean. At this revision three untracked generated files — `examples/KRlibcpp.S`,
  `examples/KRlibcpp.O1.S` and `libc.S` at the root — plus `build-loop.md` at the root (not ours) sat
  outside version control. **None of them is present at `3aabc28`**; nothing was lost, because they
  are build products. See §6.8.
- **Suite 889/889** on `windows-msvc-debug`, `windows-msvc-release`, `linux-gcc-debug`,
  `linux-gcc-release`, `linux-clang-debug`, `linux-sanitize` — clean gate at `2b602f5`
  (`implementation.md` → `## Gate verification`). **58 tests execute at `-O1`** across tiers 0–2.
- The pre-optimisation baseline was `8511e50` with 828 tests. Measured against it:

| | `-O0` | `-O1` |
|---|---:|---:|
| KR32 corpus instructions | **−27.0%** (62,012 → 45,295) | **−41.7%** (→ 36,135) |
| KR16 corpus instructions | −24.1% (23,191 → 17,595) | −27.7% (→ 16,770) |
| `bench` ticks KR32 | −29.1% (272,602 → 193,340) | **−43.3%** (→ 154,533) |
| `bench` ticks KR16 | −28.0% (→ 192,758) | −30.7% (→ 185,386) |
| `kr32/libc.c` frame bytes | 41,840 → 29,864 (−28.6%) | 41,840 → 22,320 (−46.7%) |

- `-O0` is byte-identical to no-flag and asserted by `krc_optimization_levels`; the check-in
  baselines `benchmarks/size.tsv` and `benchmarks/ticks.tsv` match the tree.
- Metrics: `krc --dump-stats [-O0|-O1] file.c` prints `stats <fn> <instr> <blocks> <frame bytes>`
  and a TOTAL. **It counts IR instructions, not assembly instructions** — the 36,135 above is a
  corpus-wide assembly count from `benchmarks/`, a different unit. Confusing the two produced a
  wrong gate record once.

---

## 3. Build and test

The policy is in `AGENTS.local.md`; the short version:

```
# inner loop, Windows host
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug          # ~100 s, 889 tests

# Linux rows, one at a time, in WSL
wsl -e bash -lc 'cd /mnt/c/Users/kevin/repos/KRCompiler && ulimit -s unlimited && \
  cmake --build --preset linux-clang-debug -j2 && ctest --preset linux-clang-debug'
```

- **`cmake` is not on a plain `PATH` on Windows.** It lives under the Visual Studio install:
  `<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
- A clean gate **deletes the build directory**, which is why the VM binaries live in `vm/` and not
  `build/vm/`.
- `-j2` and `ulimit -s unlimited` for the Linux rows; GCC 15 in this WSL image intermittently
  segfaults garbage-collecting its own trees, so rebuild once before believing a build failure.

---

## 4. What the optimisation programme delivered

**Landed**: S1 literal operands, S2 direct conditional branch, S3 addressing modes, S5 `BRA`
(ADR 0043), E1 constant folding, E2 local CSE, E3 dead code elimination, E4 single-block alloca
promotion, E5 empty-block elimination, E6 cross-block promotion with `phi`, the register allocator
(ADR 0049 — `-O1` only, non-default), type canonicalisation with identity-cast folding (ADR 0050,
which also improves `-O0` because selection quality is lowering under `D-049`), P2 redundant jump,
frame-slot omission for values the allocator registers, and a `volatile` pointer-dereference fix.

**Measured and declined, each with a number recorded**: E7 global CSE **6 instructions** (built,
tested, removed), P1 slot forwarding **3**, cross-block allocation **9**, widening the allocator's
filter to a call-aware rule **18** (36,135 → 36,117, suite green, reverted as not worth changing
every `-O1` output), S4 frame-slot reuse **−28.6% frames but 54 executed tests broken, twice**,
R1 emission **18 over the current filter**.

**Gate shortfalls, with numbers** (`docs/optimize.md` §11): OG1's `-O0` KR16 target misses by 0.9
points; OG3 is unmet because **its premise was wrong** (E6 measured −0.3%, a phi copy costing one
store where the load it replaced cost one load); OG4's frame-bytes half is −28.9% against −50%.
OG2 and OG5 are met.

---

## 5. The machine-IR foundation (built, proven, not yet used)

The backend now records what it emits. `emit_machine` builds a `machine::Instruction` and prints
it, so text and machine form come from one call site. Built over ten rounds and **output-neutral
throughout**, verified by a zero-byte diff over `kr32/libc.c` at both levels after every step:

- `src/backend/machine_ir.h/.cpp` — `Opcode`, `Operand`, `Instruction`, `Block`, `Function`, a
  printer and a **parser**. A corpus-wide test round-trips every emitted instruction line at both
  levels (~10,000 lines) through the representation.
- The emitter is converted in place at **98.7% of the corpus's instructions**; the remainder is
  aggregate-return and EH-resume plumbing plus diagnostic `HALT` text.
- `Instruction` carries `virtual_destination` / `virtual_sources` — data flow **beside** the
  printed operands, so printing is unchanged.
- `src/backend/machine_liveness.h/.cpp` — live ranges from that data flow, `crosses_call` per
  range, peak pressure **4 of 8 registers** over the corpus, **zero** loop-carried anomalies.
- `plan_linear_scan` + `interference_is_respected` — an allocation plan over the recorded blocks:
  **1,137 values, 0 unallocated, 0 interference violations** (call-crossing values excluded).

Nothing emits from it. That is the point of §6.3.

---

## 6. Open work, ranked

### 6.1 `-O1` and the debugger — see `docs/plan-o1-debugger.md` (highest value)

Measured: a `-O1` image keeps **all 768 variable records**, 242 functions and 67 types, and
**11,189 of 11,229 line records**. Variables survive because source variables are the IR values
named `<variable>.addr` and both promotion passes refuse those. Gaps:

- **G1** 40 lines have no address at `-O1`. **Cause undiagnosed**; diagnose before fixing.
- **G2** KDBG `Variable` records carry **no scope or range** — function, name, offset, size, type
  only. Latent blocker for S4 and for promoting a named variable: a debugger would show a stale
  value with no way to know. **Format change; the format is frozen.**
- **G3** no debug test builds at `-O1`; **G4** DAP is untested (the server starts and exits 0 on
  EOF, so a harness is buildable); **G5** OG4's "line-accurate under the debugger" is unverified.

Work packages: **W1** a DAP harness (initialize/launch/setBreakpoints/stackTrace/variables;
breakpoint hits, variable reads correctly) — no compiler change, start here. **W2** diagnose the
40 lines, policy recommended: record only lines with an address. **W3** `-O1` debug tests plus two
invariants over a corpus image (line addresses inside function ranges; `offset + size` inside the
claimed frame). **W4** scope ranges in KDBG — needs an ADR, `docs/symbols.md`, a VM reader change
and a version bump. **W5** `<optimized out>`. **W6** documentation and OG4's status.

### 6.2 S4 frame-slot reuse — −28.6% frames, blocked

Attempted twice and reverted twice (54 and 52 executed tests broken). The measured win is real;
the safety argument was not. The two known hazards: `emit_phi_copies` writes a `phi`'s slot from
a **predecessor** block, and the emitter addresses slots the IR never names (aggregate copies,
aggregate arguments, bit-field read-modify-write, aggregate returns). **It needs W4 (scope
ranges) and an emission path that owns every slot address.** Do not retry it against IR-level
reasoning; that is what failed.

### 6.3 R1: emit from the recorded form — ADR 0051, proposed

Accepting ADR 0051 changes **every `-O1` output** and gives up the byte-identical guard that made
§5 safe; its replacement guards are the suite at both levels, recorded-vs-emitted equality, and
`--dump-stats` counts. What it buys is **18 instructions over the current filter** — the plan's
356 was a ceiling that ignores the filter's type restrictions and the colouring's spills. So this
is the enabling step for 6.2, not an optimisation worth taking for itself.

### 6.4 KR16 record-return defect — **verified fixed**; the log entry is history

The tracker describes a record returned by value losing its trailing sub-word member on KR16,
reproducible in a non-template function. **A probe run for this handoff says it no longer
reproduces**: `std::tuple<int,char> mk(int a, char b)` built with the project's KR16 recipe exits
**0**, as does `mk2(char a, int b)` with `1000` as the second element. The fix landed in
`76f58d9`/`8e73859` and the header wording was audited in `5934a2c`.

Nothing to do. **Do not reopen it on the strength of the log entry** — that is what the appendix
below records, and it is the mistake this handoff's first draft made.

### 6.5 Tuple pack pointer deduction (pinned as rejected)

`std::make_tuple(p, 1)` with a pointer element deduces `int*` as `int`. Pinned by
`krc_cxx_make_tuple_pointer_rejected`, which asserts rejection. A single-parameter template keeps
pointer-ness, so the decay is specific to pack element deduction.

### 6.6 Gate shortfalls to close

KR16 `-O0` at −24.1% needs 0.9 points to reach OG1's −25%; OG4's frame bytes need −50% and have
−28.9% (the remainder is address-taken objects and named variables — S4's territory); OG3's
premise should be re-stated rather than chased.

### 6.7 `volatile` limitation

`*p` through a `volatile`-qualified pointer is marked volatile; `*(p + 1)` is **not**, because the
qualifier is read only when the operand is an identifier. No pass removes or reorders an access
today, so nothing miscompiles — but the documented contract is not yet the implemented one.

### 6.8 Small items

- `docs/optimize.md` §7's R1 row and §3.5 carry the corrected payoff figures; keep them in step if
  anything changes.
- **Three generated files are deliberately left untracked, and that is fine** — they are rebuilt
  often and no rule should generalise about them: `examples/KRlibcpp.S`, `examples/KRlibcpp.O1.S`
  and `libc.S` at the root. Note the case: the assembly **inputs** already in the tree are `.s`
  (lowercase — `targets/kr32/runtime/crt0.s`, `eh.s`, `fileio.s`, `host.s`,
  `tests/fixtures/c/branch_promotion.s`), while these three are `.S`. If one is ever meant to be
  committed, add the path explicitly rather than by pattern. **None of the three is in the tree at
  `3aabc28`** — a cleanup, not a regression, and the rule above still applies if a build recreates
  one.
- `build-loop.md` at the repo root is untracked and not from this work. It is also no longer present
  at `3aabc28`.

---

## 7. Traps that cost this session time

- **`implementation.md` is multi-session and not chronological.** Search by timestamp.
- **A merge that rebuilds `implementation.md` from one side silently drops the other side's
  entries.** Happened twice. Extract your additions (`git show HEAD:implementation.md`), then
  insert them into the other side; never regenerate the file wholesale.
- **An edit anchor must not include the boundary of what follows.** Two anchors swallowed the next
  function's signature, splicing bodies together. Read the file, anchor on a literal, and check the
  build.
- **A plan's ceiling is not its payoff.** The same error appeared three times: 35.8% memory share →
  9% (plan) → 7% (call-aware) → **18 instructions** (actual, after the filter and the colouring).
- **A byte-for-byte diff is the only honest guard for a refactor.** It caught two real defects the
  suite could not: `LOAD8U` silently substituted by `LOAD` (a miscompile), and `[R0]` printed as
  `[R0 + 0]`.
- **Run tests serially and one row at a time.** `jobs: 1` is set; overlapping rows still fail
  executed tests because every row runs the same VM binary.
- **`git ls-files` before claiming a file is untracked.** Two "untracked" handoff files were
  tracked, and were said to be unrecoverable when they were not.

---

## 8. Environment

Windows host with WSL 2 (Ubuntu 26.04, GCC 15.2, Clang 21.1.8, ASan/UBSan available); an ARM64
Raspberry Pi host runs its own gates and pushes records into `implementation.md`. Toolchain facts
and verification dates are in `AGENTS.local.md`'s Platform targets table — read it there rather
than from this file.

---

## 9. Verify this handoff before trusting it

```
git log --oneline -1                     # expect 085a8f5 or a descendant
ctest --preset windows-msvc-debug        # expect 889/889 at 085a8f5, 910/910 at 94c3295
build/.../krc --dump-stats -O1 targets/kr32/runtime/libc.c   # expect 9896 IR instr, 22320 frame bytes
```

The test count is the one expectation here that a later change moves without the compiler changing:
W1–W3 added 18 tests and the `volatile` qualifier rule 3, all after `085a8f5`. The `--dump-stats`
figures did not move, because none of that work touches what the backend emits.

If those disagree, the tree has moved: read `implementation.md`'s newest entries by timestamp and
`docs/optimize.md` §11 before planning anything.

---

## 10. Suggested first session

1. Run §9. If green, the state above holds.
2. Read `docs/plan-o1-debugger.md` and do **W1** (the DAP harness). It needs no compiler change,
   it is the instrument every later debugger test uses, and it will immediately say whether a
   `-O1` image is debuggable at all.
3. Then W2 (diagnose the 40 lost lines) — a bounded investigation with a measurable answer.
4. Do **not** start with S4 or with ADR 0051: both need W4 (scope ranges) or an emission path
   first, and both have already failed on IR-level reasoning.
5. If the priority is instead "what is still wrong in the compiler", read §6.5–§6.7: the pointer
   pack is pinned as rejected, the `volatile` gap is live but harmless today, and §6.4 is a defect
   that has since been verified **fixed**.

---

## Appendix: the discrepancy that was not one

This document first carried an appendix claiming that `sysroot/include/tuple` had lost a
correction — the tracker records at `2026-11-19T23:20` that the header was changed to document the
KR16 `make_tuple` defect, while lines 52–59 still assert `make_tuple(41, 'a')` returns
`tuple<int, char>`. **That claim was mine and it was wrong. The header is right; the tracker entry
is history.**

Evidence, gathered after the first draft:

- A probe compiled for **both** targets and run under both VMs exits **0**, so every assertion
  holds: `make_tuple(41, 'a')` gives a `tuple<int, char>` whose tail reads back `'a'`, and
  `make_tuple('a', 1000)` keeps the `1000`.
- The tracker's "still open" minimal reproducer — a **non-template** function
  `std::tuple<int, char> mk(int a, char b)` returning a local by value — also exits **0** on KR16
  when built with the project's own KR16 recipe.
- Commit **`5934a2c`** ("Audit the docs for the stale `make_tuple` defect wording") already did
  this audit and states the conclusion: *"finds no surviving claim: the only live mention is
  `sysroot/include/tuple:50`, corrected when the fix landed"*, and the remaining matches in
  `implementation.md` are dated log entries, historically accurate.

**So: do not change `sysroot/include/tuple`, and do not reopen the tuple defect on the strength of
that log entry.** The lesson is the one this project keeps teaching: a dated entry in
`implementation.md` is a statement about its own revision, not a claim about today — read it as
history and verify against the tree. The session that produced this handoff made exactly that
mistake while writing the appendix to warn about it.

