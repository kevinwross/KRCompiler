# KDBG symbol and debug format

**Status:** KDBG v1 frozen for the M4 subset; **v1.1 adds records additively**
(major `1`, minor `1`) and does not change a v1 record's shape. Images carry one
`.kdbg` debug section built from the textual `.debug_info` bridge. Every VM
backend emits the bridge: KR32 for its GUI/DAP debugger, KR16 so a `.kr16` can
carry `.kdbg` at all, KR216 from its backend, and KR232 from its assembler. The
bridge remains an intermediate format; KDBG is the
contract. See [ADR 0009](decisions/0009-kdbg-v1-freeze.md) and
[ADR 0008](decisions/0008-debug-info-bridge.md).

**Magic:** `KDBG`. **One format for every target and every debugger.**

## Image encoding

`.kdbg` is a single debug payload carried as a section of a `.kr32`, `.kr16`,
`.kr216`, or `.kr232` image. Its 64-byte header is:

| Offset | Field |
|---|---|
| 0 | `KDBG` magic |
| 4 | major version (`1`) |
| 6 | minor version (`1` in v1.1; `0` is v1) |
| 8 | header size (`64`) |
| 12 | flags |
| 16 | target ID |
| 20 | ABI version |
| 24 | pointer width in bytes (`2` for a 16-bit target, `4` otherwise) |
| 25 | endianness (`1` little) |
| 28 | build ID (8 bytes) |
| 36 | section count |
| 40 | section-table offset |
| 44 | string-table offset |
| 48 | string-table size |
| 52 | payload CRC32: this field is zeroed, and the CRC covers the whole payload (header, section bytes, tables, and strings) |
| 56 | image base (u64, little-endian; 0 = not recorded) |

Addresses in the payload are absolute in the image's load space. `attach_kdbg`
runs after layout, and a `.kr16` is mapped at the addresses in its section table
rather than relocated at load. The image base the payload was built for is the
little-endian `u64` at offset 56; a reader compares it with the container's own
base — the `.kr32` header's base field, or the address of the `.kr16`'s first
section — and rejects a payload that records a different value. A payload that
records zero predates the field and is accepted unchanged, so the write of the
field does not invalidate an older image. No version bump and no record type is
involved.

The section table has 32-byte entries: name string offset, section type, flags,
payload offset, payload size, payload CRC32, and reserved words. A section is a
record stream of `tag u16`, `flags u16`, `length u32`, payload. Readers skip
unknown tags and reject out-of-bounds or checksum failures.

## Sections and records

| Section | Records |
|---|---|
| `.kdbg_src` | `Source`: id, path offset |
| `.kdbg_line` | `Line`: address, source id, line, column |
| `.kdbg_func` | `Function`: name, start, end, frame size, result type |
| `.kdbg_sym` | `Symbol`: name, address, size, type |
| `.kdbg_var` | `Variable`: function, name, frame offset, type, size |
| `.kdbg_type` | `Type` and `Member`: name, size, kind, member layout |
| `.kdbg_build` | `Build`: compiler, build ID, source count |

Strings are NUL-terminated offsets into the payload string table. The
Function record's name comes from the .debug_info F record's display
name, which is Class::method for a member function and the symbol for
everything else ([ADR 0030](decisions/0030-debug-display-names.md)); a display
name is not unique, so anything that resolves keeps using the symbol. Records are
emitted deterministically: sources, lines, functions, symbols, variables, types,
then build metadata.

Every function's entry address carries a line record for the source line of its
definition. The bridge labels it `.L<symbol>.begin`; the label and the function
symbol are at the same address, so the line record and the Function record's
`start` agree. A breakpoint on the definition line therefore binds before the
prologue ([ADR 0061](decisions/0061-function-definition-location.md)).

Two translation units can define the same internal name, and the linker resolves
every bridge label through one name-keyed table where the first definition wins,
so a label a record resolves must be unique per object. KR32 and KR16 name the
function symbol, which the language keeps unique for external linkage. The kr216
direct-bridge producer emits its own per-object labels for every label a record
resolves — the begin and end labels, the per-instruction line labels, the scope
range and an internal-linkage global's data label — while the F and G name fields
stay the source display name. It keeps `.L<symbol>.begin` as a readable alias at
the entry address, but no bridge record resolves it.

### KR232 assembler debug directives

KR232's bridge is built by its assembler from directives the backend writes into
the assembly text, not from IR debug locations:

