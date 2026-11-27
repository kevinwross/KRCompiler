# Test fixtures

Fixtures are grouped by how the suite consumes them:

- `c/` — the C and C++ fixtures, one behaviour per file. Each is registered in
  [tests/CMakeLists.txt](../CMakeLists.txt) as a syntax check, a compile, a link,
  or a link-and-run row, depending on the target and what it exercises.
- `extracted/` — the seed extracted from the sibling doctest suites, with
  `manifest.tsv` recording the source path, source line, output file, and byte
  count for each file. The tiered corpus in `tests/corpus/extracted/` is the
  wider extraction; see [tests/corpus/README.md](../corpus/README.md).
- `games/` — the scripted input for the three game examples.
- `kr232-fixture.s`, `kr232-draw.s`, `kr232_offset_form.s`, `status-high.s` —
  assembly fixtures for the KR232 image rows.
- `kr232-vectors.txt` — the KR232 encoder vector table.

Generate or refresh the seed with:

```sh
krc_extract_fixtures --out tests/fixtures/extracted \
  ../kr32/tests/unit/test_cc_1.cpp \
  ../kr32/tests/cxx/test_cxx_codegen_1.cpp
```

Rules:

- Do not edit extracted files by hand; change the extractor or the upstream doctest source.
- Keep the extractor's output deterministic.
- A fixture is `.c` when it comes from a C test file and `.cpp` when it comes from a C++ test file.
- A fixture that needs a target image is build output, not a committed binary.
