# 0062 — KR232 integer offsets reach the generic form

- Status: accepted
- Date: 2026-11-26
- Decision: how the kr232 assembler encodes an integer address whose displacement does not fit the 8-bit Normal form
- Scope: the kr232 assembler in krcompiler; no new syntax, and no ISA, ABI, object, symbol, target or IR change

Reviewed 2026-11-26 in `docs/planning/0062-kr232-integer-offset-forms-review.md`;
findings 1–5 are addressed in this text.

## Context

### The failure

The kr232 assembler reaches only the 8-bit displacement form for an integer
address. A displacement of +128 or more, or −129 or less, fails to assemble
even though the ISA defines the encoding:

```text
$ krc -c --target kr232 big.s -o big.o
error: KR232 assembler: line 4: displacement out of the signed byte range
```

with `big.s` containing `ld r0, [r2+1000]`. The boundary is exactly the signed
byte: `[r2+127]` and `[r2-128]` assemble, `[r2+128]` does not, and `st`,
`ldbu`, `ldhu`, `stb`, `sth`, and `lea` through a general base fail the same
way. Floating point is not affected: `fld f0, [r2+1000]` assembles because the
FP path falls back to the `FA` 32-bit-displacement form.

### Where the limit is

The encoding-selection code is in `kr232_parser.cpp`, not in
`kr232_assembler.cpp`. That split matters when looking for the defect:
`kr232_assembler.cpp` drives the parse and emits sections; `parse_instruction`
in `src/asm/kr232_parser.cpp` chooses the byte form.

- The load path ends at `return encode_normal(entry.normal_op, ra, address.base, address.disp, error);`
  (`src/asm/kr232_parser.cpp:467`).
- The store path does the same with the stored register:
  `src/asm/kr232_parser.cpp:512`.
- `lea` through a general base is `src/asm/kr232_parser.cpp:481`.
- `encode_normal` rejects any displacement outside −128…+127 with
  "displacement out of the signed byte range"
  (`src/asm/kr232_encoder.cpp:165-168`).
- `encode_generic` already encodes modes 5, 6 and 7 correctly
  (`src/asm/kr232_encoder.cpp:271-341`), and its unit test already covers modes
  5 and 7 (`tests/unit/test_kr232_encoder.cpp:88-106`). The parser calls it for
  exactly one instruction, `SXTH` (`src/asm/kr232_parser.cpp:272`); no integer
  load, store or `lea` reaches it.

The missing piece is therefore one dispatch decision, not an encoder or
machine feature.

### The ISA already has the form

`kr232-isa.md` §9 defines mode 6 as `[Rb+d16]` and mode 7 as `[Rb+d32]`, both
**generic only**; §8.1 defines the `F0` specifier byte (`0 MMM RRRR`) and the
extension order, and Appendix A has vectors for both modes. The sibling kr232
tree's decoder already reads them and prints them
(`../kr232/src/decoder.cpp:201-235`), and the FP `FA` form uses the same
32-bit displacement machinery.

### One text, one encoding

The repository already has a shortest-form policy: the parser emits the 2-byte
compact form for `[r3]` and the 3-byte Normal form for `[r3+4]`, even though
`F0` mode 5 could encode the same address. `test_kr232_roundtrip.cpp:17-20`
excludes the `(generic)` alternates so that one text has one encoding; Appendix
A marks **eight** such rows, not seven (the fixture comment says seven, which is
a miscount), and the fixture contains no `F0` row at all. This decision extends
that ladder by two rungs; it does not invent a new policy.

### What the gap costs today

- Hand-written assembly cannot express a form the ISA defines.
- The kr232 backend already carries workarounds for it. `frame_load`,
  `frame_store` and `frame_store_float` emit `mov r8, r13` + `sub r8, #offset`
  and access `[r8]` when `offset > 128`
  (`src/backend/kr232_backend.cpp:385-420`); the comment names "an unencodable
  displacement". The workaround costs two instructions and a scratch register
  per deep slot.
- The positive-offset variadic parameter load is not guarded:
  `ld r5, [r13+<incoming>]` at `src/backend/kr232_backend.cpp:480`, with
  `incoming = 8 + 8*i`, reaches 128 at the sixteenth named parameter and would
  hit the same assembler error.

