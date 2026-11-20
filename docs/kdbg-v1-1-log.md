# KDBG v1.1 log

Append-only. Every entry begins with the exact `iso` string from `local-datetime`,
called immediately before that entry was written. Entries are never rewritten; a
correction is a new entry. The plan is
`krdbg/docs/requests/kdbg-v1-1.md`; stage A's record is
[docs/adopt-krc-log.md](adopt-krc-log.md).

## 2026-11-20T11:14:45-08:00

**Changed:** created the branch; nothing else.

**Branch:** `kdbg-v1-1`, cut from `adopt-krc` at `0951c6e` (`Close out stage A
and correct the log`). `adopt-krc` remains at `0951c6e` and is not moved by this
work; `master` is untouched.

**Read:** `krdbg/docs/requests/kdbg-v1-1.md` in full, including section 8. The
settled decisions: the bridge stays and is extended additively (new record
letters only); the 64-bit encoding lands now, as format and tests, without waiting
for a kr64 producer; krdbg consumes first; the version bump is minor only
(major `1`, minor `1`); the branch is cut from `0951c6e`.

**Next step:** K1's first deliverable is design, not code — settle the exact
bridge record spellings and the KDBG tag numbers in `docs/symbols.md`, then emit
the location/scope/flags/array/type-ref records the backend already has the
information for.

**Blocker:** none.

## 2026-11-20T11:17:21-08:00

**Changed:** K1's first deliverable — the design point — is settled in
[docs/symbols.md](symbols.md). The bridge spellings and KDBG tag numbers are now
fixed before any encoder work, as the plan requires. No code was written: K1's
second deliverable is blocked by a premise in the plan, recorded below.

**Design landed in `docs/symbols.md`:**

- Header: major `1`, minor `1`; a reader ignores the minor and reports "newer
  KDBG" only when a caller requires a record it does not implement.
