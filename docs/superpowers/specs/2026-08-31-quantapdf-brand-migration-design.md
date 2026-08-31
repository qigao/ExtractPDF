# QuantaPDF Brand Migration Design

## Decision

The project, native library artifact, CMake package identity, tests, manifest,
and current user-facing documentation adopt **QuantaPDF**. The product line is:

> QuantaPDF — PDF made easy.

## Breaking rename boundary

This migration intentionally removes the old identity rather than preserving a
compatibility layer. Exported symbols, public types, constants, include guards,
test hooks, and private identifiers are renamed to the `quantapdf_*` and
`QUANTAPDF_*` namespaces. The only public include is
`<quantapdf/quantapdf.h>`.

The primary CMake target is `quantapdf` with canonical alias
`QuantaPDF::QuantaPDF`. No legacy alias is retained. The produced native
library is named `quantapdf`.

`QUANTAPDF_BUILD_TESTS` is the only test-build option.

## Repository cleanup

The unused root `libpdf.c` MuPDF 1.3 prototype is deleted. It is not part of
the current build or ABI, relies on obsolete APIs and global state, and is
already superseded by the `src/` implementation and regression suite.

Historical plans and specifications are renamed and updated so the repository
contains no old product identifier in filenames or text.

## Verification

Static verification must prove that:

- current build metadata uses QuantaPDF/quantapdf;
- only the canonical CMake alias exists;
- the canonical public header directly declares the QuantaPDF ABI;
- `libpdf.c` and current references claiming it remains present are gone;
- patch whitespace is clean.

Configure, build, and CTest verification require the repository's versioned
root CMake presets. Their absence remains a blocking configuration defect and
must not be bypassed with ad hoc build commands.