| Directive | Bridge record | Meaning |
|---|---|---|
| `.debug_function <display> <symbol> <frame> <result-type>` | `F` | Every lowered function, internal linkage included. `display` is the source name (ADR 0030), `symbol` the linked symbol the V/K/W records name. |
| `.debug_line <path> <line>` / `.debug_line <line>` / `.debug_line 0` | `L` | The source file and line the following instructions belong to; `0` means "no source line", so no record is written until the next directive. With no directive, a hand-written `.s` falls back to its own line numbers. |
| `.debug_var <function> <name> <offset> <type> <size>` | `V` | A parameter or named local's frame home. `function` is the symbol. |
| `.debug_varinfo <function> <name> <kind> <scope> <flags>` | `W` | The kind (`param`, `local`, `static`), function scope and flags. |
| `.debug_scope <function> <name> <parent> <start-label> <end-label>` | `K` | A lexical scope; the function scope is the function's range. |
| `.debug_global <name> <type> <size>` | `G` | A defined global, whatever its linkage. `name` is the source name. |
| `.debug_type ...` | `T`/`C`/`M`/`A`/`R` | The type records the backend adds for every referenced type. |

The assembler resolves every record's range through labels and emits its own
per-object symbols for them (a hash of the assembly text), because the linker
resolves bridge labels through one name-keyed table: two translation units that
both define an internal `helper` must not resolve to one another's range. For the
same reason an internal-linkage `G` keeps its source name in the record but uses
a per-object label. A hand-written `.s` that names no `.debug_function` keeps the
older behaviour (an `F` for each exported text label), and exported data without
`.debug_global` keeps a `G` with an unknown type.

KR232 lowers every `ret` as an inline epilogue (`leave #0; ret`), so there is no
single pre-epilogue return label. Its `F` end label sits after the last inline
epilogue and the function range therefore includes it; a pc in an epilogue is
inside the function and reports the return statement's line. This is narrower
than KR32's `.L<symbol>.ret` rule only in name: a target that wants the reference
range would have to lower returns through one shared epilogue block first.

### KR216 direct-bridge producer

KR216 is the direct-bridge style: the backend appends the records while it emits
text, so it sees each instruction's `DebugLocation`, and the linker resolves the
labels after layout. Its rules:

- `S`/`L` are emitted from the IR location's file and line, one `S` per distinct
  file. A header-defined function's records name the header, not the including
  `.cpp`.
- The entry label carries the definition's line, before the prologue. A
  synthesized function (a global-initializer runner, a generated `main` wrapper)
  has no definition, emits no entry line record, and does not inherit the
  previous function's line.
- `F` is emitted for every lowered function, internal linkage included:
  `<display> <begin-label> <end-label> <frame> <return-type>`. The begin/end
  labels are per object; the range ends at the pre-epilogue label, so a pc in the
  epilogue names no function (the KR32/KR16 convention). `K` names the symbol and
  shares that range.
- `V`/`W` name the function by its **display name**, which is the shape a reader
  without a `K` bridge resolves. For a member function the F display name is
  `Widget::get` while the symbol is `Widget_get`; a consumer that derives a
  function's identity from `K` keeps the display name here because the variable
  records do not name the symbol.
- A parameter and every `<variable>.addr` local get a `V` and a `W`; a shadowed
  declaration's alloca is `<variable>.addr.<n>` and is recorded as
  `<variable>.<n>`, so it is a second row rather than a dropped one. Compiler
  temporaries (`argument.copy`, `call.temp`, …) never get a record.
- `G` is emitted for every defined global, external and internal. The record's
  name is the source name; an internal-linkage object's label is per object so two
  units' `counter` symbols do not resolve to one address. A weak definition that
  another object also has is one object after the link and one `G` record
  describes it.
- `T`/`C`/`M`/`A`/`R` describe every referenced type, depth first.

## v1.1 additions

v1.1 is **additive**. A v1 record keeps its tag and its exact payload; every new
fact is a new tag, a new section, or a new `.debug_info` letter. A v1 reader
accepts a v1.1 payload and reports the v1 records; it skips the new tags and the
new sections. A v1.1 reader accepts a v1 payload and reports what is present.
The major stays `1`; the minor becomes `1`. The header's minor is advisory: a
reader does not reject a payload for a minor it does not know, but it reports
"newer KDBG" if a caller requires a record that the reader does not implement.

### Bridge records (`.debug_info`, NUL-separated)

