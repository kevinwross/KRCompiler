# Dynamic instruction benchmarks

The optimiser has two useful measurements: emitted size, which the assembly
snapshots expose, and retired instructions, which the VM reports when it halts.
`cmake/BenchmarkImage.cmake` records the second without adding a profiler to a
target.

Build one image per level, then run each image:

```sh
krc -O0 -o bench-o0.kr32 source.c
krc -O1 -o bench-o1.kr32 source.c
krc -O2 -o bench-o2.kr32 source.c

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

