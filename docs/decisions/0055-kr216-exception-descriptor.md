# 0055 — The kr216 16-bit exception descriptor

- Status: accepted (2026-11-22)
- Date: 2026-11-22
- Decision: fixes the `.cpp_except` table, the landing-pad entry state and the frame walk used by C++ exceptions on kr216
- Scope: exception unwinding only; typeinfo layout follows ADR 0053 and the existing single-inheritance chain

## Context

The kr216 backend must lower `invoke`, `landingpad` and `resume`, and the
runtime must find the protected region that covers a call site and enter its
landing pad with the right frame. KR32 has a descriptor (ADR 0026), but its
fields are 32-bit and its frame layout differs: KR32 stores the saved frame
size below R7, while kr216 stores the caller frame at `[FP]` and the return PC
at `[FP + 2]`. A 16-bit descriptor is therefore a separate contract.

The 64 KiB address space forces the table to stay small. Every field is a
two-byte absolute address or count; the linker patches the addresses and
defines `__krc_eh_start`/`__krc_eh_end`.

## Decision

### Descriptor layout (little-endian, two-byte words)

```text
u16 magic          0xE216
u16 function_start
u16 function_end    exclusive
u16 frame_size      the ENTER operand
u16 region_count
region[]            region_count records of three words:
  u16 start_pc
  u16 end_pc        exclusive
  u16 landing_pad
```

Descriptors are emitted in `.cpp_except`, one per function that contains a
protected call. A function with no `invoke` has no descriptor.

### Lookup

The walk holds `(frame, pc)`, starting with the throwing function's `FP` and
the return address of the `__cxa_throw` call. It scans descriptors for one
whose `[function_start, function_end)` contains `pc`. Inside that function it
scans regions for `[start_pc, end_pc)` containing `pc`. If no descriptor or
region matches, the exception is uncaught and the process exits with status
134.

If a region matches, the walk continues at its `landing_pad` with:

- `FP` = the frame of the function that owns the region,
- `SP` = `FP - frame_size`,
- `R2` = the thrown object pointer,
- `R3` = the thrown typeinfo pointer.

The landing pad stores the pair in an eight-byte frame scratch area: the object
at offset 0 and the typeinfo at offset 4, matching the IR's `+0` and `+4`
loads, and returns a pointer to that area.

### Resume

`_Unwind_Resume(object)` continues the walk from the current frame and return
address. The runtime keeps the current exception header in a global; the header
is `{typeinfo, destructor, previous}` at three two-byte words before the thrown
object.

### Type matching

`__krc_eh_matches(thrown_type, catch_type)` walks the thrown typeinfo's base
link at `+6` (the 16-bit typeinfo layout from ADR 0053) and returns 1 on an
exact match, 0 otherwise.

## Consequences

The backend emits descriptors only for functions with protected regions, so a
program that never throws pays nothing for the table. The walk reads `[FP]` and
`[FP + 2]` rather than a frame-size offset, so a descriptor's `frame_size` is
used only to restore `SP` before entering a pad.

The table is absolute-addressed and section-relative relocation applies as for
any other `.word`; the linker's existing `__krc_eh_start`/`__krc_eh_end`
synthesis is unchanged.

## Alternatives considered

- **32-bit descriptor fields.** Rejected: wastes four bytes per address in a
  64 KiB image and does not match the target's pointer width.
- **A frame-size field alone, with no region table.** Rejected: cleanup and
  catch regions cannot be distinguished, so a call outside a protected region
  would enter the wrong pad.
- **Two-phase unwinding with a personality routine.** Rejected for v1: the
  corpus needs catch and destructor cleanup, and a single walk plus pad entry
  keeps the runtime small enough for the 16 KiB data budget.