`MOV` is not part of this gap. The ISA gives `MOV` only register and immediate
operands (`kr232-isa.md` §8.1, operation `0x00`), and the assembler correctly
rejects `mov r0, [r2+4]` with "unsupported instruction or operand shape". No
`MOV` memory alias is being added.

## Decision

### Form ladder

For an integer memory operand `[B+d]`, the complete ladder is:

| Condition | Form | Length | Selection |
|---|---|---|---|
| absolute constant | Register or `F1` | 5/6 | existing branch, before the helper |
| PC-relative | `F9` or `F1` | 6 | existing branch, before the helper |
| post-increment / pre-decrement | Compact | 2 | existing branch, before the helper |
| `d == 0` and a compact form exists | Compact mode 2 | 2 | existing branch at `kr232_parser.cpp:464-466`, before the helper |
| `-128 <= d <= 127` | Normal, mode 5 | 3 | helper |
| `-32768 <= d <= 32767` | `F0` generic, mode 6 | 6 | helper |
| otherwise | `F0` generic, mode 7 | 8 | helper |

The helper covers only the last three rows. The four rows above it are the
existing branches: the zero-displacement compact check runs before the helper
is called, so the helper never sees `d == 0` and would encode it as Normal if
it did. That is deliberate; the caller keeps the compact choice and the helper
is a "Normal or generic" selector.

`d` is the signed `int32_t` the address parser already produces; mode 7
therefore always fits and there is no fifth case.

### One helper

Add a parser helper that both the load/store blocks and the general-base `lea`
use. The encoder already has everything it needs:

```cpp
// Normal or generic encoding for [base + displacement], called after the
// compact, absolute, PC-relative and auto-increment branches are ruled out:
// mode 5 inside d8, otherwise F0 mode 6 (d16) or mode 7 (d32). `data` is Rd for
// a load or LEA and Rs for a store. The address is specifier 2 in every case,
// even though assembly writes a store's address first (kr232-isa.md §8.1).
std::optional<EncodedInstruction> encode_integer_address(
    unsigned normal_op, unsigned generic_op, unsigned data, unsigned base,
    std::int32_t displacement, std::string& error) {
    if (displacement >= -128 && displacement <= 127) {
        return encode_normal(normal_op, data, base, displacement, error);
    }
    const auto mode = static_cast<std::uint8_t>(
        displacement >= -32768 && displacement <= 32767 ? 6 : 7);
    return encode_generic(generic_op, {0, static_cast<std::uint8_t>(data), 0},
                          {mode, static_cast<std::uint8_t>(base), displacement}, error);
}
```

### Operation bytes

Extend the existing `LoadStore` table in `kr232_parser.cpp` with the `F0`
operation byte so the mnemonic-to-opcode mapping stays in one table:

| Mnemonic | Normal op | `F0` op | | Mnemonic | Normal op | `F0` op |
|---|---|---|---|---|---|---|
| `ld` | 0 | `0x0E` | | `st` | 1 | `0x0F` |
| `ldbu` | 2 | `0x10` | | `ldbs` | 3 | `0x11` |
| `stb` | 4 | `0x12` | | `ldhu` | 29 | `0x1C` |
| `ldhs` | 31 | `0x1D` | | `sth` | 30 | `0x1E` |

`lea` is handled outside that table: Normal op 14 and `F0` operation `0x13`.

### Call sites and operand order

Replace the final `encode_normal` in the load block (line 467), the store block
(line 512) and the general-base `lea` (line 481) with the helper.

In the store block the parser's `rb` is the **stored register** and
`store_address.base` is the base; pass them in that order. The `F0` encoder
writes specifier 1 from its first operand and specifier 2 from its second, so
putting the address first because the source does produces a store to the wrong
operand. This is the one hand-encoding trap the helper must not reproduce.

Leave untouched:

- the absolute path (`encode_ld_absolute`, `encode_st_absolute`,
  `encode_long_integer`) — a constant address stays 5 or 6 bytes;
- the PC-relative path (`encode_wide`, and `F1`/`F9` for `lea`) — PC data
  displacements are relative to the instruction address, not the next
  instruction;
- the `sp` `lea` path (`F1` op 4 / `F9` op 5) — it already has a wide form;
- the post/pre and zero-displacement compact paths.

### Bounds and errors

`parse_address` accepts only a numeric displacement
(`src/asm/kr232_parser.cpp:129-172`), so this path never sees a relocatable
symbol and the helper needs no relocation support. `encode_generic` re-checks
the d16 range and the one-extension rule; no new diagnostic text is required.
A displacement that fits none of the forms cannot occur because `d` is already
a 32-bit signed value, which mode 7 represents.

