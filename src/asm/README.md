# Assembler

Implementation starts at **M2** (KR32 syntax/encoding) and continues at **M3** (object writer, relocations, relaxation). The framework is shared; each target supplies syntax and encoding modules.

Design: `plan.md` §9.4, [docs/object.md](../docs/object.md).