The existing `S`, `L`, `F`, `V`, `G`, `T`, `C`/`M` records are unchanged. The new
letters are ones KR32's parser does not know, so it skips them; `P` is already
known to KR32 and is reused with its existing spelling.

| Record | Fields | Meaning |
|---|---|---|
| `P` | `<function> <name> <reg> <start_label> <end_label>` | A register-held variable over a half-open live range. `reg` is `R0`–`R15` or `F0`–`F15`; an `F` prefix sets the floating-point/frame flag, exactly as KR32's parser reads it. `start_label`/`end_label` resolve through the linked symbol table. |
| `K` | `<function> <name> <parent> <start_label> <end_label>` | A lexical scope. `parent` is another scope name, or `-` for the function scope. Half-open range. |
| `W` | `<function> <name> <kind> <scope> <flags>` | Variable flags and scope membership. `kind` is `param`, `local`, or `static`; `scope` is a `K` name or `-`; `flags` is `0x` followed by a hex `u32` (bit 0 address-taken, bit 1 read-only, all others reserved 0). |
| `A` | `<type_name> <element_type> <count>` | An array type: the element type's debug name and the element count. |
| `R` | `<type_name> <target_type>` | A type reference: the target of a pointer (or the element type of an array). |
| `C` | `<name> <size> <base>` | The existing C record, now with its base field written: the base class's debug name, or `-` for none. The field already existed; it was empty. |

A `P` record's range is in addresses after linking; before linking the labels are
the only form. The bridge spells no address itself.

### KDBG sections and tags

The v1 sections and tags are unchanged. New tags are `u16` and unique across the
format; new sections have new section types.

| Section | Type | Tag | Record | Payload (little-endian) |
|---|---|---|---|---|
| `.kdbg_loc` | 8 | 9 | `Location` | function `u32`, name `u32`, register `u16`, fp `u8`, reserved `u8`, start `u32`, end `u32` |
| `.kdbg_scope` | 9 | 10 | `Scope` | function `u32`, name `u32`, parent `u32`, start `u32`, end `u32` |
| `.kdbg_var` | 5 | 11 | `VarInfo` | function `u32`, name `u32`, kind `u16`, flags `u16`, scope `u32` |
| `.kdbg_type` | 6 | 12 | `Base` | type `u32`, base `u32` |
| `.kdbg_type` | 6 | 13 | `ArrayType` | type `u32`, element `u32`, count `u32` |
| `.kdbg_type` | 6 | 14 | `TypeRef` | type `u32`, target `u32` |
| `.kdbg_line` | 2 | 15 | `Line64` | address `u64`, source `u32`, line `u32`, column `u32` |
| `.kdbg_func` | 3 | 16 | `Function64` | name `u32`, start `u64`, end `u64`, frame `u32`, type `u32` |
| `.kdbg_sym` | 4 | 17 | `Symbol64` | name `u32`, address `u64`, size `u64`, type `u32` |
| `.kdbg_loc` | 8 | 18 | `Location64` | function `u32`, name `u32`, register `u16`, fp `u8`, reserved `u8`, start `u64`, end `u64` |

`kind` in `VarInfo` is `1` parameter, `2` local, `3` static. A `Location`'s fp
byte is `1` when the register is an FP register, matching the bridge's `F`
prefix. `start`/`end` are half-open and absolute in the image's load space, like
every other address.

The 64-bit tags are written instead of their 32-bit counterparts when the
target's pointer width is 8; a 32-bit target writes only the v1 tags. The header's
pointer width is authoritative. Addresses that fit 32 bits are still written with
the tag the target selects, so a reader never has to try both.

`target_id` is the KRO target id: `1` KR32, `2` KR16, `3` KR64, `4` KR216,
`5` KR232. KRO and KDBG carry the same number; KR216's row was missing here
until KR232's was added (ADR 0057).

### Compatibility rules

- A v1 reader: major must be `1`; minor is ignored; unknown tags and unknown
  sections are skipped; every v1 record decodes as before.
- A v1.1 reader: a v1 payload (minor `0`) decodes with the new fields empty; a
  v1.1 payload decodes with them populated. A required-but-unknown tag is
  reported as "newer KDBG", never guessed.
- A bridge consumer: KR32's parser is a chain of known letters with no trailing
  error, so `K`, `W`, `A`, `R` are skipped; `P` is already parsed with the same
  spelling. No existing record's field order changes.

