# Handoff — KRCompiler

**Written:** 2026-11-16, at the close of a session that ran this goal for 149 rounds.
**Repository state:** `origin/master` at the commit recording the
`2026-11-16T09:56` host gate. Working tree clean. Suite green on all four
configurations.

The previous handoff (2026-11-14) is archived as `handoff-2026-11-14.md`. Several
of its claims are now false — notably that `<memory>` is blocked, and that M10 is
the largest gap. **Do not read it as current**; it is kept for its historical
sections on traps and corpus notes only.

`implementation.md` is the record of what actually happened, gate by gate. Its
`## Gate verification` section lists thirty-three gates with four configuration
rows each.

---

## 1. What was asked for

A six-item list, in dependency order:

1. publish the pending table commit behind a host gate
2. variadic templates with fold expressions (M6) — the gate to M10's headers
3. linker hardening: weak/COMDAT symbols, section GC, relocation relaxation
4. `tuple` and `functional` over the new variadics (M10)
5. the KR16 file layer (M9), with a fixture pinning `--fs-root`
6. a CI workflow mirroring the host gate, plus release notes and the Tier 0–2 corpus

Out of scope by the operator's decision: creating the `v1.0` or `v1.1` tags.

**Each item is measured before it is changed, lands with a fixture or a test, and
leaves the suite green on all four configurations.**

---

## 2. State: five of six complete

| # | Item | State |
|---|---|---|
| 1 | Table publish behind a gate | **complete** |
| 2 | Variadic templates, `sizeof...`, folds, pack expansion | **complete** |
| 3 | Linker hardening — all three parts | **complete** |
| 5 | KR16 file layer + `--fs-root` fixture | **complete** |
| 6 | CI, release notes, Tier 0–2 corpus | **complete** except G8 itself |
| 4 | `functional` | **complete**; `make_unique` works on KR32 |
| 4 | `tuple` | **compiles, links, runs — one wrong size** |

Features landed this session, each gated and published:

- **object-level section GC** (`krld --gc-sections`) — the probe with one
  unreferenced object went from 38,203 bytes to **199**, and `--map` no longer
  lists the symbol. Deterministic across rebuilds.
- **branch relaxation** — the backend emits `BRA` for local branches, the
  assembler promotes an out-of-range one to `JMP`. **8%** off a real KR16 image.
- **`Sema::type_of_name`** — a constructor's name resolved through its template
  arguments, so `new T(args...)` works.
- **the parser's pack label** — `typename... Ts` used to have its ellipsis thrown
  away by `static_cast<void>(match("..."))`.
- **variadic class templates** — `Pack<int>`, `Pack<int, char>`,
  `Pack<int, char, long>` fold `sizeof...(Ts)` to 1, 2 and 3.
- **partial specialization** — a class head written as a template-id parses,
  registers apart from the primary, and is selected by argument count.
- **`make_unique`** for KR32. KR16 rejects it for aggregate returns, which is a
  target limit recorded in `sysroot/include/memory`.

---

## 3. The one thing left: `tuple`

### What works today

```cpp
template <typename... Ts> struct Tup { };
template <typename T, typename... R>
struct Tup<T, R...> { T head; Tup<R...> tail; };
int main() { Tup<int, int> t; return 42; }        // compiles, links, runs to 42
```

A recursive variadic class template used to overflow the stack. It does not any
more — see "the fix that landed" below.

### What does not

```cpp
Tup<int, char> t;
sizeof(t.tail)   // 0; should be 1, because Tup<char> is one byte
```

The member resolves to the **empty primary**, not to `Tup<char>`.

### Why

`instantiate_class_template` selects a partial specialization when

```cpp
arguments.size() > fixed      // fixed = parameters.size() - 1
```

For `Tup<T, R...>`, `fixed` is 1, so **arity 1 fails** (`1 > 1` is false) and the
primary is used. The correct comparison is `>=`: the fixed parameter takes the
first argument and the pack absorbs the remainder, which may be empty.

### The trap: `>=` crashes

Setting the rule to `>=` gives the right selection and **reintroduces a stack
overflow**, including at arity one, on a tree that already has the class-name fix.
So this is **not** a one-line change. Do not make it alone.

### What has been ruled out, each by measurement

Do not spend rounds re-testing these; each entry says how it was eliminated.

| Candidate | How it was eliminated |
|---|---|
| a duplicate `RecordInfo` per instance | `record_type` prints exactly one call for the instance |
| deep recursion in `map_record` | depth chain never reaches 12 frames |
| recursion in `instantiate_class_template` | depth chain never reaches 6 frames |
| `analyze_member_declaration` | breakpoint never hit |
| `base_type_from_specifiers` | breakpoint never hit |
| the `default_constructors_` marker | the crash is not in the constructor path |
| an `align_of` depth guard alone | the cycle passes through `size_of` |
| a shared `size_of` + `align_of` guard | **still overflows** |
| `rewrite_instance_tag` as the arity-one cause | a print shows it renames **nothing** at arity one |

### What is known about the crash that `>=` causes

- `align_of` is genuinely part of it: the failing program makes **at least
  10,000** `align_of` calls, a normal program **fewer than 10,000** (call
  histogram, round 142).
- `cdb` shows `Sema::align_of+0x24e` repeated — that offset is the
  `align_of(*member.type)` line — with `size_of` beneath it.
- Under `>=` the crash happens **without any rename**, so the self-reference that
  `>=` produces arrives by a third path.

### The fix that landed

