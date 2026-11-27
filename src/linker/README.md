# Linker

`kro_object.*` holds the object model. The linker reads KRO objects and archives, resolves symbols (including weak definitions), applies relocations, builds KDBG data from the debug bridge, and writes the per-target images through `kr16_image.*`, `kr216_image.*`, and `kr232_image.*`; `archive.*` reads GNU and BSD KRO archives. COMDAT groups are not implemented.

Design: [docs/object.md](../../docs/object.md), `plan.md` §12.
