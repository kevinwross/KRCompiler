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