The encoder writes the v1 sections first (sources, lines, functions, symbols,
variables, types, build), then `.kdbg_loc` and `.kdbg_scope`, so the order stays
deterministic for the same input. A `.kdbg_type` section carries a type's `Type`
record, its `Member` records, then any `Base`/`ArrayType`/`TypeRef` records for
that type; a reader applies the last three to the named type.

### JSON dump schema

`krc --dump-symbols --json` prints one object. v1 keys keep their names and
types; v1.1 adds keys and never renames or retypes a v1 key.

| Key | Type | Present |
|---|---|---|
| `target` | number | always |
| `abi` | number | always |
| `minor` | number | always (v1.1 addition) |
| `pointer` | number | always (v1.1 addition) |
| `compiler` | string | always |
| `sources` | array of `{id, path}` | always |
| `functions` | array of `{name, start, end, frame, type}` | always |
| `variables` | array of `{function, name, offset, size, type}` | always |
| `variableInfo` | array of `{function, name, kind, flags, scope}` | v1.1 |
| `locations` | array of `{function, name, reg, fp, start, end}` | v1.1 |
| `scopes` | array of `{function, name, parent, start, end}` | v1.1 |
| `types` | array of `{name, size, kind, members}` plus optional `base`, `element`+`count`, `target` | always, optional keys v1.1 |

`reg` is `"R9"` or `"F0"`; `fp` is a JSON boolean. Addresses are numbers, not
strings, and may exceed 2^32 only in a v1.1 wide payload.



A `Line` record is written for each instruction that carries a source location, so a
line has a record exactly while the image still has an instruction at it. A line
whose instructions a pass removed has no record: the format does not map it onto a
neighbouring line, because a breakpoint that cannot be placed is better than one
that stops in the wrong place. A debugger showing a stopped frame therefore reports
the next line that still has code.

`-O1` shrinks the set of placeable lines by the code it removes. The case worth
naming is E5 (`empty_block_elimination`, `src/ir/passes.cpp`): a statement whose
whole body is one jump — `break;`, `continue;` — lowers to a block holding a single
`br`, and eliminating that block deletes the only instruction that carried the line.

A `Variable` record exists for each **source variable**, which the IR names
`<variable>.addr`; a compiler temporary — `call.temp`, `operator.temp`,
`subscript.this` — is not recorded, so a pass may promote one without leaving a
record pointing at a dead slot (`docs/optimize.md` E4). A shadowed declaration's
alloca is `<variable>.addr.<n>`: it is recorded too, with the `.addr` marker
removed and the numeric suffix kept (`x.12`), so the two declarations stay two
rows. A promoted source variable would invalidate its record, which is why the
pass refuses `<variable>.addr` and `<variable>.addr.<n>`.

The driver passes `promote_recorded = false` for kr232 and kr216 at every level:
neither backend emits a register location (`.kdbg_loc`/`P`), so `-O2` does not
promote a recorded alloca and every named local keeps its frame home
([ADR 0052](decisions/0052-kdbg-variable-scope-ranges.md)). `-O2` is still not a
supported source-level debug level for those targets until a location producer
exists, because inlining and reordering can remove the function a stop is in and
change which line a pc belongs to; debug them at `-O0` or `-O1`.

Source paths are copied from the `.debug_info` bridge. `krld
--source-root=DIR` rewrites paths under `DIR` to `/`-separated paths relative to
`DIR` before writing KDBG; paths outside `DIR` are left unchanged. Passing the
project root keeps repository sources portable. Without the option the recorded
paths are unchanged.

## Tooling

- `krc --dump-symbols image.kr32` prints canonical text for a `.kr32`, `.kr16`,
  `.kr216`, or `.kr232` image.
- `krc --dump-symbols --json image.kr16` prints the stable JSON form.
- `krld` builds `.kdbg` from `.debug_info` after linking.
- `include/krc/debug/kdbg.h` is the shared reader/writer; `krc::debug` packages it.

## Freeze checklist

- [x] Exact record encoding and tag registry.
- [x] Type-table model and member layout.
- [x] Line/frame records at `-O0`.
- [x] Header and section CRC, bounds checks.
- [x] Text and JSON dump schema.
- [x] Shared reader/writer API.
- [ ] Location-expression opcodes (post-v1).
- [ ] Sidecar naming, strip behavior, and build-ID matching (post-v1).
- [ ] Full C++ scope/template records (post-v1).
