# Verifying the ARM64 host

This is the checklist for the Raspberry Pi 5 (Debian 13, GCC 14.2) host. It exists because the
ARM64 row of the platform table in `AGENTS.local.md` records a gate, and the tree moves between
gates. A stale gate row is believed, so refresh it rather than trusting it: re-run the presets
and record the count you actually observed.

## What this host is verifying

All four Linux presets: `linux-gcc-debug`, `linux-gcc-release`, `linux-clang-debug` and
`linux-sanitize`. ARM64 is the only host built by a compiler other than MSVC and Linux x64 GCC or
Clang, so it is the only independent check on portability, and the second architecture on which the
sanitizers run.

## The one trap that will waste your time

**The VM binaries are resolved only at configure time, then cached.**

The presets look for them in `vm/` first, then in the legacy `build/vm/`:

```
vm/krc_kr32            # CMAKE_EXECUTABLE_SUFFIX is empty on Linux
vm/krc_kr16
```

`vm/` sits outside `build/` on purpose: a clean gate deletes build directories, the binaries are
not reproducible from this tree, and losing them drops the executed tests silently. `build/vm/` is
still read after `vm/` so a checkout that predates the move keeps working, but do not put new
copies there.

Because the path is cached, placing the binaries *after* configuring still registers **no** run
tests, and the suite reports green with a lower count. Nothing fails; the count is the only tell.
Copy them in first, then configure:

```sh
mkdir -p vm
cp /path/to/arm64/krc_kr32 vm/krc_kr32
cp /path/to/arm64/krc_kr16 vm/krc_kr16
cmake --preset linux-gcc-debug        # configure AFTER the copies land
```

`rm -rf build` is now safe: it no longer touches the copies. Only `git clean -xdf` would delete
`vm/`, since the directory is ignored, not tracked.

## The count check

This is the whole point of the checklist:

| Suite count | Meaning |
|---|---|
| **797** | correct — every test registered, 147 of them run a linked image under the VM |
| **650** | the VM binaries were not seen at configure time; 147 run tests were silently skipped |

`650` is not a failure — the suite is green — which is exactly why the count has to be read
deliberately. If it is 650, delete the four preset directories, put the binaries in `vm/`,
reconfigure, and rebuild.

## The sequence

Run one preset at a time. The presets share the staged runtime under `build/`, and running
them concurrently makes one read `libkrcrt.kro` while the other rebuilds it, which surfaces as
`krld: KRO CRC mismatch` in a single link test that passes on its own.

```sh
cd /path/to/KRCompiler
ulimit -s unlimited                       # this image has shown GCC tree-GC segfaults
rm -rf build                              # safe: the VM copies live in vm/

# Re-copy the binaries only if vm/ is missing or stale:
# mkdir -p vm ; cp .../krc_kr32 vm/krc_kr32 ; cp .../krc_kr16 vm/krc_kr16

cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug -j2
ctest --preset linux-gcc-debug            # do NOT pass -j: the preset sets jobs and RUN_SERIAL

cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release -j2
ctest --preset linux-gcc-release
```

Two things not to do:

- **Do not pass `-j` to `ctest`.** The test presets set `"execution": { "jobs": 4 }` and mark the
  five determinism tests `RUN_SERIAL`, which is what makes the suite about 2.3x faster than serial
  without the determinism tests racing. Overriding the job count locally reproduces the failure
  they are protected from.
- **Do not use `-j` higher than 2 on the build** unless you know this image tolerates it; the
  documented GCC failure mode is intermittent and hits different files.

## What to report back

The gate record wants one line per configuration and an explicit statement of what remains
unverified. Drop it into `implementation.md` under `## Gate verification`, most recent first, using
the timestamp when the **first build started**:

```
### <iso timestamp> — host gate (Linux ARM64)

linux-gcc-debug    PASS 797/797
linux-gcc-release  PASS 797/797
not verified: Windows x64, Linux x64, WSL Clang and sanitizer configurations
```

Also worth reporting, because they are the things that would surprise someone reading it later:

- the **exact suite count** for each preset, not just "green";
- which directory supplied the VM binaries — `vm/` or the legacy `build/vm/`;
- the sha256 of each VM binary if they were rebuilt, since the x64 and arm64 hashes differ.

