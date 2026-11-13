# 0024 — Tile intrinsics and memory-class aggregate returns

- Status: accepted
- Date: 2026-11-13
- Decision: adds an IR intrinsic opcode, lowers the `kr32::matrix` surface to tile
  instructions, and implements the memory-class aggregate return in `R4`
- Supersedes: the "large returns remain unsupported" consequence of ADR 0015

## Context

`../kr32/examples/matrix.cpp` is the last corpus program that compiles and does
not link. It calls `kr32::matrix_mul`, `transform`, and the rest of
`kr32/include/kr32/matrix.hpp`, whose declarations the reference compiler lowers
to tile instructions rather than to library code; no translation unit defines
them, so a call that reaches the linker fails on an undefined symbol.

Two capabilities are missing behind it:

1. The IR has no form for a target operation that has no portable instruction.
   Plan §9.1 names this hook ("Target hooks handle unusual operations (matrix
   tiles, transform stack, atomics, syscalls)") and plan §6.4 names the surface
   (`__builtin_kr32_matrix_mul` and siblings with `kr32/matrix.hpp` wrappers),
   but `docs/ir.md` has no instruction for it.
2. A by-value `mat4` return is 64 bytes. ADR 0015 returns aggregates of up to 8
   bytes in `R0`/`R1` and leaves larger ones unsupported, while `docs/abi.md`
   and the KR32 C++ ABI already state that `mat4` is memory class and uses
   `sret`. `make_translation` in the corpus program returns a `mat4` by value,
   so without that path the matrix is never written and every transform result
   is zero.

## Decision

### IR

- `docs/ir.md` gains `intrinsic`, one instruction whose operation is named by an
  `Intrinsic` enum and whose operands are the operation's arguments. The IR
  names the operation, not the target: `matrix.multiply`, `matrix.multiply_add`,
  `matrix.transpose`, `matrix.transform`, `vector.dot`, `transform.push`,
  `transform.pop`, and `transform.drop`.
- A value-returning intrinsic carries the record or scalar result type it
  produces; the backend materialises it in the instruction's frame slot, which
  is the existing SSA-value model.
- The verifier checks each intrinsic's operand count, that every operand is a
  record pointer, and the result type: a 4x4 tile of `f32`, a 4-lane `f32`
  vector, `f32`, or `void`.
- The opcode is not a v1.1 reservation: it is implemented in v1.0 for the
  targets that report the capability.

### Front end

- IRGen recognises a call whose callee is a declaration named `matrix_mul`,
  `matrix_mul_add`, `transpose`, `transform`, `dot`, `push`, `pop`, or `drop`
  with the signature `kr32/matrix.hpp` declares, and emits the intrinsic
  instead of the call. Matching is by symbol and signature, so a user function
  with one of those names keeps its own body.
- A target without the matrix capability (`has_matrix`) reports
  `matrix intrinsics are not supported for target '<name>'` rather than
  lowering to an instruction the backend cannot encode.

### KR32

- The assembler and encoder gain the ISA's `0x61` M escape (`MMUL`, `MMAC`,
  `MVEC`, `MTRANS`, `VDOT`, `MPUSH`, `MPOP`, `MDROP`), the `0x52`/`0x53` tile
  transfers (`MLOAD`, `MSTORE`, `VLOAD`, `VSTORE`, with the strided form's row
  stride in the extension word), and `WRSP` for setting the transform stack
  pointer.
- Each intrinsic uses T0–T3. The tile file is caller-saved, an intrinsic is an
  expression temporary, and sources are read before the destination is written,
  so no tile allocator is required. `matrix.transform` transposes the vector
  before `MVEC` and the product back before `VSTORE`, matching the column-0
  result convention of `MVEC`.
- The transform stack pointer is zero at reset and `MPUSH` faults below 64, so
  `crt0.s` sets TSP to the linker's `__stack_limit`. Tile saves then occupy the
  64 KiB between the program stack and the heap and cannot overlap either.
- A by-value aggregate return larger than 8 bytes is memory class: the caller
  places the destination address in `R4` (which does not consume an argument
  register) before `CALL`, and the callee copies the object to it at `ret`.
  This implements the convention `docs/abi.md` already documented and closes the
  gap ADR 0015 left open.

## Consequences

- The corpus program `matrix.cpp` compiles, links, and returns 42, so 14 of the
  15 corpus examples run with the status their `main` returns.
- Large aggregate params are still register-only. The ABI passes aggregates
  larger than 8 bytes as a pointer to a caller-owned copy; that path is not
  implemented and no current fixture needs it.
- An FP-only aggregate of 9–16 bytes is returned through `R4` rather than in
  `F0`/`F1`. The KR32 ABI gives `F0`/`F1` that case; the backend has no
  FP-aggregate return path at all, and `sret` is correct for the integer-member
  aggregates that exist today.
- `vec4` and `mat4` are laid out with 4-byte alignment, where the reference
  compiler uses `alignas(16)`. The ISA requires only 4-byte alignment for tile
  transfers, so the generated code is correct; the ABI documents the stronger
  alignment, and a future change must either match it or amend `docs/abi.md`.
- The IR dump gains `intrinsic` and the backend gains the tile opcodes; both
  are deterministic and covered by the existing double-build checks.

## Alternatives considered

- Emit the tile instructions directly from IRGen as inline assembly: rejected
  because the IR must stay the contract between the front end and every
  backend, and a second lowering path cannot be verified.
- Recognize the names by string in the assembler or linker: rejected because
  the call has to be removed before the symbol reaches the linker, and because
  the signature check belongs where types are known.
- Return `mat4` in `R0`–`R3` plus hidden words: rejected because `docs/abi.md`
  already fixes memory-class returns in `R4`, and 64 bytes cannot fit in the
  argument registers.
- Keep the transform stack out of the runtime and leave TSP to the program:
  rejected because a zero TSP faults on the first `MPUSH`, and every program
  that uses tiles would need the same setup.
