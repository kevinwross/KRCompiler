# Verifying the ARM64 host

This is the checklist for the Raspberry Pi 5 (Debian 13, GCC 14.2) host. It exists because the
ARM64 row of the platform table in `AGENTS.local.md` records a gate, and the tree moves between
gates. A stale gate row is believed, so refresh it rather than trusting it: re-run the two presets
and record the count you actually observed.

## What this host is verifying

`linux-gcc-debug` and `linux-gcc-release` — the same two presets the Linux x64 host runs. ARM64 is
the only host that builds the tree with a compiler other than MSVC and Linux x64 GCC, so it is the
only independent check on portability.

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
deliberately. If it is 650, delete the two preset directories, put the binaries in `vm/`,
reconfigure, and rebuild.

## The sequence

Run one preset at a time. The two presets share the staged runtime under `build/`, and running
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

Nothing, if the counts are right. But these have **never** been run on this host and are not part
of the two presets above:

- `linux-clang-debug` and `linux-sanitize` — Clang 19.1.7 is installed on the Pi, so these *could*
  run here even though they cannot run on the Windows/Linux-x64 machine. That would be new
  coverage; it is not part of this checklist and should be a separate change with its own record.
