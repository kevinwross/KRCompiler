# Assembler

Each target supplies a syntax and encoding module over a shared framework: `kr32_assembler.*` and `kr32_encoder.*`, `kr16_assembler.*`, `kr16_encoder.*` and `kr16_object.*`, `kr216_encoder.*` and `kr216_object.*`, and `kr232_parser.*`, `kr232_assembler.*`, `kr232_encoder.*` and `kr232_object.*`. The shared `kro_writer.*` writes the KRO container.

Design: `plan.md` §9.4, [docs/object.md](../../docs/object.md).
