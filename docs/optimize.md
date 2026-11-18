# Optimizer strategy

**Status:** draft for discussion, 2026-11-17. Not approved, not a contract, and it changes nothing in `src/`.
**Owner of the contract it proposes to amend:** [plan.md](../plan.md) §8.8 (optimisation levels) and §16.1 (milestones).
**Companions:** [docs/ir.md](ir.md) (the frozen IR), [docs/symbols.md](symbols.md) (KDBG v1), `../kr32/docs/optimizations.md` (the sibling project's precedent).

This document records what the emitted code costs today, what the kr32 project learned from
optimising its own output, and the strategy this repository should take — with the ordering
justified by measurement rather than taste. Sections 1–5 are findings; 6–11 are the proposal;
12 is the list of decisions that need an answer before any of it is written.

Measured claims below come from the shipped `krc` binary in
`build/windows-msvc-debug/host/bin/Debug/krc.exe` over the checked-in corpus. Reproduction
commands are in §3.4. A claim that is not measured names its source: a file, a section, or a
sibling document.

---

## 1. The short version

`-O1` does not exist. `krc --help` has no `-O` option, `src/` contains no occurrence of
"optimi" in any spelling, and the only transformation in the compiler is a 72-line file that
deletes `MOV r, r`, `ADD r, r, #0` and `MUL r, r, #1` (`src/backend/peephole.cpp`), called
once at the end of the KR32 backend (`src/backend/kr32_backend.cpp:955`).

The code that pass runs over is, by measurement, about 82% data movement, address computation
and branching on KR32 and 90% on KR16. For a representative file, 4.5% of the emitted
instructions do arithmetic. The compiler is not failing to optimise a good baseline; it is
emitting the most literal lowering of each IR instruction, and the optimisation headroom is
close to the whole instruction stream.

Four levers exist, and they are not interchangeable:

| Lever | Where it lives | Structural cost | Measured headroom |
|---|---|---|---|
| Instruction selection | `src/backend/` | none — local to the emitter | branch 14.6%, `LEA` 11.3%, and most of the 17.0% `MOV` share (§3.1) |
| IR passes | `src/ir/` (new `passes/`) | a pass manager; **needs phi lowering first** | the 39.1% memory traffic, less what selection and allocation take |
| Register allocation | `src/backend/` | a machine IR, or promotion passes | part of the 39.1% memory traffic, plus frame bytes |
| Text peepholes | `src/backend/peephole.cpp` | a growing chain of line matchers | 838 store→load pairs, one per ten instructions (§3.3), then diminishing |

The shares overlap by construction: a select-then-register-allocate pipeline takes the same
instructions out of the memory column that a text peephole would, which is why they are listed as
alternatives per lever and not as a sum.

The recommended order is top to bottom, for the reasons in §6. That is the opposite of what the
sibling project did, and §5 explains why its order was right for it and wrong here.

---

## 2. What exists today

| Fact | Evidence |
|---|---|
| The pipeline in the plan has an optimiser between the verifier and target lowering. | `plan.md` §7.1 line 508: `-> optimiser (-O0/-O1)`. |
| The CLI contract names `-O0`/`-O1`. | `plan.md` §15.1 line 1227: `-g -O0 -O1`. |
| Both flags are parsed and listed, `-O0` is the default, and `-O0` output is byte-identical to a build with no flag. | `src/tools/krc/main.cpp` option table; `cmake/CheckLevelIdentity.cmake` and the `krc_optimization_levels` test assert the identity. |
| The `-O1` pass list is specified in the plan. | `plan.md` §8.8 line 645: "mem2reg, constant folding, dead code elimination, common subexpression elimination, copy propagation, CFG simplification, branch folding, peepholes, graph-colouring register allocation. No inlining, LTO, loop-invariant code motion, or vectorisation in v1." |
| Of that list, two passes exist: `-O1` removes an unconditional branch to the label that follows it, and constant-folds a pure 32- or 64-bit integer operation whose operands are all constants. There is no pass manager and no other IR pass. | `src/backend/peephole.cpp` (`remove_redundant_jumps`), called from both backends at `-O1` only; `src/ir/passes.cpp` (`constant_folding`), run by the driver before the backends and re-verified afterwards. `src/ir/` otherwise still holds the model, builder, verifier and printer. |
| Both backends emit **text** instruction by instruction, appending to a `std::string`. | `Kr32Backend::emit_function`/`emit_instruction` (`kr32_backend.cpp:958`, `:1155`); `kr16_backend.cpp` has the same shape. |
| Every IR value with a type gets its own frame slot, unconditionally. | `kr32_backend.cpp:296-305`: `layout.slots.emplace(instruction.get(), offset)` for every non-void result. |
| So every IR instruction costs a store, and every operand use costs a load. | §3 measurement: 3.74 machine instructions per IR instruction. |
| The graph-colouring allocator is built, unit-tested and **deliberately unwired**. | `src/backend/register_allocator.h:3-10`: "NOT WIRED IN, deliberately ... every value is memory-resident by construction". |
| The backends have no `phi` lowering; IRGen avoids `phi` entirely by staging across branches in `alloca` slots. | `docs/ir.md:80-84`: "have no `phi` lowering: a value that has to cross a branch is carried in an `alloca` slot". |
| `-c` assembles the same emitted text, so a text-level transformation reaches both products. | `src/tools/krc/main.cpp:528-539` (`assemble(*assembly, ...)` inside the `emit_object` path). |
| Each function ends with a `.cpp_except` descriptor holding its **frame size** and region labels. | `kr32_backend.cpp:1137-1152`. |
| Every instruction with a debug location emits a `.Ldbg.<fn>.<n>:` label immediately before itself. | `kr32_backend.cpp:1036-1053`. |
| `-O1`'s debug guarantee is line/function mapping only. | `plan.md` §8.7 line 638; `docs/ir.md:102`. |
| KDBG v1 can only say a variable lives at a **frame offset**. | `docs/symbols.md:46` (`.kdbg_var` → `Variable`: "function, name, frame offset, size, type"); ADR 0009 puts location expressions post-v1. |
| Tests assert almost nothing about emitted code shape. | One assertion in the whole suite: `tests/unit/test_irgen.cpp:164` checks `MOV R1, #4` appears. |
| There is no optimiser milestone, gate, acceptance criterion, or metric anywhere. | `plan.md` §16.1 lists M0–M10; §5.7 gates G0–G9; M2's deliverable mentions "instruction selection, regalloc" and its gate G2a is "correct call/return sequences". |

Three consequences follow immediately, and they are labelled because they constrain sequencing:

**F1 — FIX-BEFORE.** `plan.md` §8.8 puts **mem2reg first**, but mem2reg is the pass that creates
`phi` nodes, and `docs/ir.md:80-84` records that neither backend lowers `phi`. Landing `-O1` in
the order §8.8 lists it produces IR the backends cannot compile. Either phi lowering arrives
first, or the first promotion pass is restricted to values whose defs and uses are in one block
(§7, pass E4).

**F2 — FIX-BEFORE.** KDBG v1 cannot express "this variable lives in `R9`". A pass that promotes a
named local to a register therefore silently makes every `.kdbg_var` record point at a slot
nothing writes — the debugger shows `0` for a live variable. kr32 hit exactly this and extended
its format with a `P` record (inbox note `kr32-Debug-Locations.md`). We must either keep named
locals in their slots at `-O1` or take a KDBG change with an ADR.

**F3 — ERRATA.** `-O1` is promised by the plan and reachable by nobody: no flag, no milestone, no
gate, no metric. A promise with no gate is how a compiler ships `-O1` that is `-O0` with a
different pass order. §11 proposes the gates; the plan needs an Appendix A entry once this
strategy is accepted (§12, Q1).

---

## 3. Where the instructions actually go

### 3.1 The mix

Every emitted line classified by its mnemonic into memory (`LOAD*`/`STORE*`, `LD*`/`ST*`),
move (`MOV`), address (`LEA`), branch (`JMP`/`JCOND`/`JZ`/`JNZ`/`BRA`/`BEQ`/…/`CALL`/`RET`),
compare (`CMP`), and everything else (real arithmetic, shifts, floating point, helper calls).

| Set | Target | Files | Instructions | mem | mov | addr | branch | cmp | other ALU | mem+mov+addr+branch |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `tests/fixtures/c`, files under 1.2 KB | KR32 | 117 | 17,099 | 37.4% | 17.4% | 11.5% | 14.3% | 3.7% | 15.7% | **80.6%** |
| tier0 + tier1 + `fixtures/c` | KR32 | 147 | 35,744 | 39.1% | 17.0% | 11.3% | 14.6% | 4.0% | 14.0% | **82.1%** |
| `tests/fixtures/c`, files under 1.2 KB | KR16 | 91 | 10,703 | 42.4% | 15.2% | 13.2% | 19.2% | 3.9% | 6.1% | **90.0%** |
| tier0 + tier1 + `fixtures/c` | KR16 | 120 | 27,871 | 42.2% | 14.7% | 12.7% | 20.8% | 4.4% | 5.2% | **90.4%** |

Single file, both targets, `tests/corpus/extracted/tier0/arithmetic.c` (a 120-line file of
mostly `if (expr != value) return n;` checks):

| | KR32 | KR16 |
|---|---:|---:|
| instructions | 650 | 680 |
| memory | 240 (36.9%) | 233 (34.3%) |
| `MOV` | 115 (17.7%) | 117 (17.2%) |
| `LEA` | 62 (9.5%) | 55 (8.1%) |
| branches | 150 (23.1%) | 197 (29.0%) |
| `CMP` | 50 (7.7%) | 50 (7.4%) |
| real arithmetic | 29 (4.5%) | 21 (3.1%) |
| frame size | 432 bytes | 196 bytes |

The KR16 "other ALU" share is smaller than KR32's because 32-bit and wider operations there
become helper calls rather than inline instructions — the same work, moved into `CALL`s the
table counts as branches.

### 3.2 Expansion from IR

Eight sample files, IR instruction count against emitted instruction count: **1,825 IR
instructions become 6,818 machine instructions, 3.74×**. Per file the ratio runs from 3.4× to
4.3×, and is 3.55× for `arithmetic.c`. A backend that selected instructions well and kept values
in registers would be near 1.2–1.5×; the excess is the frame round trip per value.

### 3.3 The two obstacles that shape any text-level pass

- **Debug labels sit between instructions.** The corpus emits **8,769 `.Ldbg` labels against
  34,616 instructions** — one every four instructions, 26% of all label-bearing lines (the same
  ratio holds in the 14-file tier0+tier1 subset: 2,179 against 8,388). A peephole that matches
  "the next line" sees a label where the next instruction is.
- **A store is followed by a load of the same slot across a label.** Ignoring labels and looking
  only at the instruction stream, **838 pairs are `STORE Rn, [R7 + k]` immediately followed by
  `LOAD Rm, [R7 + k]`** — one pair per ten instructions, and the canonical store→load-forwarding
  opportunity. A strictly line-adjacent matcher finds **zero** of them, and folding all 838 would
  remove 838 loads (10% of the stream) or, where the value can stay in the stored register, 1,676
  instructions (20%).
  kr32's `remove_adjacent_slot_roundtrips` folded exactly this class *across a branch-target
  label* and mis-compiled a loop head; its own note calls that the bug that motivated the debug
  work. The lesson is not "skip labels" — it is "know which labels nothing branches to".

### 3.4 Reproduction

```powershell
# per-file mix and totals (replace the file set as needed)
$krc = "build\windows-msvc-debug\host\bin\Debug\krc.exe"
foreach ($f in Get-ChildItem tests\corpus\extracted\tier0 -Filter *.c) {
  & $krc -S $f.FullName -o "$env:TEMP\$($f.BaseName).s"   # add --target=kr16 for KR16
}
# count lines matching '^\s+[A-Z]' (instructions), '^\.Ldbg' (debug labels), and classify by mnemonic
# IR instruction count
& $krc --dump-ir tests\corpus\extracted\tier0\arithmetic.c
```

Definitions matter and are stated so the numbers can be re-derived: an "instruction" is a line
whose first non-space character is an uppercase letter (so directives, comments and labels are
excluded); a "label" is a line starting with `.L`. Directives such as `.word` and `.byte` are
neither, which is why data-emission lines do not distort the percentages.

---

## 4. What the plan already fixes, and what it leaves open

Fixed by `plan.md` §8.8 and `docs/ir.md:140-152`, and not open for redesign here:

- `-O0` is the semantic floor: no semantic-changing optimisation, every variable in a frame
  slot, full debug info.
- `-O1` is mem2reg, constant folding, DCE, CSE, copy propagation, CFG simplification, branch
  folding, peepholes, graph-colouring register allocation.
- **No inlining, no LTO, no LICM, no vectorisation in v1.**
- No strict-aliasing assumptions: "the optimiser MUST NOT introduce type-based aliasing
  optimisations" (§8.4).
- `volatile` loads and stores are never removed, reordered across other volatile operations, or
  combined; atomic ordering is preserved; exception/cleanup edges are preserved; determinism is
  byte-exact (§8.8, §15.6).

Left open, and what this document proposes to settle:

1. The **order** in which those passes land, and which lands first when they depend on each other
   (F1 is exactly such a dependency).
2. Whether register allocation is wired into the emitter or arrives as promotion passes.
3. What a pass is allowed to assume about `phi`, about `.Ldbg` labels, and about the `.cpp_except`
   descriptor.
4. What `-O1` promises about a **named variable's** debug location (F2).
5. How any of it is measured, and what makes a gate pass.

---

## 5. The kr32 precedent

`../kr32/docs/optimizations.md` documents 34 numbered passes plus a safety model, and
`../kr32/src/asm/optimizer.cpp` is that implementation: **5,854 lines in one file**, 80 function
definitions, all operating on `std::vector<std::string>` lines of assembly text, plus a 39 KB
`tests/unit/test_optimizer.cpp`.

What that bought, as its own document reports it: **44.3% fewer instructions on C and 22.8% on
C++** at `-O1` against `-O0` on its examples, with `libc.c` going from 16,334 to 9,936
instructions; the linked kernel image at 73,072 bytes. That is a real result from a design we
should not copy verbatim.

Five lessons, each with the evidence in that document:

1. **A text pass chain buys a lot and then charges rent.** By the time it needed cross-block
   knowledge the project had grown `build_cfgs`/`CfgFunction` *inside* the text pass
   (`optimizer.cpp:642`) and a register/liveness model on strings. The 34 passes are not
   independent: each has its own barriers (calls, `.Lcs_*` pads, `.Lclean_*` pads) that exist
   because the representation is a string, not a graph.
2. **Labels are a correctness hazard, not a formatting detail.** A store/load fold across a
   branch-target label produced a wrong loop comparison; the fix was to make the exemption
   conditional on nothing branching to the label. This repository emits a `.Ldbg` label per
   instruction, so the same hazard is 26% of all lines here.
3. **Frame-slot promotion forces a debug-format change.** Once locals live in registers, the
   frame-offset-only variable record lies. kr32 added a `P` record (register, half-open label
   range) and a per-pass application rule, because a later pass that inserts instructions
   invalidates any line index an earlier pass recorded. Our `docs/symbols.md:46` is the same
   starting point; §10 is the decision.
4. **Each pass needs an explicit safety model, and the model is where the design lives.** kr32's
   document spends as much space on what the passes refuse to do (calls, exception landing
   regions, unknown stores, escaping addresses) as on what they rewrite. We should write the
   refusals down before the rewrites.
5. **The residual gap is caused by the architecture, not by pass count.** kr32 accepts a C++/C
   difference (22.8% vs 44.3%) because promotion skips any function with a handler, since "a
   `catch` clause reads the frame slot such a promotion rewrites". A representation where
   cleanup and handlers are edges rather than text regions does not have that constraint.

The transferable conclusion: **34 text passes were the right answer for a code generator that
had no IR to optimise.** This compiler has a frozen typed SSA IR with a verifier. Doing the work
at the level the representation already models is cheaper, checkable, and retargetable — and the
text pass shrinks to a bounded cleanup stage instead of becoming the optimiser.

---

## 6. Strategy: four layers, in this order

### Layer 1 — instruction selection quality (KR32 and KR16 backends)

No new machinery: `emit_instruction` stops emitting the most literal possible lowering. Four
changes, each measured in §3 and each independently testable:

| Change | Today | Becomes | Headroom |
|---|---|---|---|
| Constant operand → literal form | `MOV R1, #0` + `ADD R0, R0, R1` | `ADD R0, R0, #0` (and nothing at all when it folds to an identity) | 277 such `MOV #imm` pairs in the 8,388-instruction subset; 17% of instructions are `MOV` |
| `condbr` → one conditional branch | compare materialised to 0/1 via `MOV R2,#0` / `JZ` / `JMP` / `MOV R2,#1`, stored to a slot, reloaded, `CMP R0,#0`, `JNZ`, `JMP` | `CMP` + one `JCOND` in the sense the fall-through wants | 14.6% of KR32 instructions are branches, 20.8% on KR16; ~4 instructions per `condbr` |
| GEP + load/store → addressing mode | `LEA R0, [R7+24]` + `LOAD R0, [R0]` | `LOAD R0, [R7+24]`; `[Rs+disp]` and indexed `LOADX`/`STOREX` for pointer chains | 11.3% `LEA` on KR32, 12.7% on KR16 |
| Frame slots by liveness, not one per value | `insert()` in `binarytree.c` gets 184 bytes; `arithmetic.c` 432 bytes on KR32 | slots reused once a value is dead; frame size becomes a measured metric | frame bytes are 432 (KR32) and 196 (KR16) for `arithmetic.c` today, and scale with the IR, not the source |

KR16 gets one extra selection rule from its profile: an unconditional branch should be `BRA`
where it is in range and `JMP` otherwise (`docs/targets/kr16.md:39-49` records that the backend
does not select `BRA` and the assembler does not relax it). On a 64 KiB target, branch width is
a first-class selection decision, not a peephole.

**Why first:** every item is a local change inside one function of one target's backend, none of
it needs a new representation, and together the four items address the largest measured shares
(mem 39.1%, mov 17.0%, addr 11.3%, branch 14.6% on KR32). It also makes every later measurement
honest: until selection is decent, an IR pass's measured win is contaminated by what the emitter
does with the IR it is handed.

### Layer 2 — an IR pass manager and the safe passes

New files under `src/ir/passes/`, one file per pass, driven by an explicit list per optimisation
level, with the **verifier run after every pass** (it already exists: `docs/ir.md:104-119`).
A pass is `bool run(Function&)` / `bool run(Module&)` returning "changed", and the driver iterates
a fixed-order list to a fixed point or a bounded count — fixed iteration order is what keeps output
byte-identical.

Order within the layer follows the dependency graph, not the plan's prose:

1. **Constant folding and algebraic identities** — IRGen currently emits `sub i32 0, 7` un-folded
   (`arithmetic.c` dump), so this is pure missing work. Pure ops only; never fold something that
   can trap; division by zero stays.
2. **Copy propagation and local CSE** — dominated by the IR's own SSA form; no aliasing needed
   because a value's identity is its definition.
3. **DCE** — delete pure instructions with no users. Loads and stores are not pure; a load from a
   non-volatile slot with no users can go, a store cannot unless a later pass proves it dead.
4. **Single-block alloca promotion (mem2reg without `phi`)** — promote an alloca whose every
   store and load is in one block and whose address never escapes. This is the
   dependency-free subset of the plan's mem2reg and needs no phi lowering. It removes the
   alloca, its frame slot and its `V` record, so it is only legal for allocas with **no** debug
   record (§10, option A).
5. **CFG simplification** — remove unreachable blocks, merge a block into its single predecessor
   or single successor, thread a branch through an empty block. These are what make the
   branch-materialisation pattern disappear *after* Layer 1 changed its shape.
6. **Full mem2reg with `phi`** — only after the backends lower `phi` (F1). Requires critical-edge
   splitting and a dominance frontier, and it is the pass that makes a later register allocator
   worthwhile.
7. **Global CSE / value numbering** — last, because it is the pass most likely to be wrong and the
   one whose absence costs least.

### Layer 3 — register allocation

The allocator exists and is tested but consumes IR values (`src/backend/register_allocator.h:30`)
while the emitter produces strings. Two routes:

- **Route A (structural): introduce a machine IR.** A per-function list of blocks each holding
  machine instructions with explicit defs/uses, virtual registers and a target opcode, then
  allocate, then print. This is how the allocator gets wired, and it is also what makes Layer 1
  expressible as patterns instead of string concatenation. Cost: a new backend representation and
  a rewrite of both emitters.
- **Route B (incremental): promotion passes at the machine level.** kr32's approach: find
  straight-line spans containing no call, assign a register the function never mentions otherwise,
  rewrite loads/stores. Cheaper, and it can be done inside the current emitter — but it is the
  pass that forced kr32's debug-format change, and it exists because there is no allocator.

**Recommendation: Route A, after Layer 2's mem2reg**, with Route B explicitly rejected as the
permanent design and permitted only if a size or speed emergency appears first. The reasoning is
in §5: route B is affordable once, and expensive every year afterwards.

KR16 should not automatically join this layer. Five registers against a frame-per-value emitter
means an allocator can spill more than it saves; the metric there is **image bytes**, not
instructions, and the honest first step is selection and frame-slot reuse (Layer 1), measured.

### Layer 4 — a bounded text peephole

Keep `peephole.cpp`, make it the last 3–5% rather than the mechanism:

- store→load forwarding of a frame slot within a block, **with a branch-target check**: fold
  across a `.Ldbg` label, never across a label some branch targets;
- redundant jump removal (a `JMP` to the immediately following label);
- `ADJSP #0` and empty prologue/epilogue removal;
- **hard budget: one file, a few hundred lines, no CFG reconstruction, no register model.** If a
  proposed peephole needs either, it belongs in Layer 1 or Layer 2.

Any pass that moves instructions must respect the two structural constraints §3.3 and §2 name:
`.Ldbg` labels carry the line table, and the `.cpp_except` descriptor repeats the frame size as a
`.word` that a prologue change must keep in step.

---

## 7. The pass list

Ordered as the work should land. "Metric" is what the corpus table (§11) must show.

| # | Pass | Layer | Removes | Precondition | Must not | Metric |
|---|---|---|---|---|---|---|
| S1 | Literal operands | selection | `MOV #imm` + register operand | operand is a constant fitting the 11-bit inline field | change a value that is later needed in a register | `MOV` count |
| S2 | Direct conditional branch | selection | boolean materialisation, slot round trip, second jump | `condbr` with a comparison condition | reverse a condition whose operands are `i1` from `select` | branch count |
| S3 | Addressing modes | selection | `LEA` + indirect load/store | address is `gep` with constant offsets | fold across a `volatile` or atomic access | `LEA` count |
| S4 | Frame-slot reuse | selection | dead slots | value is dead at that point | reuse a slot named by a `V` record while it is live | frame bytes |
| S5 | `BRA`/`JMP` selection (KR16) | selection | one word per in-range branch | displacement fits the B-type reach | emit `BRA` out of range | image bytes |
| E1 | Constant folding | IR | un-folded constant expressions | operands constant, op total | fold a trapping op or a `volatile` operation | instruction count |
| E2 | Copy propagation / local CSE | IR | duplicated pure computation | SSA value identity | merge loads or anything with memory effects | instruction count |
| E3 | DCE | IR | unused pure instructions | no users, no side effect | remove `volatile` loads, stores, calls, atomics, EH edges | instruction count |
| E4 | Single-block alloca promotion | IR | alloca + load/store round trips | one block, address does not escape, **no `V` record** | promote a named variable (debugger would lie) | instruction count, frame bytes |
| E5 | CFG simplification | IR | unreachable/empty blocks, jumps to jumps | no EH edge crosses the block | break a landing-pad region | branch count |
| E6 | mem2reg with `phi` | IR | alloca round trips across blocks | **backends lower `phi`**; critical edges split | promote an escaping or `V`-recorded alloca at `-O1` | instruction count, frame bytes |

E5 has landed and is the largest single-pass win since S3: an empty block is a branch per `if`
statement, and IRGen leaves one behind after most of them. `kr32/libc.c` falls from 10,476 to 10,131
IR instructions and the corpus at `-O1` by 280 (KR32) and 88 (KR16). E6's precondition is larger than
it looks — `Opcode::Phi` exists in the IR but **neither backend lowers it** and IRGen never creates
one, so `phi` lowering in both backends, edge splitting and the promotion pass belong to a single
change.
| E7 | Global CSE | IR | repeated pure expressions | dominance order fixed and deterministic | cross a call for a value derived from memory | instruction count |
| R1 | Machine IR + allocation | backend | frame traffic for temporaries | machine IR exists; liveness correct at EH edges | allocate across a call without a save; ignore ABI-fixed registers | instruction count, frame bytes |
| P1 | Slot forwarding (label-aware) | text | `STORE`/`LOAD` of one slot | nothing branches to the intervening label | fold across a branch target | instruction count |
| P2 | Redundant jump | text | `JMP` to the next label | target is the following instruction | remove a jump that a descriptor names | instruction count |

`docs/ir.md` freezes the instruction set; none of E1–E7 needs a new opcode, so **no IR ADR is
required**. E6 needs `phi` lowering, which the IR already models and the backends do not
implement — that is completing the frozen contract, not changing it.

**E1 and E2 have landed, and E2's measurement changes the order below it.** E2 removes **2
instructions on the whole corpus**, because IRGen loads every variable read: the same source
expression twice produces `%a.load` and `%a.load.3`, different SSA values, so "SSA value identity"
almost never holds. `kr32/libc.c` sits at 10,476 IR instructions at `-O1` with and without E2. So
**E4 (alloca promotion) is the enabler for E2, E3 and E7** rather than merely the next row: promotion
is what turns repeated loads into repeated uses of one value, and the passes above it start paying
only after it. The ordering in the table is otherwise unchanged.

---

## 8. The safety contract (write this before the rewrites)

Each pass must state, in its own header comment, what it refuses. The project-wide floor:

1. `volatile` and atomic accesses are never removed, reordered relative to each other, or
   combined (§8.4).
2. No type-based aliasing assumptions, at any level, ever (§8.4). Two pointers of different
   types may alias here.
3. Exception edges, landing pads, cleanup edges and the `.cpp_except` region table survive every
   transformation; a pass that changes a prologue keeps the descriptor's frame size in step
   (`kr32_backend.cpp:1137-1152`).
4. `.Ldbg` line labels survive every transformation, and a value may never be moved across one in
   a way that changes the line a fault reports. `-O1` guarantees line/function mapping
   (`plan.md` §8.7), so this is a real obligation, not a nicety.
5. No named variable's frame slot is deleted or promoted while KDBG can only describe a frame
   offset (§10).
6. Determinism is byte-exact (§15.6): pass order is a fixed list, iteration over containers is
   ordered or sorted, and no pass may embed an address, a timestamp, or a pointer-derived name in
   its output.
7. The verifier runs after every IR pass and a failure aborts compilation, not "warns and
   continues" (`docs/ir.md:119`).

---

## 9. Testing and measurement

Three tiers, because one is not enough:

**Tier 1 — IR-level unit tests** (`tests/unit/test_passes.cpp`). Source string → IR → pass → the
IR dump compared as text. Text comparison is legitimate here because the dump is deterministic and
the pass's contract *is* the IR shape. One behaviour per test, plus a negative test per safety
rule (a `volatile` load survives DCE; an escaping alloca is not promoted; a `catch` region
survives CFG simplification).

**Tier 2 — shape assertions on emitted assembly.** Structural predicates, not golden bytes:
"no `STORE Rn, [R7+k]` is followed by a `LOAD Rm, [R7+k]` in this function", "this `condbr`
emits exactly one conditional branch", "frame size for this function is ≤ N bytes". Golden
assembly files would fail on every unrelated improvement and teach nothing about the property
being asserted. Note that the one existing assembly assertion, `test_irgen.cpp:164`'s
`MOV R1, #4`, is incidental to what that test is about and should become a semantic check.

**Tier 3 — the executed fixtures, on both targets, at both levels.** This is the real safety net
and it already exists: fixtures are linked and run under the KR32 and KR16 VMs (`KRC_KR32_VM`,
`KRC_KR16_VM`), and the last recorded gate is 813 tests on six configurations
(`implementation.md`, gate `2026-11-17T21:38:02`). The suite must run **twice — once per
optimisation level** — because a pass that is correct at `-O0` is the one that breaks at `-O1`
and vice versa. Cheapest implementation: a CMake option that sets the default level, with the two
paths registered as distinct test names, in the same spirit as kr32's front-end `optimize`
parameter, which pins the unoptimised shape in its unit tests while integration tests run
optimised.

**Determinism** is the fourth check and it is already a habit here: the same inputs built twice,
byte for byte (§15.6). Every pass landing must extend the existing determinism tests to cover
`-O1` output.

**Metrics.** Add `krc --dump-stats` (per function: instruction count, frame bytes, spill count;
per module: totals) and a checked-in table in this document, updated by whoever changes codegen,
in the style of kr32's benchmark table. The rule: *a pass lands only if it moves its metric on the
corpus and regresses no file*, and the numbers are quoted with the file set, the target and the
flags, because a percentage on an unnamed corpus is not a measurement.

---

## 10. The debug-info decision (F2)

`-O1` promises line/function mapping only (`plan.md` §8.7, `docs/ir.md:102`), so the *plan* does
not require variable locations at `-O1`. But the debugger is a shipped feature (G4), and a
debugger that shows `0` for a live local at `-O1` is a defect report waiting to happen, not a
documented limitation.

Three options:

| Option | Cost | Effect |
|---|---|---|
| **A. Promote temporaries only** — registers for IR values with no `V` record; named locals and parameters keep their slots | none beyond Layer 3 | every `V` record stays true; most of the win survives, because the frame traffic is dominated by temporaries (every IR value gets a slot today) |
| **B. Promote everything, document the loss** | one paragraph | debugger shows garbage for promoted locals at `-O1`; the kr32 failure reproduced deliberately |
| **C. Extend KDBG** — a variable-location record (register + half-open label range) | ADR + minor version + reader work in the debugger | full fidelity, the way kr32 went |

**Recommendation: A now, C when Layer 3 lands, never B.** Option A is exactly expressible with
today's format and needs no ADR; it also keeps the invariant "a `V` record is a promise" true
from the first `-O1` build. Option C's record should follow kr32's shape (`P <function> <name>
<register> <start_label> <end_label>`, range half-open, several records per variable allowed) —
it is already validated by an independent implementation, and the format is versioned, so a new
record type is a minor bump.

---

## 11. Proposed gates

IDs are provisional and deliberately not `G*` (those are plan §5.7's acceptance gates).

| Gate | Closes | Measured pass condition |
|---|---|---|
| **OG1** | Layer 1 (selection) | corpus instruction count down ≥25% against the §3.1 baseline on both targets; every executed fixture still green; the `-O` flag exists, both levels build byte-identically twice, and the incidental `MOV R1, #4` assertion is replaced by a semantic check |
| **OG2** | Layer 2 without `phi` (E1–E5) | further ≥15% on the corpus; verifier runs clean after every pass on every fixture; the negative tests (§9 tier 1) exist and fail when the safety rule is removed |
| **OG3** | `phi` lowering + mem2reg (E6, F1) | further ≥10%; a fixture whose loop-carried value survives promotion; IR dump shows `phi` and both backends compile it |
| **OG4** | Layer 3 (machine IR + allocation) | frame bytes down ≥50% on the corpus and instruction count down ≥15%; `-O1` still line-accurate under the debugger; KR16 measured separately and reported, not assumed |
| **OG5** | Layer 4 + `-O1` as a product | the bounded peephole landed, `-O1` documented in `docs/release-notes.md`, the metric table checked in and current, and the suite green at both levels on six configurations |

Each gate is recorded in `implementation.md` under `## Gate verification` in the existing shape,
with the corpus table as its evidence.

---

## 12. Decisions needed before code is written

1. **Q1 — Does this strategy become a plan change?** Adding an optimiser milestone and an `-O1`
   definition with gates is a material change to `plan.md` §8.8/§16.1, so it needs an Appendix A
   entry (D-xxx) once accepted. Recommendation: yes, one entry covering the pass list, the layer
   order, and the `-O0`/`-O1` guarantee boundary.
2. **Q2 — Structural route or fast path?** §6 recommends selection → IR → allocation → small
   text pass. The alternative is kr32's: start with a text pass chain and get 30–40% in two weeks,
   then live with the representation. Recommendation: structural route, with a hard rule that
   Layer 4 never exceeds a few hundred lines.
3. **Q3 — Does the first `-O1` promote named locals?** §10 option A (no) or B (yes, and document
   the loss) or C (extend KDBG first). Recommendation: A, then C.
4. **Q4 — Does KR16 get the same passes?** Its metric is image bytes on a 64 KiB target, its
   register file is five registers, and relaxation is mandatory. Recommendation: selection and
   frame-slot reuse yes, allocation only if measured to win, and never by assuming the KR32
   numbers transfer.
5. **Q5 — Where does the `-O` flag live and what is the default?** `plan.md` §15.1 already lists
   `-O0 -O1` and the checkin has no flag at all. Recommendation: implement `-O0` (default) and
   `-O1` as written, keep `-O1` non-default until OG5, and make the test suite run both.
6. **Q6 — Do we measure first or write a pass first?** Every number in §3 comes from reading
   emitted text with a script. A `--dump-stats` mode makes the metric a compiler output rather
   than a one-off script, which is what makes the "no regression" rule enforceable.
   Recommendation: `--dump-stats` in the first change, before the first pass.

---

## 13. What this document does not claim

- It does not claim any of the percentages will survive contact with the register allocator; they
  are the headroom each lever has *independently*, measured at `-O0` shape.
- It does not propose inlining, LTO, LICM, vectorisation, or cross-object optimisation: §8.8
  excludes them for v1, and `docs/ir.md:147-152` lists them as non-goals.
- It does not propose changes to the frozen IR (`docs/ir.md`), the object format, the ABI, or
  KDBG, except the KDBG extension that §10 option C would require as its own ADR.
- It ran no build. The measurements used the binary already in `build/windows-msvc-debug`; the
  test suite was not run and no pass was implemented.
