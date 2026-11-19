# Making `-O1` output work with the KR32 debugger

**Status:** plan, 2026-11-19. Nothing here is implemented; this is the shape the work should take.
**Relates to:** `docs/optimize.md` §7 (R1, S4) and §11 (OG4's "`-O1` still line-accurate" condition),
`docs/symbols.md` (the frozen KDBG format), ADR 0049 (allocator), ADR 0050, ADR 0051 (proposed).

## What exists, measured today

The container is documented in `docs/object.md` — *"KRO object and executable container"* — and it is
a two-stage pipeline: `krc -c` writes a relocatable `.kro`, `krld` writes a `.k32` executable that
carries `.text`, `.data`, `.cpp_except`, `.debug_info` and `.kdbg`. The VM loads the `.k32`.

The VM's debug interface is not one thing (`vm/krc_kr32.exe --help`):

```
debug <file>     dump debug information (M5)
dap <file>       Debug Adapter Protocol server (stdio or --port)
gui              open the windowed debugger (M6)
```

So a debugger speaks **DAP** and reads **KDBG**. Both were exercised for this plan: the DAP server
starts and exits 0 on EOF, so it can be driven by a script; `debug` and `krc --dump-symbols` both
print the same content.

KDBG record kinds (`docs/symbols.md`): `Source`, `Line` (address, source, line, column), `Function`
(name, start, end, frame size, result type), `Symbol`, `Variable` (function, name, **frame offset**,
size, type), `Type`, `Member`, `Build`.

The same program at both levels, linked and measured with `krc --dump-symbols`:

| | `-O0` | `-O1` |
|---|---:|---:|
| sources | 3 | 3 |
| line records | 11,229 | **11,189** |
| functions | 242 | 242 |
| symbols | 136 | 136 |
| **variables** | **768** | **768** |
| types | 67 | 67 |

Two facts explain why the variable count survives `-O1` at all: source variables are the IR values
named `<variable>.addr`, and both promotion passes **refuse those** (`docs/symbols.md`; `docs/optimize.md`
E4/E6), and the frame-slot omission added in this session drops slots only for values the allocator
kept in a register — which a source variable never is. So `-O1` currently keeps every named variable
in its frame slot, and that is the property a debugger depends on.

## What already works

- A `-O1` image loads and runs (`vm vm`, exit 0).
- `-O1` images carry complete function, variable and type records, and 99.6% of the line records.
- The suite pins the debug data at the default level: `krc_debug_c_variable`, `krc_debug_cxx_class`,
  `krc_debug_cxx_members`, `krc_debug_names`, `krc_debug_paths_relative` — all via `--dump-symbols` on
  a linked `.k32`.

## The gaps

**G1 — 40 source lines have no address at `-O1`.** 11,229 → 11,189. A breakpoint on such a line
either fails or slides to the next line. Cause not yet diagnosed: the candidates are a `.Ldbg` label
whose instruction was deleted, and a line whose only code was folded into a neighbour. Impact: a
debugger that is "line-accurate" is not, on 0.36% of lines. **Needs diagnosis before it needs a fix.**

**G2 — `Variable` records have no scope or range.** The record is function, name, offset, size, type:
nothing says *where in the function the variable is live*. At `-O0` with one slot per variable that
is harmless. It is a latent blocker for any future pass that reuses a variable's slot (S4, measured
at −28.6% frames and declined twice) or promotes a named variable: the debugger would show a value
from the wrong slot with no way to know. **This is a format change, and the format is frozen.**

**G3 — there is no `-O1` debug test.** Every debug test builds at the default level. Nothing asserts
that a `-O1` image's line addresses lie inside their function's range, that its variables resolve, or
that its function frame sizes match the emitted prologue.

**G4 — DAP is untested.** No test drives `vm dap`. The server runs and exits cleanly, so a harness is
possible, but nothing today would notice if a `-O1` image made it return no breakpoints.

**G5 — the promise is unverified.** `docs/optimize.md` §11's OG4 condition says "`-O1` still
line-accurate under the debugger" and its Status column cannot be filled truthfully today: no test
measures it, and G1 says it is 99.6% rather than exact.

## The plan

Ordered so that each step is verifiable on its own and the format changes come last.

**W1 — a DAP harness (no compiler change).** A script that starts `vm dap <image>`, speaks the
minimum protocol — `initialize`, `launch`, `setBreakpoints` on a known line, `configurationDone`,
`stackTrace`, `scopes`, `variables`, `continue`, `disconnect` — and asserts: the breakpoint is
reported verified, execution stops there, the frame names the expected function, and a named
variable reads its expected value. Run it on an `-O0` image first; it is the founder for everything
below. *Acceptance: one new test, green at `-O0`, driven entirely from the suite.*

**W2 — diagnose the 40 missing lines.** Compare the line records of the same function at both levels;
find which lines vanish and whether the instruction or the label went. Then choose a policy:
(a) record only lines that still have an address — honest, and a debugger slides naturally; or
(b) map a vanished line to the next surviving instruction, which keeps every breakpoint placeable but
can stop on a line whose code was folded elsewhere. (a) is the recommendation: a breakpoint that
cannot be placed is better than one that stops in the wrong place. *Acceptance: the policy is stated
in `docs/symbols.md`, a test asserts the invariant, and the number is in `implementation.md`.*

**W3 — `-O1` debug tests.** With W1 in place: every `krc_debug_*` test gains an `-O1` twin, and a new
test asserts two invariants over a corpus image built at `-O1` — every line address falls inside its
function's `start`–`end`, and every variable's `offset + size` falls inside the frame the function
record claims. *Acceptance: the twins pass, and the invariants hold on the corpus.*

**W4 — scope ranges in KDBG (format change; needs an ADR).** Add a live range to `Variable`
(start and end PC), so a debugger can report a variable as unavailable outside it. Required before
S4 can be considered safe, or before any pass promotes a named variable. It is a **symbol-format
change**, so it needs an ADR, a `docs/symbols.md` update, a VM-side reader change, and a version
bump; the freeze checklist in that document says what else.

**W5 — `<optimized out>`.** With W4's ranges, a variable outside its range reports as optimized out
rather than as a stale value. Small once W4 exists; impossible before it.

**W6 — documentation.** State in `docs/symbols.md` and `docs/release-notes.md` what `-O1` guarantees —
line accuracy with the policy from W2, and variable availability with the answer from W4/W5 — and
fill OG4's Status with the measured number instead of a claim.

## What this plan deliberately excludes

- **Promoting named variables at `-O1`.** It would shrink frames further and it would invalidate every
  `Variable` record. `docs/optimize.md` Q3 recommends against it until KDBG can describe the loss.
- **S4 slot reuse.** It is worth −28.6% frames and it is exactly the pass that needs W4 first. Plan
  it after W4, not before.
- **A second debug format.** DAP is the interface, KDBG is the payload; nothing here adds a third.

## Risks

- **G2's format change is the expensive one.** It touches a frozen format and the VM's reader
  together, and the freeze checklist exists because those two must move as one. Everything above it
  is test work with no format risk.
- **The 40 lost lines may be a symptom rather than a defect.** If W2 finds the label is dropped for a
  line whose code survives, the fix is in the emitter and cheaper than expected.
- **A DAP harness can rot.** It should assert behaviour a user would notice — a breakpoint hits, a
  variable reads correctly — rather than the shape of the JSON.
