# Optimizer handoff

Everything a fresh session needs to continue the optimisation work without rediscovering it.
Written at the end of a session that ran out of context before it could implement a pass.

**Written:** 2026-11-18. **State:** `origin/master` at `7832371`, tree clean, suite **828/828** on
the four routine rows, gate recorded at `7c4a452`.

---

## 1. What is authorised, and by what

| Document | What it settles |
|---|---|
| `plan.md` §8.8 + **D-049** (Appendix A) | The optimisation contract. `-O0` is straightforward lowering; **selection quality is not an optimisation level** — S1–S5-style work applies at every level including `-O0`. `-O1` is §8.8's list plus global CSE and two text peepholes. Named locals **are** promoted at `-O1`, with the debug-info cost documented. `--dump-stats` is a precondition for any pass. |
| **ADR 0049** (`docs/decisions/0049-register-allocation-wiring.md`) | Accepted. Wires the register allocator as `-O1`-only behaviour under five binding constraints. |
| `docs/optimize.md` | The strategy and the pass list (§7). **Not approved as a plan**, but its pass list is what D-049 authorises. |
| `docs/optimize-review.md` | Five findings against that document; the ones that changed it are already folded into D-049. |

**Do not implement an optimiser pass before reading D-049.** The review's central finding was that the
proposal exceeded §8.8; D-049 is the resolution and it reclassifies rather than deletes.

---

## 2. Baselines — the measurement, and how to reproduce it

Recorded in `benchmarks/size.tsv` and `benchmarks/ticks.tsv`, and **reproduced exactly** at `8511e50`
(so they are deterministic across a rebase and three doc commits):

| Measure | Value |
|---|---|
| `kr32/libc.c` | 1,583,372 bytes / 36,497 instructions |
| `kr16/libc.c` | 121,348 bytes / 9,307 instructions |
| `bench` on KR32 | 272,602 ticks, exit 42 |
| `bench` on KR16 | 267,620 ticks, exit 42 |

Reproduce from the repo root, with the build already configured:

```powershell
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$env:PATH="$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
$krc="$($PWD)\build\windows-msvc-debug\host\bin\Debug\krc.exe"
$krld="$($PWD)\build\windows-msvc-debug\host\bin\Debug\krld.exe"
$stage="$($PWD)\build\windows-msvc-debug\stage\sysroot"

# Size
& cmake "-DKRC=$krc" "-DROOT=$($PWD)" "-DTARGETS=kr32;kr16" "-DOUT=$($PWD)\benchmarks\size.tsv" -P benchmarks\MeasureSize.cmake

# Ticks
& cmake "-DKRC=$krc" "-DKRLD=$krld" "-DROOT=$($PWD)" "-DSTAGE=$stage" `
        "-DKR32VM=$($PWD)\build\vm\krc_kr32.exe" "-DKR16VM=$($PWD)\build\vm\krc_kr16.exe" `
        "-DOUT=$($PWD)\benchmarks\ticks.tsv" -P benchmarks\MeasureTicks.cmake
```

### The quoting trap that cost eight rounds

**Every `-D` value must be quoted.** An unquoted `-DKRC=$krc` reaches CMake as the literal token
`$krc`, the nested `cmake -P` cannot start the compiler, and `execute_process` reports
`no such file or directory` — which looks like a broken script and is not. The script was never wrong.

### What the workloads are

- **Size**: `targets/kr32/runtime/libc.c` (2,915 lines), `targets/kr16/runtime/libc.c` (794 lines, a
  *different* file), and the 26 extracted corpus sources. `libc.c` compiles for KR32 but is **rejected
  by the KR16 profile**, and the table records that as a row rather than an omission.
- **Ticks**: `benchmarks/programs/bench.c` — five kernels (arithmetic loop, nested loop, recursion,
  by-value struct copies, pointer walk). Each is checked against an independently computed value and
  `main` returns 42 only when all five agree, so a pass that speeds up wrong code **fails** the
  measurement instead of improving it.
- The two VMs agree on neither the label nor the stream: KR32 prints `ticks=N` on **stdout**, KR16
  prints `cycles=N instructions=M` on **stderr**. The runner handles both; do not "simplify" that away.

---

## 3. Where to start: S1, literal operands

**Scoped but not implemented.** The finding that makes it small:

- The encoder **already emits immediates** inside ALU instructions —
  `src/backend/kr32_backend.cpp:1404` is `ADD R0, R0, #offset`.
- Only the **operand loader** materialises constants into registers first —
  `src/backend/kr32_backend.cpp:340` and `:456` are `MOV <reg>, #imm`, with `:413`/`:415` and
  `:680`/`:681` covering the 64-bit paths.

So S1 is *not* a new emission mechanism. It is teaching the loader to hand a `ConstantInt` to the
instruction instead of moving it into a register first.

