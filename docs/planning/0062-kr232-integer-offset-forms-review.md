# Review — ADR 0062, KR232 integer offset forms

Reviewed 2026-11-26T14:11:18-08:00. Planning review only; ADR 0062 is `proposed`, and
nothing here changes code.

## Findings

| # | Finding | Evidence (file / section / line, quoted) | Label |
|---|---|---|---|
| 1 | The round-trip check the plan leans on is registered only when a sibling `kr232-dis.exe` sits beside the VM binary, so on a host without `vm/` copies the row does not exist and the Linux verification step covers nothing. | `tests/CMakeLists.txt:208-212`: "The disassembler lives beside the emulator in the kr232 build; when it is not there the round trip cannot be read and the row is not added." then `if(EXISTS "${kr232_dis}")`; the block closes at line 344, enclosing the new table. | FIX-BEFORE |
| 2 | The ADR calls the ISA "approved 0.4"; the document is frozen at 1.0, which changes the argument that no §15 decision is needed. | `kr232-isa.md` header: "Status \| **Frozen 1.0** — the 32-bit baseline, frozen at M3's gate on 2026-11-23"; ADR 0062 line 304 says "the ISA is approved 0.4 and frozen at M3". | ERRATA |
| 3 | The decoder citations name `src/decoder.cpp`, which does not exist in this repository; the file is the sibling's. | ADR 0062 lines 59 and 193 cite `src/decoder.cpp:201-235` and `src/decoder.cpp:41-42`; the file is `../kr232/src/decoder.cpp`, and this tree has no `src/decoder.cpp` at all. | ERRATA |
| 4 | "The seven `(generic)` alternates" does not match Appendix A, which lists eight rows marked `(generic)`. | `kr232-isa.md:1336-1343` lists eight: `mov r2, r3`, `ld r2, [r3+4]`, `ld r2, [r3+6]`, `ld r2, [r3+0x1234]`, `st [r3+2], r2`, `mov r2, #0x4000`, `sxth r2`, `ldhu r2, [r3+2]`; ADR 0062 line 66 says "the seven `(generic)` alternates". | ERRATA |
| 5 | The form ladder's first row — compact for `d == 0` — is not the helper's behaviour; it is the pre-existing branch before the helper call, and the helper would choose 3-byte Normal if it ever saw `d == 0`. | ADR 0062 lines 100 and 120-125: the table says `d == 0` → "Compact mode 2", while the helper starts "if (displacement >= -128 && displacement <= 127)"; the compact choice is `kr232_parser.cpp:464-466`, reached before line 467. | ERRATA |

Additional findings exist and are available on request.

## Verdict

**`Can this be done?`** — Yes. The encoder already emits `F0` modes 6 and 7
(`kr232_encoder.cpp:271-341`), the decoder already reads them
(`../kr232/src/decoder.cpp:201-235`), the ISA defines both (`kr232-isa.md` §8.1, §9), the parser
has a single dispatch point per form (`kr232_parser.cpp:467`, `:481`, `:512`), and the store
operand-order trap the ADR warns about is real and correctly stated
(`kr232-isa.md`: "the encoder writes Rs into specifier 1 and the address into specifier 2"). The
work is one helper, one table column, and tests; none of the five findings above changes the
design.

**`Is the verification sufficient?`** — Almost. The unit assertions and the parser byte table
are executable today, but the integration check depends on a conditionally registered ctest row
(finding 1), so the plan must state that precondition or make the row register — otherwise "the
same `ctest` row on `linux-gcc-debug`" can pass by absence.

## Checks run

- Cited section numbers all exist: **yes** — §8.1 at 386-451, §9 at 605-615, Appendix A at 1244;
  parser/encoder line numbers all match; the one exception is the decoder path (finding 3).
- Every stated count matches its own list: **no** — eight `(generic)` rows, not seven
  (finding 4).
- Every acceptance criterion has evidence available at its milestone: **no** — the round-trip
  row's registration is conditional (finding 1).
- Claims of conformance to an external standard checked against it: **n/a** — the ISA is the
  internal standard, and its §8.1/§9 text was checked line by line.

