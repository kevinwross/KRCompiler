# 0041 — One section per function, as the prerequisite for section GC

- Status: proposed
- Date: 2026-11-15
- Decision: each KR16 function is emitted into its own section, named for the
  function, so a linker can drop the ones nothing references
- Scope: object format

## Context

`plan.md` and the M2/M3/M6 checklists name section garbage collection as
outstanding linker work. Measuring what `krld` actually keeps shows why it
cannot be added to the current format.

A program with `main` and two functions nothing calls links without a word of
complaint, and `--map` shows all three:

```
.text 0x100 26312
main 0x67b4 0 global
unused_caller 0x6792 0 global
unused_helper 0x6780 0 global
```

The image is correct — the unused functions are simply present. Dropping them
requires the linker to see them as separate units, and it cannot: the object has
**one `.text` section per translation unit**, and the KR16 assembler has no
`.section` directive at all. The backend emits a single `.text` for every
function in the unit, and the object writer hardcodes `output.section = 0`.

`krld` also has no `--gc-sections`. It offers `--map`, `--layout` and
`--print-size`, and nothing that collects.

Object-level collection — dropping a whole object's `.text` when nothing
references any symbol in it — would work in the current format and would help
archives. It would not drop an unused function from a unit that something else
in that unit references, which is the case worth collecting: template instances
and inline definitions are emitted per unit and most are unused.

## Decision

- A KR16 function is emitted into its own section, named `.text.<symbol>`.
- The assembler accepts `.section <name>` and switches the current section,
  with `.text` and `.data` as fixed names for the common cases.
- The object writer records the section index rather than assuming 0, and the
  symbol table's section field is the symbol's own section.
- `krld` gains `--gc-sections`, which keeps the sections reachable from the
  entry point and drops the rest, before relocation is applied.

## Consequences

- Section GC becomes expressible, and M2/M3/M6's checklist lines can be closed.
- The object grows a section per function: a unit with forty small functions has
  forty sections instead of one. Section headers are a real cost and the
  determinism checks must be re-run, since section order becomes a determinism
  surface it was not before.
- Every function's symbol changes section index, so every object and every image
  changes. This is a wider blast radius than ADR 0040, which changed a binding
  and left the layout alone.
- This ADR is **proposed and unimplemented**. Until it lands, `krld` has no GC
  and the `--gc-sections` flag does not exist.
