# IR specification

**Status:** Frozen for the v1.0 core by [ADR 0003](decisions/0003-ir-core-freeze.md) (2026-11-09). EH, RTTI, and v1.1 metadata remain draft.
**Form:** typed SSA in memory, with a human-facing textual dump. No stable serialised IR in v1.0.

## Purpose

The IR is the contract between the C/C++ front end and every target backend. It carries enough type, memory, and debug information to generate correct code without committing to a target.

## Model

```text
module
  target id
  type table (named structs)
  globals
  functions
  constants

function
  name, linkage, calling convention
  parameters
  basic blocks (entry first) or declaration-only
  debug metadata

basic block
  phi nodes (first)
  instructions
  terminator (last)

instruction
  result type and SSA value
  operands
  memory flags (alignment, volatile)
  debug location
```

Every SSA value is defined exactly once: a parameter, a global, a constant, or an instruction result. A function's entry block has no predecessors.

## Type grammar

```text
void
i1 | i8 | i16 | i32 | i64
f32 | f64
ptr<T>
[N x T]
{ T, ... }          struct / tuple
<{ T, ... }>        union
fn(T, ...) -> T     function
%name               named struct or union
```

`i1` is the boolean type. Integer widths are explicit; signedness is carried by the operation (`sdiv` vs `udiv`, `icmp slt` vs `icmp ult`), not by the type. `wchar_t`, `char16_t`, and `char32_t` lower to target integers. References lower to `ptr<T>`; source-level reference types remain in debug metadata. Address space is always 0 in v1.0.

Named structs/unions are declared before use:

```text
%struct.Node = type { i32, ptr<%struct.Node>, ptr<%struct.Node> }
```

Type identity is structural, except that two named structs with different names are distinct. Recursive named structs are allowed through `ptr<T>`.

## Instructions

Arithmetic: `add`, `sub`, `mul`, `udiv`, `sdiv`, `urem`, `srem`, `fadd`, `fsub`, `fmul`, `fdiv`, `frem`.

Bitwise/shift: `shl`, `lshr`, `ashr`, `and`, `or`, `xor`.

Conversions: `trunc`, `zext`, `sext`, `fptrunc`, `fpext`, `fptoui`, `fptosi`, `uitofp`, `sitofp`, `bitcast`, `ptrtoint`, `inttoptr`.

Comparison: `icmp <pred>`, `fcmp <pred>`; result is `i1`. Integer predicates: `eq`, `ne`, `ugt`, `uge`, `ult`, `ule`, `sgt`, `sge`, `slt`, `sle`. Floating predicates: `false`, `oeq`, `ogt`, `oge`, `olt`, `ole`, `one`, `ord`, `uno`, `true`.

Memory: `load`, `store`, `alloca`, `getelementptr`, `memcpy`, `memset`. `load` and `store` carry `align`, `volatile`, and an atomic ordering (`none` in v1.0). `alloca` names its element type. `getelementptr` names its source element type, base, and indices and may be `inbounds`. `vastart` (ADR 0007) produces the target-lowered first variadic stack slot as `ptr<i8>`.

Control: `br`, `condbr`, `switch`, `ret`, `unreachable`.

SSA and aggregates: `phi`, `select`, `extractvalue`, `insertvalue`.

The KR32 and KR16 backends lower `phi`: a phi's value is written on each
incoming edge, at the end of the predecessor, and the block reads the
destination slot. `select` evaluates both operands, so IRGen still carries a
value that may or may not be defined by a branch in an `alloca` slot;
`emit_binary` does that for `&&` and `||`, and `emit_condition_expression` for
`?:`.

Calls: `call` has a callee operand (function or function pointer), arguments, and an explicit result type. Variadic calls name the fixed signature and pass the remaining arguments in order. `sret` is expressed by an explicit pointer argument, not a hidden IR convention.

Intrinsics: `intrinsic` names a target operation that has no portable instruction form ([ADR 0024](decisions/0024-kr32-tile-intrinsics.md)). The operation is named by an `Intrinsic` value, not by the target, and the operands are the operation's arguments. The implemented set is `matrix.multiply`, `matrix.multiply_add`, `matrix.transpose`, `matrix.transform`, `vector.dot`, `transform.push`, `transform.pop`, and `transform.drop`, all taking record pointers. A value-returning intrinsic carries its result type — a 4x4 `f32` tile, a 4-lane `f32` vector, or `f32` — and a stack operation produces `void`. A backend that does not implement an operation rejects it; a target whose profile lacks the capability refuses it before lowering.

Exceptions: `invoke` is a call with two successors — the normal edge and the landing pad the unwinder enters instead of returning ([ADR 0026](decisions/0026-exception-descriptors.md)). `landingpad` opens the pad's block and yields a `ptr<i8>` to an eight-byte scratch holding the exception object pointer at `+0` and its typeinfo at `+4`; the backend fills it on entry. `resume` continues unwinding with the exception the pad received and terminates its block.

## Memory and effects