**Size it honestly before committing.** The loader is on the path of every instruction that takes an
operand, so the change is one place but wide. The 828-test suite is the real check, and a **null
measurement is a legitimate result** — D-049 classifies these as lowering quality, so if the numbers do
not move, the lowering was already competent and the next item is the right one to attack.

### Suggested order for the whole plan

1. **Phase 1 — S1–S5** (lowering quality, applies at every level incl. `-O0`).
2. **Phase 2 — E1–E7** (IR passes behind `-O1`), one commit and one measurement each.
   **E6 (mem2reg with `phi`) needs phi lowering in the backends**, which the IR models and the
   backends do not implement. It is the largest item in the phase and **may deserve its own goal** —
   defer it explicitly rather than starting a three-round change.
3. **Phase 3 — R1 register allocation** under ADR 0049. Write the `-O0` byte-identity assertion
   **first**, before the wiring, so it cannot be satisfied by accident later.
4. **Phase 4 — P1–P2 text peepholes**, only after the earlier layers are measured.

---

## 4. Rules that this project actually enforces

From `AGENTS.local.md` (read it — it is short and it is the repository's own layer):

- **Measure before changing.** Reproduce the behaviour, record it, including negatives.
- **Never claim a check passed that was not run.** A fixture that only compiles is not evidence.
- **Smallest tier that fits.** Documentation needs no build. `src/frontend`, `src/backend`, `src/ir`
  need the inner loop (`windows-msvc-debug`) **and** the full suite.
- **A change adding a standard-library call or a new header must also be built by a second compiler**
  (`linux-clang-debug` or `linux-gcc-debug`) before pushing. This rule exists because
  `706109b` used `std::strlen` without `<cstring>`: MSVC compiled it through a transitive include,
  GCC and Clang refused, and HEAD was unbuildable on Linux while all 821 tests were green.
- **Never push a failing tree.** Revert rather than publish.
- **Record a host gate at the four routine rows** at each milestone. Format in `AGENTS.local.md`.
- **Keep `implementation.md` updated in the same commit**, and correct any claim a change makes false.

---

## 5. Traps that have already cost time

| Trap | Detail |
|---|---|
| Unquoted `-D` values | See §2. Eight rounds lost. |
| PowerShell string surgery | Constructing replacement strings failed four times (missed matches, a literal `` `r`n ``, a backspace character). **Use the `edit` tool** — it never missed. |
| Deleting `build/<preset>/stage/` alone | It holds `stage/tests/`, which nothing recreates: 740 link tests fail with a message naming the output file. A full gate deletes the whole build dir and reconfigures. |
| `-fsyntax-only` | Does not reach the IR verifier. A rejection test that expects a verifier message must **compile**, not just parse. |
| Trusting a grep | A pattern that does not match looks like an absence. Verify the query before concluding. |
| Assuming a doc is current | Three separate claims were falsified by measurement in one session, including a named list of files that were not in the corpus. |

---

## 6. Deferred, with its mechanism recorded — do not restart blindly

**Item 14: `get<I>` / qualified use of a class-template instance's static member.**
Deferred by operator decision. `get<I>` remains absent from `<tuple>`, which the header documents.

What is **known**, so it does not need rediscovering:

- The parser already consumes `Name<args>` as a name prefix (`aee29f7`, shipped).
- Sema resolves the scope to the instance tag (`Box__int`) and can read the member from
  `record->methods`, which the record carries at analysis time.
- The failure then lands in **code generation**: `identifier 'Box<int>::pick' has no IR binding`.
- The typedef form (`IntBox::pick`) fails earlier, in `lookup_tag`.

**A sixth attempt should start by printing the emitted symbol table**, to learn what name the emitter
gives an instantiated class template's static method — or whether it emits it at all, since a
qualified-only use may never register the instance as used. Five previous attempts edited lookup and
codegen; none looked at what was emitted.

---

## 7. Out of scope — decided, do not reopen

- **Configuration pruning** (build time accepted).
- **Register allocator wiring** — authorised by ADR 0049, but it is Phase 3, not a starting point.
- **M8 `double`** — settled as an IP16 profile boundary, not a defect. The two files the profile
  actually refuses are `tier1/floats.c` (`double`) and `tier2/cxx_tls.cpp` (`long long`), asserted by
  `krc_cxx_kr16_rejects_double` and `krc_cxx_kr16_rejects_longlong`.
- **`v1.0`/`v1.1` tags** — no tag until M10 closes.
- **The ARM64 host** — the Raspberry Pi owns that row and gates every incoming change.

---

## 8. First three commands

```powershell
git log --oneline -5                 # confirm you are at or past 7832371
git status --short                   # must be empty
# then read: implementation.md's entries from 7832371 backwards, and AGENTS.local.md
```

Then start S1 at `src/backend/kr32_backend.cpp:340`, `:456` and `:1404`, and measure with §2 before
and after in the same commit.
