# Backend

Each target backend lowers verified IR to assembly. The shared layer holds the machine form and the passes that work on it:

- `machine_ir.*`, `machine_cfg.*`, `machine_liveness.*` — the recorded machine form, its CFG, and register liveness.
- `register_allocator.*` — degree-ordered graph colouring, used at `-O1` and above (ADR 0049).
- `peephole.*` — the textual passes, shared by the targets.

Per-target code lives in `kr32_backend.*`, `kr16_backend.*`, `kr216_backend.*`, and `kr232_backend.*`, with the calling-convention tables in the matching `*_descriptor.*`.

Design: [docs/ir.md](../../docs/ir.md), [docs/targets/kr32.md](../../docs/targets/kr32.md), `plan.md` §9.