## Remaining work

- Approve, amend, or return ADR 0062 — the owner's decision, not this review's.
- Decide whether the round-trip row's registration is a precondition to document or a gap to
  close; the project already records the "green with fewer rows" trap.
- Consider one executed row (assemble an `F0` mode-6 load, link it, run it under the VM): the
  plan proves the bytes round-trip through the disassembler, not that the VM executes them.
- Implementation per the ADR; removing the backend's `R8` workaround stays a separate change, as
  the ADR says.

## Scope reviewed

- Reviewed: `docs/decisions/0062-kr232-integer-offset-forms.md` (all 308 lines);
  `src/asm/kr232_parser.cpp` 120-200 and 415-544; `src/asm/kr232_encoder.cpp` 140-219 and
  265-341; `src/asm/kr232_encoder.h` 1-35 and 60-127; `src/backend/kr232_backend.cpp` 378-487;
  `../kr232/src/decoder.cpp` 25-45, 143-182 and 195-235; `../kr232/kr232-isa.md` 386-460,
  600-620, 1244 and 1310-1354; `tests/unit/test_kr232_encoder.cpp` 80-112;
  `tests/unit/test_kr232_parser.cpp` 1-45; `tests/unit/test_kr232_roundtrip.cpp` 1-40;
  `tests/CMakeLists.txt` 195-244 and 325-354; `cmake/CheckKr232EncodingRoundTrip.cmake`;
  `docs/decisions/README.md`.
- Not reviewed: `kr232`'s VM execution of modes 6/7 (the plan does not test it); the backend
  workaround removal, which the ADR defers; the KR216 encoder, which has no equivalent change.

## Re-review — amended ADR

Reviewed 2026-11-26T14:18:58-08:00 against `docs/decisions/0062-kr232-integer-offset-forms.md`
(368 lines, amended 14:16). **Agreement: yes.** Findings 1–5 are addressed:

| # | Disposition | Where |
|---|---|---|
| 1 | Addressed | New "Registration precondition", lines 306-322: names `if(EXISTS "${kr232_dis}")` at `tests/CMakeLists.txt:212` closing at `:344`, requires `ctest -N -R krc_kr232_encoding_roundtrip` before claiming the row, and calls a missing row "not run", not a pass. |
| 2 | Addressed | Lines 196-197 and 363-366 now say frozen 1.0; the 0.4 claim is gone. |
| 3 | Addressed | Lines 62-64 and 205 cite `../kr232/src/decoder.cpp`; the decodable path resolves here now. |
| 4 | Addressed | Lines 71-74 state eight marked `(generic)` rows and call the fixture comment's seven a miscount; the fixture has no `F0` row, verified. |
| 5 | Addressed | Lines 100-116 add a Selection column and state that the helper never sees `d == 0` and that the compact choice is deliberate. |

New text checked, and correct:

- The executed-row fixture (lines 279-304) assembles as written. `ld r3, #value` uses the
  assembler's symbol-immediate path (`kr232_assembler.cpp:339-377`: encodes zero, attaches an
  absolute relocation at `offset + 1`); `sub r3, #128` takes the `F9` `0xB` wide-immediate form
  (`kr232_parser.cpp:391-405`, `kr232-isa.md:573`); `.data`/`.word`/`ret` are supported. It
  therefore does fail first at `ld r2, [r3+128]`, as claimed.
- "Register or `F1`" for the 5/6-byte absolute path (line 104) is right: the ISA names the
  5-byte `0x60`/`0xE0` forms "Register" (`kr232-isa.md:198,203`), and `F1` carries the
  byte/halfword absolute forms.

Residual nits, neither blocking:

- Line 119's "there is no fifth case" predates the seven-row ladder; it now reads as a leftover.
- Lines 71-74 say "eight such rows" where the marker exists eight times, but only five of the
  eight are alternates in the "one text, two encodings" sense — `ld r2, [r3+0x1234]`,
  `mov r2, #0x4000` and `sxth r2` have no shorter form. If the sentence is meant to justify
  excluding duplicate encodings, five is the count; if it means marked rows, eight is right.