### Out of scope

- No `MOV` memory form, and no assembler pseudo-instruction.
- No change to the FP path: `fld`/`fst` already fall back to `FA`
  (`src/asm/kr232_parser.cpp:561-563`).
- No change to the backend in this ADR. Removing the `offset > 128` workaround
  is a separate change because it alters generated code and `-S` output.
- No change to `kr232-isa.md` or `plan.md`: the encoding is in the frozen 1.0
  baseline, so this is not a §15 decision, a §17 row, or an Appendix A entry.

## Consequences

- `ld r0, [r2+128]`, `st [r2+1000], r0`, `ldhu`, `lea`, and the rest assemble
  to the form the ISA defines. Inputs that assembled before are byte-identical,
  because the fallback runs only where `encode_normal` previously failed.
- The disassembler prints displacements in decimal (`kr232-dis` text comes from
  `../kr232/src/decoder.cpp:41-42`), so `[r3+0x1234]` comes back as `[r3+4660]`.
  The round-trip check compares text, so its expected column must say `4660`,
  not `0x1234`.
- The assembler chooses mode 6 for a displacement that fits 16 bits, even
  though the Appendix A vector for `ld r2, [r3+0x1234]` shows mode 7. That
  vector demonstrates mode 7 (as the mode-5 vector demonstrates mode 5 for
  `[r3+4]`, which the parser already encodes as Normal); it is not the
  canonical choice for the source spelling. The vector fixture excludes the
  generic alternates for exactly this reason.
- The backend's `R8` workarounds become optional. Until they are removed,
  generated code is unchanged. Removing them would shrink deep-frame code and
  must re-baseline `-S`, determinism and any goldens in the same change.
- The unguarded variadic path at `kr232_backend.cpp:480` starts to assemble for
  large positive frame offsets.
- Cost: one parser helper, one table column, and the tests below. No ABI,
  object, symbol, target, IR or ISA change.

## Verification

### Encoder unit test

`tests/unit/test_kr232_encoder.cpp` already proves `encode_generic` handles the
modes; extend it with the exact bytes the new parser path will produce:

```text
encode_generic(0x0E, {0,2,0}, {6,3,0x1234})  F0 0E 02 63 34 12
encode_generic(0x0E, {0,2,0}, {7,3,0x1234})  F0 0E 02 73 34 12 00 00
encode_generic(0x0F, {0,2,0}, {6,3,-129})    F0 0F 02 63 7F FF
```

### Parser unit test

`tests/unit/test_kr232_parser.cpp` is the right place for the ladder, since it
tests `parse_instruction` directly. Add a case that asserts the boundary bytes:

```text
ld r2, [r3+127]      C0 23 7F
ld r2, [r3+128]      F0 0E 02 63 80 00
ld r2, [r3-129]      F0 0E 02 63 7F FF
ld r2, [r3+32767]    F0 0E 02 63 FF 7F
ld r2, [r3+32768]    F0 0E 02 73 00 80 00 00
st [r3+128], r2      F0 0F 02 63 80 00
lea r2, [r3+128]     F0 13 02 63 80 00
ldhu r2, [r3+128]    F0 1C 02 63 80 00
```

The `st` row is the one that fails if the address is passed as specifier 1.

### Assembler → disassembler round trip

Extend the table in `cmake/CheckKr232EncodingRoundTrip.cmake`. Expected text is
the canonical `kr232-dis` text, in decimal:

```text
ld r2, [r3+127]        ld r2, [r3+127]
ld r2, [r3+128]        ld r2, [r3+128]
ld r2, [r3-129]        ld r2, [r3-129]
ld r2, [r3+32767]      ld r2, [r3+32767]
ld r2, [r3+32768]      ld r2, [r3+32768]
ld r2, [r3+0x1234]     ld r2, [r3+4660]
st [r3+128], r2        st [r3+128], r2
ldbu r2, [r3+128]      ldbu r2, [r3+128]
ldhu r2, [r3+128]      ldhu r2, [r3+128]
stb [r3+128], r2       stb [r3+128], r2
sth [r3+128], r2       sth [r3+128], r2
lea r2, [r3+128]       lea r2, [r3+128]
ld r2, [sp+128]        ld r2, [sp+128]
```

