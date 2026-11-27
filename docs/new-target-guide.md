# Adding a target

This is the checklist for bringing up architecture number five. It records what
kr16, kr216 and kr232 each needed, in the order they needed it, and what the
compiler refuses to do until each piece exists. The plan's §6 budget is under a day
for a VM whose encoding is already specified; the steps below are what that day is
spent on.

Read `docs/targets/schema.md` first: a target is a descriptor plus a backend, and the
descriptor's fields are the ABI contract the backend and the runtime both obey.

## 1. The target id and profile

`include/krc/target/target.h` declares `TargetId`; `src/target/target.cpp` holds the
profile table and the extension table.

- Add the enumerator to `TargetId` (append it; the values are read by the container
  format and by `kr232`'s picker, so a renumber is a format change).
- Add a `constexpr TargetProfile` beside the others with every field filled in:
  pointer/int widths, endianness, data model, `has_*` capabilities, `aggregate_rules`
  (`MemoryOnly` unless the target passes small aggregates in registers) and
  `backend_available`.
- Register it in the table that `target_for_extension` and `extension_for` read, and
  in `target_name`/`target_from_name`. One table: a second list of names is how two
  spellings start disagreeing.

`backend_available = false` is legal and is what kr64 ships today: the target is
known, `--list-targets` prints it as `backend=no`, and the compiler refuses to compile
for it with a message naming the target rather than an internal error. Setting it
true is the last step of this document, not the first.

## 2. The descriptor

`src/backend/<target>_descriptor.{h,cpp}` declares the register file: which registers
are arguments, which are constant, which carry the frame pointer, stack pointer,
return address, flags and exception frame, the call alignment, and the scratch sets.
KR32 and KR232 have a descriptor file; KR16 and KR216 encode the same facts in their
backends. Copy the shape from KR32 or KR232 and change the numbers.

A descriptor is not a style choice. `docs/targets/schema.md` freezes the fields, and
ADR 0002 freezes the schema itself, so a field that does not fit needs an ADR before
code that depends on it.

## 3. The assembler, encoder and object writer

`src/asm/<target>_*` is not one shape across the four targets. kr232 has a parser,
an assembler, an encoder and an object; kr32 and kr16 have an assembler that parses
internally, an encoder and an object; kr216 has an encoder and an object and no
separate assembler. The pieces are:

- `_parser` (optional): text to statements.
- `_encoder`: statements to bytes. Every refusal names the operands it rejected.
- `_assembler`: sections, labels, fixups, and one `krc::listing::ListingRow` per
  statement so `-l` works (see [include/krc/listing.h](../include/krc/listing.h); the
  row carries section, address, bytes, source text and line).
- `_object`: the backend's object, written through the KRO writer.

The assembler is also what `-S` output is fed back into, so the text it accepts and
the text the backend emits must be the same subset. A target whose backend emits an
instruction its assembler rejects fails at the second half of a normal build.

## 4. The backend

`src/backend/<target>_backend.{h,cpp}` lowers IR to that assembler's text. The
dispatch is in `src/toolchain/toolchain.cpp` and the compile path in
`src/tools/krc/main.cpp`; both switch on `TargetId`, so a new case in each is part of
the step, not an afterthought.

`has_eh`, `has_rtti`, `has_cpp`, `has_tls`, `has_atomics` in the profile decide which
front-end features the target accepts. A target without exception support must refuse
`throw` with a diagnostic naming the target, not emit code that traps at run time.

## 5. The runtime, sysroot and image layout

`targets/<target>/runtime/` holds the startup file and the C runtime objects the
linker places: `crt0.s`, syscall glue, and the C library. Headers live in
`sysroot/<target>/include`, with `bits/` generated from the profile. Every link
recipe names the runtime objects it wants: `krc image` links the objects it is given
and nothing else, so the startup, file I/O, EH and `libkrcrt` objects are passed on
the command line.

The sysroot is installed under `share/krc/sysroot/<target>` and exported by the
package (`KRC_SYSROOT`). A consumer links with `$<TARGET_FILE:krc> image`, never by
naming an object path, so the sysroot layout is the compiler's business.

## 6. Tests

Four kinds, and each exists for a reason the others do not cover:

- **encoder vectors** — every opcode's bytes, from the ISA's appendix.
- **assembly round-trip** — `-S` output assembles back to the same bytes.
- **listing drift** — a committed `.lst` compared byte for byte, as
  `tests/fixtures/listing/<target>-crt0.lst` does for all four targets.
- **executed rows** — an image built and run, when a VM exists for the target. A
  target with no VM gets the first three and says so in its ADR.

## 7. The ADR

`docs/decisions/NNNN-<target>-bringup.md` records: the profile's choices where the
ISA left room, what the backend does not implement yet, and which of the four test
kinds exist. Every earlier target has one; a target without one is a target nobody
can later tell was finished.

## 8. The guest tree

A new VM repository is created from `skeleton/`: `CMakeLists.txt` with
`include(cmake/KrGuest.cmake)` and a `::core` export, two presets, a gate script, a
fixture generator and a drift row. `cmake/KrGuest.cmake` is copied unchanged from an
existing VM tree — four identical copies is deliberate, because a tree that cannot
find its compiler must fail at configure rather than silently use another.

## 9. krdbg

One adapter linking `<vm>::core`, plus a golden hash regenerated by script. The
adapter maps the VM's registers and memory onto krdbg's port interface; it is small,
and it is what makes the new VM debuggable on the day it boots.

## 10. The family gate

One line in `kr232/scripts/check-family.ps1`: the repository's name, its gate script
or preset, and its expected row count. A repository absent from that list is absent
from every gate the family runs, which is how a tree rots without anyone noticing.

## Order and evidence

Do them in this order and run the check each step names:

| Step | Done when |
|---|---|
| 1–2 profile and descriptor | `krc --list-targets` prints the target, `backend=no` |
| 3 assembler and object | a hand-written `.s` assembles and `-l` renders it |
| 4 backend | `-S` output assembles back to the same bytes |
| 5 runtime and sysroot | `krc image --target=<t>` links `crt0.s` and a C fixture |
| 6 tests | encoder vectors, round-trip and listing drift rows pass |
| 8 guest tree | its gate runs and its drift row fails when a fixture is edited |
| 9 krdbg | the adapter's golden hash matches |
| 10 family gate | one command builds and gates every tree, including the new one |
| — | `backend_available = true`, and the ADR says what is still missing |