- Bridge additions, all new letters (KR32's parser skips what it does not know):
  `P function name reg start_label end_label` (reusing KR32's existing P spelling
  and its `R`/`F` register form), `K function name parent start end` for a lexical
  scope, `W function name kind scope flags` for `param|local|static` and a
  `0x`-hex `u32` flag word (bit 0 address-taken, bit 1 read-only), `A type element
  count` for arrays, and `R type target` for type references. `C name size base`
  keeps its existing shape and now writes the base field.
- KDBG tags: `Location` 9 in the new `.kdbg_loc` (section type 8), `Scope` 10 in
  `.kdbg_scope` (type 9), `VarInfo` 11 in `.kdbg_var`, `Base` 12, `ArrayType` 13
  and `TypeRef` 14 in `.kdbg_type`, and the 64-bit variants `Line64` 15,
  `Function64` 16, `Symbol64` 17 and `Location64` 18. Every payload's field order
  and width is written in the document; v1 tags 1–8 and sections 1–7 are untouched.
- `target_id` is the KRO id (`1` KR32, `2` KR16, `3` KR64).
- Compatibility rules: a v1 reader skips unknown tags and sections; a v1.1 reader
  accepts minor 0 and leaves the new fields empty; no v1 record is resized.

**K1's second deliverable is blocked — the plan assumes a register-held source
variable that the compiler cannot currently produce.** The plan's done-when asks
for "a location for a variable the backend knows is register-held". With
promotion and slot reuse off, there is no such variable:

- `Kr32Backend::layout_function` always reserves a slot for every parameter
  (`src/backend/kr32_backend.cpp:319-324`) and for every `Alloca`
  (`:328-333`); the register path only omits a slot for a **value the allocator
  keeps in a register**, and parameters and allocas never reach that set.
- `compute_allocation` keeps only instruction results:
  `keep_local_allocations` iterates instructions and explicitly skips
  `Alloca`/`Phi` (`src/backend/register_allocator.cpp:228-256`), and parameters
  are not instructions at all (`allocate_registers` interns them, then the filter
  drops them). So `allocated_registers_` never contains a source variable.
- `docs/plan-o1-debugger.md:42-45` states the same rule: a slot is dropped only
  for "a value the allocator keeps in a register — which a source variable never
  is". ADR 0052 repeats it.
- Reproduced at this branch's base (`0951c6e`) on a full `-O1` compile of
  `int add(int a, int b) { int sum = a + b; return sum; }`:
  `krc --dump-symbols` prints `variable add.a offset=8`, `add.b offset=12`,
  `add.sum offset=32` — three `V` records with frame offsets and **no `P`**, which
  is the correct description of where those variables live.

The only register location the backend knows without a codegen change is a
parameter's incoming ABI register between function entry and the prologue store
that writes its slot. A `P` record for that range would satisfy the letter of the
done-when and be useless — it expires before the first source statement, while
the existing `V` record is correct for the whole function. Emitting it would also
give readers two locations for one variable with no rule to choose between them.
I did not do that, and I did not enable register-holding for a source variable:
that is a codegen change outside "do not enable promotion or slot reuse".

**Decision needed before K2.** One of:

1. Amend K1's done-when to a fixture-driven `P` record — the bridge parser,
   KDBG `Location` encoding and `--dump-symbols` text/JSON are then tested through
   a synthetic `.debug_info`/`KdbgImage`, and no backend emits `P` until
   promotion lands under ADR 0052. This is the smallest change and keeps every
   format deliverable.
2. Authorise keeping one source variable in a register (the allocator already
   allocates parameters; the emitter would need to keep the allocation and
   respect call clobbering). That is a codegen change and needs its own evidence.
3. Accept the entry-only parameter location (true but nearly useless, and it
   conflicts with the existing `V` record).

I stopped here rather than build K2–K9 on the assumption that a `P` producer can
be demonstrated, because the producer question changes what K1's test must prove.
The format design is committed and reviewable now.

**Commands and real results:**

- `krc -O1 -c` then `krld` then `krc --dump-symbols` on the fixture above:
  `KDBG target=1 abi=1 sources=1 lines=9 functions=2 symbols=0 variables=3
  types=1`; `function add 0x10000-0x10060 frame=40 type=i32`;
  `variable add.a offset=8`, `add.b offset=12`, `add.sum offset=32`.
- No build was run: this entry changes documentation only.

**Commit:** the commit that carries this entry and the design.

**Next step:** the owner's decision on K1's producer; then K1's parser/emission
half and K2–K9.

**Blocker:** K1's done-when has no producer under the plan's own "promotion and
slot reuse stay off" rule.

## 2026-11-20T13:08:33-08:00

**Changed (K1–K9):** KDBG v1.1 is implemented, additively, on `kdbg-v1-1`. No v1
record changed number or payload, and the allocator, promotion and slot reuse were
not touched.

- **K1.** The bridge parser in `attach_kdbg` reads `P` (register location with a
  live range, KR32's spelling), `K` (lexical scope), `W` (variable kind, scope and
  flags), `A` (array element type and count) and `R` (type reference); `C`'s
  existing base field is now written. Both backends emit the records they already
  know: a function `K` scope, `W` for every parameter and source local, `A`/`R`
  for array and pointer types, and `C` with the base name. No path emits `P` yet:
  the producer stays deferred under decision 6.
- **K2.** KDBG gains `Location` tag 9 in the new `.kdbg_loc` (section type 8),
  `Scope` 10 in `.kdbg_scope` (type 9) and `VarInfo` 11 in `.kdbg_var`; the
  minor is `1`. The hand-built fixture (a `.debug_info` text with `S/L/F/V/P/K/W/
  T/C/A/R`, run through `attach_kdbg`) decodes and both dumpers print it.
- **K3.** `ir::Type` carries `base` and `base_type` as debug metadata only;
  `complete_struct` accepts them and `map_record` passes the base's IR name and
  type. Layout and code generation are unchanged. A compiled
  `struct Derived : Base {}` prints `type Derived size=4 kind=struct base=Base`
  and `type Base size=4 kind=struct`, and the JSON has `"base":"Base"`.
- **K4.** `ArrayType` tag 13 and `TypeRef` tag 14, with no type-string parsing:
  the decoder returns the element type and count, and a pointer's target. A
  compiled `int a[3]; int *p = a;` prints
  `type [3_x_i32] size=12 kind=array element=i32 count=3` and
  `type ptr<i32> size=4 kind=pointer target=i32`.
- **K5.** 64-bit variants `Line64` 15, `Function64` 16, `Symbol64` 17 and
  `Location64` 18, written instead of the 32-bit forms when the header's pointer
  width is `8`. A fixture with addresses above 2^32 (`0x1_0000_0004`,
  `0x3_0000_0000`) round-trips and `require_kdbg_feature(WideAddresses)` passes.
- **K6.** `target_id` comes from `kROTargetId`: a KR32 image says `1`, a KR16
  image `2`, a future KR64 image `3`; the dumper prints it.
- **K7.** Text and JSON print every new field (`minor`, `pointer`, counts,
  `varinfo`, `location`, `scope`, type `base`/`element`/`count`/`target`); the
  JSON schema is documented in `docs/symbols.md`. `require_kdbg_feature` reports
  `newer KDBG` for a payload whose minor is newer than the reader, `predates` for
  a v1 payload missing a v1.1 fact, and a plain missing-records error otherwise.
- **K8.** Tests: the v1.1 round-trip, the hand-built bridge fixture, the v1-reader
  fixture on a v1.1 payload, the 64-bit fixture, truncated and section-CRC
  failures, the newer-minor report, and compiled base/array/pointer/target-id
  records. The unit binary now has **172 cases / 1,654 assertions**, all passing.
- **K9.** `docs/symbols.md` (v1.1 records, tags, ordering, JSON schema),
  ADR 0009's v1.1 amendment, ADR 0052's update (the format half is the additive
  `Location`, the `Variable`-grows recommendation is withdrawn), and
  `docs/toolchain-embedding.md`'s reader notes.

**Compatibility rows, real output:**

- **A v1 reader on a v1.1 payload.** The test-side `decode_as_v1` (the previous
  decoder's tag dispatch, tags 1–8 only) decodes the v1.1 fixture: sources 1,
  lines 1, functions 1, variables 1, types 1, no error. The new tags and the
  `.kdbg_loc`/`.kdbg_scope` sections are skipped because their tags are unknown.
- **KR32's debug path on a v1.1-built `.k32`.** `..\KR32\build\windows-ninja\
  kr32.exe debug arr.k32` (the `arr.k32` this tree built) exits **0** and prints
  the parsed source paths and functions, starting with
  `source 0 build/kdbg11-check/arr.c` and
  `function use 0x10000-0x10084 frame=72 result=i32`. The new `K/W/A/R` letters
  and the written `C` base are skipped or accepted, not errors.
- **Both dumper forms.** `krc --dump-symbols arr.k32` prints
  `KDBG target=1 abi=1 ... minor=1 pointer=4 locations=0 scopes=212 varinfo=696`,
  `type [3_x_i32] ... element=i32 count=3`,
  `type ptr<i32> ... target=i32`, `varinfo use.n kind=param ... scope=use` and
  `scope use:use parent=- 0x10000-0x10090`. The JSON form contains `"minor":1`,
  `"pointer":4`, `"variableInfo":`, `"locations":[]`, `"scopes":`, `"element"`,
  `"target"`; `inherit.k32` adds `"base":"Base"`.

**Boundary matrix, recorded in `implementation.md` for `ddae511`:**
`windows-msvc-debug` 913/913, `windows-msvc-release` 913/913,
`linux-gcc-debug` 719/719, `linux-gcc-release` 719/719,
`linux-clang-debug` 719/719, `linux-sanitize` 719/719.

**Commit:** `ddae511` (`Add KDBG v1.1 records additively`). The gate record and
this entry follow in a documentation commit.

**Next step:** push `kdbg-v1-1`; krdbg consumes it first (decision 3).

**Blocker:** none. The `P` producer remains the deferred item recorded in
decision 6; it arrives with the ADR 0052 promotion change.
