# Linker

Implementation starts at **M3** (`krld`). It reads `KRO` objects, resolves weak/COMDAT symbols, applies relocations, relaxes branches, merges debug data, and invokes per-target image writers.

Design: [docs/object.md](../docs/object.md), `plan.md` §12.