`rewrite_instance_tag` renamed the class name of **every** node whose label
matched, including a node carrying its own `template-arg:` labels. So
`Tup<R...>` inside `Tup<int, int>` became `Tup__int__int__1` — the record being
built — whose layout then contained itself and recursed until the stack ended. It
now skips any node that names its own instantiation.

That fixed the crash. It did not fix the size.

### The next measurement, concretely

A print in `instantiate_class_template` for the **arity-one** case, reporting:

1. the `instance_tag` it computes,
2. what `selected` points at (primary or specialization),
3. what `class_instances_` holds for that tag at that moment.

Ask *what happens*, not *what should change*. That is the only kind of
measurement that has produced results on this defect — see §6.

---

## 4. Building and testing

Use the presets from `AGENTS.local.md`. It has three tiers; use the smallest that
covers the change.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Linux rows, in WSL, **one at a time**, `-j2`, with a raised stack:

```bash
wsl -e bash -lc "cd /mnt/c/Users/kevin/repos/KRCompiler && ulimit -s unlimited && cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j2 && ctest --preset linux-gcc-debug"
```

**CMake is not on `PATH`.** Use
`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.

A **gate** means: delete the four build directories, then build and test each
preset **one after another** — not concurrently, because `ctest` reads the staged
`libkrcrt.kro` while another job may be rebuilding it, which shows up as
`krld: KRO CRC mismatch`. Record it under `## Gate verification` with the
timestamp of when the builds **started**, plus an explicit
`not verified: native Linux x64, Linux ARM64`.

Expected counts: **677** on Windows, **586** on Linux. The difference is the
KR16 executed tests, registered only when a VM exists.

---

## 5. Environment traps that cost real time

1. **Never write C++ through a PowerShell here-string.** A double-quoted
   here-string turns a backtick-n into a real newline, splitting string literals.
   This produced **nineteen** broken files in one session. Use `edit` for every
   repository change. This is the single most expensive mistake in the log.
2. **`std::abort()` in a console app raises a Windows error dialog.** A
   diagnostic that aborts puts a popup on the operator's screen on every run. Use
   `_exit`, or return a value, or run the crashing input **once** under `cdb`
   where the debugger catches it.
3. **Verify a symbol before breaking on it.** `x krc!*align_of*` shows the real
   name is `Sema::align_of_type`. "No breakpoint hit" is ambiguous between "not
   called" and "never bound"; it was read the wrong way here.
4. **A depth counter needs a decrement.** Without one it counts total calls,
   aborts during an ordinary build, and surfaces as
   `MSB8066 ... exited with code -2147483645`.
5. `cdb` truncates the symbol column in `kb`/`k` regardless of width.
6. Debugging Tools for Windows:
   `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`.
7. Never run `kr32.exe` without asking. Private copies with `SDL2.dll` live in
   `build/vm/`; KR16 needs **both** `--fs-root` and `--fs-quota`.
8. `../kr16` and `../kr32` are read-only inputs except their `inbox/` directories.
9. `AGENTS.local.md` is an instruction file, not a log. Milestone state belongs in
   `implementation.md`.

---

## 6. The method that worked, and the one that did not

**What worked, every time:** instrument the site of failure and read a value.

- section GC — counted how many objects were dropped
- relaxation — counted how many branches were promoted: of 1,152, only **56**,
  and that was the whole clue
- `type_of_name` — printed `params=2` against the loop's 1
- the pack label — printed `pack_index=1` where 0 was needed
- the crash — a call histogram, at least 10,000 against fewer than 10,000
- the class-name bug — grepped for where the value is **written**

**What failed, for forty rounds:** reasoning about which layer the defect was in.

Fourteen conclusions of the form "the fix is a small change in X" were wrong, and
each was corrected by a measurement. Two of them were *corrections of a correct
finding*, because a print without a node identity was read as being about the
wrong thing.

**Three rules that would have saved most of this session:**

1. **Instrument the producer, not the consumer.** A value arrives wrong long
   before the function you are staring at reads it.
2. **Put an identity in every print** — a node pointer, a tag, a count — so a
   print cannot be mistaken for being about something else.
3. **Treat silence from a guarded path as ambiguous.** "The breakpoint did not
   hit" could mean "not called" or "never bound". Check which first.

---

## 7. Suggested first hour

1. Read the last twenty entries of `implementation.md`; the chain is there.
2. Confirm the tree is green: `ctest --preset windows-msvc-debug` — expect 677.
3. Promote `build/corpus/tail1.cpp` to `tests/fixtures/c/`. It asserts
   `sizeof(Tup<int, char>.tail) == 1` and fails today for the right reason.
4. Add the print described in §3 to `instantiate_class_template`.
5. **Do not change the comparison rule on its own.** The crash it causes has a
   third cause that neither the rule nor the rename explains.

If the print shows the arity-one instance being created and cached correctly, the
fix is in how the member's specifier is resolved against it. If it shows the
instance created twice, the fix is identity. One build answers it either way.

---

## 8. Honest note on the session that wrote this

The goal ran 149 rounds and about twenty-one hours. Five of six items are done
and gated, and this document describes the sixth in more detail than the five
completed items combined.

The last fifty rounds produced **one** fix — a good one, published — and a great
deal of narrowing. The narrowing is genuine: ten candidates measured and
eliminated, and the two that remain are named above. But the same period contains
nineteen broken files from one shell mistake, two rounds spent correcting a
correct finding, and three reverts that discarded working code along with broken
code.

If you take one thing from this file: **the measurements are reliable and the
conclusions were not.** Trust the tables in §3, re-derive the fix from a fresh
print, and do not trust any "this is one small change" claim that is not
immediately followed by a test result.
