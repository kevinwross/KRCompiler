# Verifying the ARM64 host

This is the checklist for the Raspberry Pi 5 (Debian 13, GCC 14.2) host. It exists because the
ARM64 row of the platform table in `AGENTS.local.md` records a gate at **789 tests**, and the tree
has since moved to **797** with an ABI change among them (ADR 0048 gives KR16 aggregate returns a
hidden stack argument). A stale gate row is believed, so it needs refreshing rather than trusting.

## What this host is verifying

`linux-gcc-debug` and `linux-gcc-release` — the same two presets the Linux x64 host runs. ARM64 is
the only host that builds the tree with a compiler other than MSVC and Linux x64 GCC, so it is the
only independent check on portability.

## The one trap that will waste your time

**`build/vm/` lives inside `build/`, and a clean gate deletes build directories.**

The presets look for the VM binaries at:

```
build/vm/krc_kr32      # CMAKE_EXECUTABLE_SUFFIX is empty on Linux
build/vm/krc_kr16
```

That path is resolved **only at configure time** and then cached, so:

- Placing the binaries *after* configuring still registers **no** run tests, and the suite reports
  green with a lower count. Nothing fails; the count is the only tell.
- Deleting `build/` to get a clean gate deletes the binaries too, and the same silent loss happens.

So either keep the binaries outside the tree and copy them in after configuring:

```sh
mkdir -p build/vm
cp /path/to/arm64/krc_kr32 build/vm/krc_kr32
cp /path/to/arm64/krc_kr16 build/vm/krc_kr16
cmake --preset linux-gcc-debug        # configure AFTER the copies land
```

or delete only the preset directories rather than all of `build/`:

```sh
rm -rf build/linux-gcc-debug build/linux-gcc-release
```

Both are fine. Deleting `build/` wholesale is the one to avoid unless the binaries are backed up.

## The count check

This is the whole point of the checklist:

| Suite count | Meaning |
|---|---|
| **797** | correct — every test registered, 147 of them run a linked image under the VM |
| **650** | the VM binaries were not seen at configure time; 147 run tests were silently skipped |

`650` is not a failure — the suite is green — which is exactly why the count has to be read
deliberately. If it is 650, delete the two preset directories, put the binaries in `build/vm/`,
reconfigure, and rebuild.

## The sequence

Run one preset at a time. The two presets share the staged runtime under `build/`, and running
them concurrently makes one read `libkrcrt.kro` while the other rebuilds it, which surfaces as
`krld: KRO CRC mismatch` in a single link test that passes on its own.

```sh
cd /path/to/KRCompiler
ulimit -s unlimited                       # this image has shown GCC tree-GC segfaults
rm -rf build/linux-gcc-debug build/linux-gcc-release

mkdir -p build/vm                         # re-copy the binaries if build/vm was removed
# cp .../krc_kr32 build/vm/krc_kr32 ; cp .../krc_kr16 build/vm/krc_kr16

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
- whether `build/vm/` was present before the gate or had to be restored;
- the sha256 of each VM binary if they were rebuilt, since the x64 and arm64 hashes differ.

## What is expected to fail

Nothing, if the counts are right. But these have **never** been run on this host and are not part
of the two presets above:

- `linux-clang-debug` and `linux-sanitize` — Clang 19.1.7 is installed on the Pi, so these *could*
  run here even though they cannot run on the Windows/Linux-x64 machine. That would be new
  coverage; it is not part of this checklist and should be a separate change with its own record.