- No strict-aliasing assumptions in v1.0.
- `volatile` loads/stores are never removed, reordered across other volatile operations, or combined.
- The flag is set from the source access, not from the type: a dereference is `volatile` when the
  pointee is, through a declared pointer and through pointer arithmetic over one — `*p` and `*(p + 1)`
  are both marked. IRGen has no general expression typing, so a pointer returned by a call or
  produced by a cast is not recognised, and an access through it is emitted unmarked.
- Atomics use `seq_cst` first; weaker orders are target/runtime dependent and are not emitted in v1.0 C.
- `memcpy`/`memset` are explicit for aggregate copies and initialization.
- `load` has a result type; `store` has a value operand and a pointer operand. Pointer pointee types are checked where the type is available.

## Debug information

Every instruction may carry `{file, line, column}`. Variables are represented in debug metadata, not as IR values: frame offset, register, address, TLS offset, or location expression. `-O0` uses frame slots; `-O1` may use registers, but v1 guarantees line and function mapping only. A function also carries the source location of its definition, which a backend emits as an entry label and a line record before the prologue ([ADR 0061](decisions/0061-function-definition-location.md)).

## Verifier rules

1. Every block ends with exactly one terminator and has no instructions after it.
2. Every operand is non-null and defined in the same function or is a global/constant/function.
3. SSA definitions dominate uses. A definition in the same block must precede the use; a phi definition dominates all uses except its own incoming values.
4. Phi nodes are first in their block; incoming count equals predecessor count; incoming values match the phi type and dominate the corresponding predecessor edge.
5. Branch/switch targets belong to the same function.
6. Binary arithmetic and comparisons use matching operand types; result types obey the opcode.
7. `load`/`store`/`getelementptr` pointer and pointee types agree; GEP indices are integers; `alloca` returns a pointer; `vastart` returns `ptr<i8>` and has no operands.
8. Calls match the callee function type: arity, fixed parameter types, variadic extras, and return type.
9. `ret` matches the function return type; `br` targets exist; `condbr` condition is `i1`.
10. An `invoke` has two successors and matches its callee's type like a call; a `landingpad` opens its block and produces a `ptr<i8>`; a `resume` takes a pointer and produces `void`.
11. Named structs are complete before use; recursive references go through pointers.
12. An `intrinsic` names an implemented operation, has that operation's operand count, takes record pointers, and produces the type the operation returns. A record *result* of a matrix or vector operation holds the matching count of `f32` lanes, either as that many fields or as one array field of that extent.

Verifier failures abort code generation with a compiler-bug diagnostic; a malformed module never reaches a backend.

## Textual dump

The dump is human-facing and not a stable interchange format. It prints the module whether or not verification failed, so a dump can show a defect the verifier would reject; krc -c is what turns that rejection into a diagnostic. It uses:

```text
%struct.Node = type { i32, ptr<%struct.Node> }

declare i32 @putchar(i32)
@message = constant [6 x i8] c"hello\00"

define i32 @add(i32 %a, i32 %b) {
entry:
  %sum = add i32 %a, %b
  ret i32 %sum
}
```

Constants are `i32 42`, `f64 1.5`, `null`, `undef`, and `c"..."` for a byte array. Globals are `@name = global T ...`; linker-resolved declarations print as `@name = external global T`, C `static` objects print as `@name = internal global T`, and C++ class metadata as `@name = weak constant T ...`. External globals have no storage in the module. An address-array constant prints as `[@fn, @global, 0]`: its words are relocated addresses, which is how a vtable or typeinfo table names what it points at ([ADR 0025](decisions/0025-rtti-vtable-typeinfo.md)). Instruction names are assigned deterministically by the builder; unnamed values print as `%tmpN`. Target id appears as a module comment.

## Passes and levels

- `-O0`: straightforward lowering, all variables in frame slots, full debug.
- `-O1` (the default, [ADR 0060](decisions/0060-o1-default.md)): mem2reg, constant folding, DCE, CSE, copy propagation, CFG simplification, branch folding, peepholes, graph-colouring register allocation.
- `-O2` and `-Os`: the same pipeline run to a second fixed point, kept only when the text is smaller; `inline_small_functions` runs at these levels.

## Non-goals for v1.0

- Address spaces/banking, word-addressed memory, or Harvard code/data split.
- Inline/call-graph metadata or inline-frame reconstruction.
- A stable serialised IR artifact.
- LTO or cross-TU optimisation.

## Freeze checklist

- [x] Type grammar and identity rules.
- [x] Instruction opcode set and operand rules for the v1.0 core.
- [x] Volatile/atomic semantics.
- [x] Exception edges, landing pads, and resume (ADR 0026).
- [x] Vtable/RTTI metadata reserved for v1.1.
- [x] Debug-location representation.
- [x] Pass pipeline and `-O0`/`-O1` guarantees.
- [x] Textual dump grammar (human-facing, not stable).
- [x] Verifier rules.

## Open items

- Address-space extension for future banked/word-addressed targets.
- Inlining and call-graph metadata for a later `-O2`.
- Serialised IR for a future tooling mode.
