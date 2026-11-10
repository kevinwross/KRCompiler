# Runtime

Implementation starts with the M3 minimal C runtime and continues at M9/M10. The runtime is common source plus a HAL and per-target crt/syscall/soft-float/unwinder stubs.

Design: `plan.md` §14, [docs/abi.md](../docs/abi.md).
