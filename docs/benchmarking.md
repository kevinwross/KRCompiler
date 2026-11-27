# Dynamic instruction benchmarks

The optimiser has two useful measurements: emitted size, which the assembly
snapshots expose, and retired instructions, which the VM reports when it halts.
`cmake/BenchmarkImage.cmake` records the second without adding a profiler to a
target.

Build one object per level, link each against the staged runtime, then run each
image:

```sh
stage=build/windows-msvc-debug/stage/sysroot/kr32/lib
rt=targets/kr32/runtime

krc -c -O0 source.c -o bench-o0.kro
krc -c -O1 source.c -o bench-o1.kro
krc -c -O2 source.c -o bench-o2.kro

for level in o0 o1 o2; do
    krld "$rt/crt0.s" "bench-$level.kro" "$stage/libkrcrt.kro" \
         "$rt/fileio.s" "$rt/eh.s" -o "bench-$level.kr32"
done

cmake -DVM=path/to/kr32-vm -DIMAGE=bench-o0.kr32 \
      -DNAME=source-o0 -DOUTPUT=bench.tsv \
      -P cmake/BenchmarkImage.cmake
cmake -DVM=path/to/kr32-vm -DIMAGE=bench-o1.kr32 \
      -DNAME=source-o1 -DOUTPUT=bench.tsv \
      -P cmake/BenchmarkImage.cmake
```

Each invocation appends a tab-separated row:

```
name    retired instructions    exit status
```

`MAX_STEPS` can raise the default 50,000,000-instruction budget. `ARGS` passes
guest arguments after the image name. The count is retired instructions, not
cycles: the KR32 and KR16 VMs have no cycle model, so a lower number is fewer
instructions, not necessarily less wall time. On Windows, pass the VM and image
paths with native backslashes; CMake's `execute_process` does not launch a
forward-slash VM path on every host.

The script fails if the image prints no step count. It is deliberately not a
CTest case: a benchmark records a number, while a test needs an assertion. Use
the recorded rows to compare `-O0`, `-O1`, `-O2` and `-Os` on the same source.

## Recorded run: bench_loop on KR32

Source: `tests/fixtures/c/bench_loop.c` (200,000 iterations). Images were linked
with `cmake/LinkRuntime.cmake` against the staged KR32 runtime and run with the
KR32 VM through `cmake/BenchmarkImage.cmake`.

| Build | Retired instructions | Exit |
|---|---:|---:|
| `-O0` | 7,400,037 | 96 |
| `-O1` | 4,400,030 | 96 |
| `-O2` | 4,400,030 | 96 |

O1 retires 40.5% fewer instructions than O0. O2 is identical to O1 on this
kernel: the benchmark has one hot loop and no calls or frame slots for the O2
passes to remove.

## Recorded run: startrekng on KR32

The multi-file game's scripted mission was run under the KR32 VM at O1 and O2.
The counts are the VM's final `ticks=` value; the transcript is too large for
`BenchmarkImage.cmake`'s captured-output path.

| Build | Retired instructions | Exit |
|---|---:|---:|
| `-O1` | 9,555,395 | 0 |
| `-O2` | 9,514,595 | 0 |

O2 retires 40,800 fewer instructions, a 0.43% reduction. The game is dominated
by calls and address-taken objects, which limits the O2 promotion passes.