The check matches expected strings with `string(FIND ...)`, not whole lines, so
do not add two rows where one expected string is a substring of another (for
example `[r3+12]` alongside `[r3+128]`); the shorter row would pass for the
wrong reason.

### Executed row, when a VM is configured

The round trip proves the bytes decode; it does not prove the machine executes
modes 6 and 7. When `KRC_KR232_VM` is configured, add a `BuildAndRunKr232.cmake`
row in the shape of the existing `krc_kr232_run_*` rows. The fixture is a `.s`
file whose `main` forms a base below a known word and loads through the new
form:

```asm
.globl main
.text
main:
  ld  r3, #value
  sub r3, #128          ; base = value - 128
  ld  r2, [r3+128]      ; F0 mode 6, the form this ADR adds
  ret                   ; crt0 exits with R2 as the status
.data
value:
  .word 42
```

with `CRT0=targets/kr232/runtime/crt0.s` and `EXPECT_EXIT=42`. This fixture
fails on the current tree at the `ld r2, [r3+128]` line with the displacement
diagnostic, so it exercises the new form rather than the old path. On a host
with no VM, record the row as not run; the round trip and the parser tests are
not a substitute for it.

### Registration precondition

The round-trip row is registered inside `if(EXISTS "${kr232_dis}")`
(`tests/CMakeLists.txt:212`, closing at `:344`), and `kr232_dis` is derived
from `KRC_KR232_VM`. Where that path is not configured the row is absent, and
`ctest -R kr232` passes with fewer tests instead of failing. Confirm the row is
registered before claiming this verification:

```text
ctest --preset windows-msvc-debug -N -R krc_kr232_encoding_roundtrip
```

A missing row is **not run**, not a pass. The implementing change should
register the round-trip test whenever a `kr232-dis` path is known — from
`KRC_KR232_VM`'s directory or a dedicated `KRC_KR232_DIS` cache variable — and
treat a configured VM path with no `kr232-dis` beside it as a configuration
error. The row needs `krc`, `krld` and the disassembler; it does not run the VM.

### Prove the check bites

The new rows must fail on the current tree with the displacement diagnostic
above, and pass after the change. Remove the fallback in a scratch build and
confirm they fail again; a row that passes with the fallback removed is not
testing the fallback. This is the same "proved to bite" practice the encoding
round-trip check already records.

### Commands

```text
ctest --preset windows-msvc-debug -N -R krc_kr232_encoding_roundtrip
ctest --preset windows-msvc-debug -R kr232
krc_tests
```

and the same two `ctest` invocations on `linux-gcc-debug`, plus the
`krc_kr232_run_*` row when a VM is configured. The unit assertions live in
`krc_tests` (`tests/CMakeLists.txt:32-34`), so a green `ctest -R kr232` alone
does not cover the parser ladder.

## Alternatives considered

- **Leave the gap and compute the address in source.** Rejected: the backend
  already pays two instructions and a scratch register for every deep slot,
  hand-written assembly cannot express a defined form, and the workaround at
  `kr232_backend.cpp:480` is missing.
- **Expand the address in the assembler** (`mov r8, r13; sub r8, #d; ld [r8]`).
  Rejected: the assembler may not allocate a scratch register, because R8 can
  be live at the expansion point, and the inserted `sub` changes flags.
- **Always use `F0` for memory operands.** Rejected: 4–8 bytes against 2–3 for
  the common case, and it would change every existing object.
- **Use mode 7 for everything beyond d8.** Rejected: 8 bytes where 6 fit, and
  it contradicts the shortest-form policy the compact/Normal selection already
  follows.
- **Make `encode_normal` promote internally.** Rejected: `encode_normal`'s
  contract is the fixed 3-byte Normal class, and its callers include forms that
  must keep rejecting a wide operand (the immediate and 16-bit operations). The
  parser owns form selection; the encoder stays a pure encoder.
- **Remove modes 6 and 7 from the ISA.** Rejected: the ISA is frozen 1.0 (the
  32-bit baseline frozen at M3's gate); the encoding is already in it, mode 7 is
  used by the `FA` FP form and the Appendix A vectors, and the decoder already
  implements both. Removing it would be a 2.0 change, not this fix.
- **Add a pseudo-op such as `ld.w`.** Rejected: the width is a property of the
  address mode, not a second mnemonic, and the ISA has no such spelling.