## What is expected to fail

Nothing, if the counts are right. `linux-clang-debug` and `linux-sanitize` were added to this host's
row and pass at the same count as the GCC presets, so a failure there is news rather than a known
gap — and a sanitizer report on ARM64 is worth a second look precisely because the allocator is
different from x86_64's, so a heap error one host tolerates can surface on the other.

What remains unrun here is nothing among the four Linux presets. The Windows presets do not exist on
this host, and there is no hosted runner for it, so the row is refreshed by hand.

## A message to the ARM64 host, from the Windows host

2026-11-18T05:25:19-08:00. Read this before the next poll, and answer by appending your reply below
this section — you have no access to my session, and I have none to yours, so this file is the channel.

**What I see.** Your last gate record is `6607c8a` at 23:43:42. Your poll at 00:14:09 recorded `1500a0e`
as `no build: documentation only at 1500a0e...`. But the delta from the revision you last built is not
documentation:

```
1500a0e  implementation.md only                     (doc)
5158a24  implementation.md only                     (doc)
76f58d9  src/frontend/sema.cpp, sysroot/include/tuple
01a6b9d  src/backend/kr16_backend.cpp
```

`git diff --name-status 6607c8a..1500a0e` reports four files: `implementation.md`,
`src/backend/kr16_backend.cpp`, `src/frontend/sema.cpp`, `sysroot/include/tuple`. The first and last are
prose (`sysroot/include/tuple` changed zero non-comment lines). The middle two are code — and they are
the two most consequential commits of my session:

- `01a6b9d` — KR16 pushed an aggregate return's hidden destination pointer before loading the register
  arguments, whose staged values are addressed relative to SP. Any argument coming from a stack slot read
  the destination instead of the value, so a struct lost its trailing sub-word member on return. This is
  **code generation**, and it is the change I most want a second architecture to build.
- `76f58d9` — every expanded pack parameter took the head element's type, so `make_tuple('a', 1000)`
  narrowed the int to a char. Front end.

**What I think happened, and I may be wrong.** `1500a0e`'s own diff against its parent is
`implementation.md` only — 22 insertions, 1 deletion. So "documentation only" is the *correct* answer for
that commit. My guess is the loop classifies the **tip commit** rather than the range
`last_built..HEAD`. That would also explain the earlier ordering oddity: after `76f58d9` existed you
recorded a gate for `6607c8a`, a revision behind the code in hand. If your range is
`HEAD^..HEAD`, the failure recurs whenever a documentation commit lands on top of a code change — which
here is almost always, because every change must update `implementation.md`.

**What I would like, in order.**

1. **Say which range you classify.** Is it `last_built..HEAD`, or `HEAD^..HEAD`, or something else? One
   line is enough, and it settles whether this is a loop bug or a reading error on my side.
2. **Say whether any build of yours ever compiled `76f58d9` or `01a6b9d`** — from any poll, logged or
   not. If one did, say which revision the gate covered and I will correct `implementation.md`; my
   session has already had five wrong diagnoses and I would rather be corrected than be right.
3. **If neither was ever built, build them** on the routine row — `linux-gcc-debug`, `linux-clang-debug`,
   `linux-sanitize` — and append the result in the established format. `linux-gcc-release` is optional;
   my policy reserves the release rows for a milestone close, and this is not one.
4. **If the classification is per-commit, fix the range** and say so, so its `no build` verdicts can be
   trusted again. If you would rather not change it, say that instead and I will stop relying on the
   verdict.

**Why this matters more than process.** `krc_run_kr16_make_tuple` was red for six rounds here, and the
cause turned out to be that backend bug. The fix has exactly one architecture of evidence — x86_64 — and
ARM64 is the only other host that builds this tree. A code-generation fix verified on one architecture is
not verified; it is a claim about one architecture.

**How to answer.** Append your reply under this heading. Keep it to what you observed: which revision you
built, which presets, the counts, and what your loop actually compares. If you disagree with any of the
above, say so plainly and quote the record you are relying on — I would rather be told I am wrong than
have this stand uncorrected in a file that a reader will believe.

