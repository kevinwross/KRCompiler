# Test fixtures

`extracted/` contains C and C++ source extracted from the sibling KR32/KR16 doctest suites. The files are committed so the compiler has a stable corpus independent of the sibling repositories.

Generate or refresh them with:

```sh
# From the repository root, after M0 builds.
krc_extract_fixtures --out tests/fixtures/extracted \
  ../kr32/tests/unit/test_cc_1.cpp \
  ../kr32/tests/cxx/test_cxx_codegen_1.cpp
```

The current M0 set is a deliberately small seed (five fixtures per source file). The full extraction is a later M1 task. `manifest.tsv` records the source path, source line, output file, and byte count for each fixture.

Rules:

- Do not edit extracted files by hand; change the extractor or the upstream doctest source.
- Keep the extractor's output deterministic.
- A fixture is `.c` when it comes from a C test file and `.cpp` when it comes from a C++ test file.
