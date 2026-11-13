# 0030 — Source-level names in debug records

- Status: accepted
- Date: 2026-11-13
- Decision: adds a debug display name to IR functions and uses it in the `F` record
- Scope: IR data model and `.debug_info` content; the KDBG format is unchanged

## Context

The debug records name a C++ member function by its symbol, so a debugger
listing a class shows `Animal_legs`, `Bird_ctor_0`, and `bad_cast_what` instead
of `Animal::legs`, `Bird::Bird`, and `std::bad_cast::what`. The symbol has to
stay mangled — the linker and the vtable slots use it — but it is an
implementation name and it hides the class association that a debugger exists to
show.

`.debug_info` carries free-form text records (`S`, `L`, `F`, `G`, `V`, `T`,
`C`, `M`), and the linker turns `F` into a KDBG function whose `name` is that
same string. So the display name can change without touching the object format,
the symbol table, or the KDBG encoding.

## Decision

- `ir::Function` gains an optional `debug_name`. When it is empty the backend
  uses the function's symbol; when it is set the `F` record carries it.
- IRGen sets it for every function it creates from source: a free function uses
  its source name, a member function uses `Class::method`, a constructor uses
  `Class::Class`, and a destructor uses `Class::~Class`. A synthesized copy of
  an inherited method keeps the base's display name, because it is the base's
  code.
- The IR printer and the IR verifier ignore the field: it is metadata that only
  the backend reads, so `--dump-ir` output and IR reproducibility are unchanged.
- The KDBG format, the symbol table, the object format, and the ABI stay as they
  are. This is a content change in one debug record, not a format change.

## Consequences

- `--dump-symbols` on a C++ image lists `Animal::legs`, `Bird::legs`,
  `Penguin::legs`, `Animal::Animal`, `Animal::~Animal`, and
  `std::bad_cast::what`, which is what a debugger should show for a class.
- The display name is not unique — two classes may define the same member and a
  translation unit may hold both — so it is for display only. Anything that
  resolves must keep using the symbol.
- A lambda, a static local initializer, or another synthesized function whose
  display name is not set keeps its symbol name, which is still unique and
  searchable.

## Alternatives considered

- Mangle the class into the symbol (`_ZN1Animal4legsEv`) and let the debugger
  demangle: rejected for now. The whole toolchain would have to agree on the
  grammar, the vtable and typeinfo names are already `_ZTV`/`_ZTI`, and the
  display name is needed now. It stays a possible later ADR.
- A separate `D` record for display names: rejected because `F`'s `name` field
  is already free-form and nothing depends on it equalling the symbol.
