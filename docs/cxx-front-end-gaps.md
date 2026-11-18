# C++ front-end gaps found by examples/KRlibcpp.cpp

**Status:** current state, 2026-11-18. This is a list of constructs the compiler
rejects or mishandles today, each with a reproducer and the diagnostic it
produces, so they can be fixed deliberately rather than rediscovered. It is not
a plan and it claims nothing about when each will be closed.

**Why this file exists.** `examples/KRlibcpp.cpp` is a 3,500-line C++ body of
code intended as a code-generation benchmark, and the compiler could not compile
it. Working through the rejections produced four distinct gaps. **Gap 4 is now
fixed** (it was the only one that produced invalid IR); the other three are open,
and one of those is a deliberate design decision rather than a defect.

**How each entry was verified.** Every entry was reproduced with a **full
compile** (`krc -S <file> -o <file>.s`, or `krc -c`), not with `-fsyntax-only`,
and every entry names a **control** — a closely related shape that does compile
— so the boundary of the gap is known rather than guessed. See
[The `-fsyntax-only` trap](#the--fsyntax-only-trap) for why that distinction is
called out.

| # | Construct | Diagnostic | Status |
|---|---|---|---|
| 1 | `new T[n]` with a runtime `n` | `variable length arrays are not supported` | gap |
| 2 | `throw <scalar>` | `throw requires a class type` | **deliberate**, tested |
| 3 | Zero-argument functional cast `T()` | `expected expression` / `parenthesized initializer takes exactly one value` | gap |
| 4 | Initialising or assigning a record from an overloaded-operator result | `expression is not an lvalue`, then `IR verifier: ... getelementptr has an invalid base or result` | **fixed**, see below |

---

## 1. A `new`-expression with a runtime bound

```cpp
int runtime_new(int n) { int* p = new int[n]; delete[] p; return 0; }   // rejected
int fixed_new()        { int* p = new int[4]; delete[] p; return 0; }   // compiles
```

**Diagnostic:**

```text
gap1.cpp:1:42: error: variable length arrays are not supported
  int runtime_new(int n) { int* p = new int[n]; delete[] p; return 0; }
```

**Site:** `src/frontend/sema.cpp:1206`, in `apply_declarator_operations`'s
`ArrayDeclarator` case. That is the *ordinary declarator* path, so the
new-expression's bound is reaching it rather than a `new[]`-specific check — a
reading of the code, not a measured call path.

**Boundary:** the bound must be a constant. `new int[4]` is fine, and so are
local arrays whose size is constant.

**Affects:** `examples/KRlibcpp.cpp:2265`, `int* fresh = new int[grown];` inside
`KRIntVector::reserve`, where `grown` doubles at runtime.

**Direction if it is picked up:** the bound is a runtime value, so this is a
codegen gap as much as a front-end one — `operator new[]` would need the
element count scaled by the element size at runtime. Not a parser fix.

---

## 2. Throwing a scalar

```cpp
try { throw 1; } catch (...) { return 1; }   // rejected
```

**Diagnostic:**

```text
gap2.cpp:2:11: error: throw requires a class type
      try { throw 1; } catch (...) { return 1; }
```

**Sites:** `src/frontend/sema.cpp:3247`, and again in IR generation at
`src/irgen/c_irgen_expressions.cpp:3697` and `:3702`.

**This one is deliberate, not a defect.** The `.cpp_except`/unwinder design
(ADR 0026) carries a class object with its typeinfo; a scalar has neither, so it
is rejected rather than thrown as a raw value.
`tests/fixtures/c/throw_scalar.cpp` pins the diagnostic, and the test
`krc_cxx_throw_scalar` fails the day scalar throw starts working, which is the
signal to write the supporting design rather than a rewrite.

**Affects:** `examples/KRlibcpp.cpp:3281, 3288, 3291, 3298, 3301` — five
`throw <int>` sites, with `catch (int code)` handlers at `:3311` and `:3336`.

**Closing it** means deciding what a scalar exception *is*: an ADR-level
question (a boxed scalar plus typeinfo, or the existing class path with a
synthesised type), not a local patch.

---

## 3. A zero-argument functional cast, `T()`

Two different failures for what C++ treats as value-initialisation.

**Concrete type name — rejected by the parser:**

```cpp
int scalar_value_init() { int total = int(); return total; }
```

```text
gap3c.cpp:1:39: error: expected expression
  int scalar_value_init() { int total = int(); return total; }
                                        ^
```

**Dependent type in a template — rejected in IR generation:**

```cpp
template <typename T>
T total_of(const T* data, int count) { T total = T(); /* ... */ return total; }
int use_total(const int* data) { return total_of(data, 3); }   // instantiating T=int
```

```text
gap3.cpp:3:5: error: parenthesized initializer takes exactly one value
      T total = T();
      ^
```

**Site:** `src/irgen/c_irgen.cpp:1710-1712` — the path that supports `int x(7)`
requires exactly one argument and reports this message for an empty list.

**Boundary:** `S s = S();` for a *concrete struct* compiles (verified), so it is
the empty argument list and the dependent type that are unsupported, not
parenthesised initialisation as a whole.

**Affects:** `examples/KRlibcpp.cpp:2982` (`KRFixedArray<T,N>::total()`) and
`:3006` (`KRAccumulate<T>`), both instantiated with arithmetic types.

**Workaround in the benchmark copy:** `T sum = 0;`, which is exactly equivalent
for the arithmetic instantiations that exist. It would not be for a class `T`.

---

## 4. A record initialised or assigned from an overloaded-operator result

The only remaining blocker for the benchmark, and the worst of the four because
it leaves the module invalid rather than merely rejecting one line.

```cpp
struct M {
    int v;
    M operator+(const M& other) const { M r; r.v = v + other.v; return r; }
};
int from_operator() {
    M a; M b; a.v = 1; b.v = 2;
    M c = a + b;          // rejected
    return c.v;
}
```

**Diagnostics, before the fix:**

```text
gap4.cpp:7:13: error: expression is not an lvalue
      M c = a + b;
```

The assignment form failed identically, and in the same function the module then
failed verification:

```text
gap4-assign.cpp:8:11: error: expression is not an lvalue
      c = a * b;
error: IR verifier: function 'assign_from_operator': getelementptr has an invalid base or result
```

**Site:** `src/irgen/c_irgen_expressions.cpp`, the fall-through in `emit_lvalue`.
That function handles a subscript, a member, a move call and a `CallExpression` —
materialising a temporary for a call that does not return a reference — and
everything else was "not an lvalue". An overloaded operator is an
`AstKind::BinaryExpression` (or unary), not a `CallExpression`, so it never
reached the temporary-materialising branch.

**Fix:** `emit_lvalue` now handles a binary or unary expression the way it
handles a call: it emits the expression, and when the result is a record it
materialises it into a temporary and returns that as the lvalue. A *scalar*
operator result is still not an lvalue and keeps the diagnostic, so this did not
widen into "any expression is assignable". The regression is pinned by
`C IRGen initialises a record from an overloaded operator result`
(`tests/unit/test_irgen.cpp`), which needed a C++ front-end entry point in that
file — `generate_cxx`, because the front end selects the language from the input
extension and the existing helper parsed everything as C.

**Consequence:** `examples/KRlibcpp.krc.cpp` now compiles for KR32 and
`examples/KRlibcpp.S` is its assembly — 31,188 instruction lines across 224
global functions. `examples/KRlibcpp.cpp` itself still needs gaps 1 and 2.

**Controls that compile** (verified):

- `M c = make();` where `make()` returns `M` by value — so the gap is *not*
  "no copy-initialisation from a temporary"; the operator result is what
  changes the outcome.
- `return a + b;` from a function declared `M f(M a, M b)` — so the operator
  result is usable as a value; it is only *initialising or assigning from* it
  that is rejected.
- `operator+` returning `M` by value, and a struct with an array member
  (`float m[9]`) copied with `b = a` — both compile, so neither the return
  convention nor the array member is the cause.

**Affects:** `examples/KRlibcpp.cpp:2600`, `KRMat3 product = rotation * scaled;`
in `KRMat3Exercise`. Every other record-valued operator in the file
(`KRVec2::operator+`, `-`, `*`) is used only inside expressions that reach a
scalar, so this is the single site.

**Suggested direction:** extend `emit_lvalue`'s `CallExpression` branch to any
expression whose result is a record — an operator call is a call — and
materialise the temporary there. The interesting part is not the fix but the
knock-on: today the failed lvalue leaves an operand empty and IR generation
keeps going, which is what turns one rejected line into an invalid module.

---

## The `-fsyntax-only` trap

`-fsyntax-only` stops before IR generation and before the IR verifier, so it
passes on constructs that fail to compile. During this investigation it produced
two confidently wrong conclusions, both now disproved, and they are recorded
here so nobody spends the same effort:

1. **"The failure only appears when the tail of the file is present."** Wrong.
   Every bisect of the file used `-fsyntax-only`, so every "prefix compiles"
   result was meaningless for codegen. The failure is local: the same block, and
   a 12-line reproducer, fail on their own.
2. **"Replacing local array declarations with struct members fixes it."** Wrong,
   same cause. The change made `-fsyntax-only` pass and did nothing else; it has
   been reverted, and local constant-size arrays are fine (verified).

The lesson is the one already recorded in `AGENTS.local.md` under known traps:
a rejection that is expected to fail later than syntax analysis must be
**compiled**, not merely parsed.

---

## State of `examples/KRlibcpp.krc.cpp`

The original `examples/KRlibcpp.cpp` is untouched. `examples/KRlibcpp.krc.cpp`
is a copy carrying the smallest adaptations for the gaps that are still open,
each marked in the file with a comment:

| Gap | Adaptation in the copy |
|---|---|
| 1 | `reserve()` caps the capacity it records and allocates a fixed block (`new int[1024]`) |
| 2 | `KRError` (one `int code` field) plus a `KRThrow(int)` helper; handlers catch `KRError&` and read `.code` |
| 3 | `T sum = 0;` / `T total = 0;` in the two templates |
| 4 | none — the gap is fixed in the compiler |

It compiles for KR32, and `examples/KRlibcpp.S` is the assembly it produces:

```text
krc -S --target=kr32 examples/KRlibcpp.krc.cpp -o examples/KRlibcpp.S
```

That file is generated and is deliberately **not checked in** — the examples
directory carries sources, not artefacts, and the command above reproduces it.

31,188 instruction lines across 224 global functions, 1.25 MB — comparable to
`targets/kr32/runtime/libc.c` (27,091 instructions), which makes it a plausible
second size-and-ticks workload once gaps 1-3 close and the original file can be
used unmodified. It has **not** been added to `benchmarks/`: a workload whose
source needs three adaptations measures the adapted source, not the file.

The file's own header names its compile line as `kr32 cxx ... -S -O1`, i.e. it
was authored for the sibling `kr32` compiler. Some of these gaps are therefore
differences between the two front ends rather than defects in either — worth
knowing before treating the rest as bugs.
