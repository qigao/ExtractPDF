# QuantaPDF Brand Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Completely replace the old project identity with QuantaPDF across the product, build, ABI, tests, and documentation.

**Architecture:** Perform one deliberate breaking namespace migration. Rename every public/private identifier and repository text occurrence, move the public header and historical document filenames, and retain no compatibility alias or wrapper.

**Tech Stack:** C11, MuPDF 1.28.2, CMake, CTest, vcpkg manifest.

**Spec:** `docs/superpowers/specs/2026-08-31-quantapdf-brand-migration-design.md`

## Global Constraints

- Exported symbols/types use `quantapdf_*` and constants/macros use `QUANTAPDF_*`.
- `<quantapdf/quantapdf.h>` is the only public include.
- `QuantaPDF::QuantaPDF` is the only namespaced CMake alias.
- The obsolete root `libpdf.c` is deleted and no current documentation may claim it remains present.
- CMake configure/build/test commands are not run until the required root presets exist.

---

### Task 1: Rename the public ABI and build identity

**Files:**
- Move: the former public header into `include/quantapdf/quantapdf.h`
- Modify: `CMakeLists.txt`
- Modify: `vcpkg.json`

**Interfaces:**
- Produces: `<quantapdf/quantapdf.h>`, `quantapdf_*`/`QUANTAPDF_*`, target `quantapdf`, alias `QuantaPDF::QuantaPDF`, and option `QUANTAPDF_BUILD_TESTS`.

- [x] Move and rename the public header and every ABI declaration.
- [x] Rename the CMake project and concrete library target to QuantaPDF/quantapdf.
- [x] Remove the superseded CMake alias and test-build option.
- [x] Rename the vcpkg manifest package to `quantapdf`.

### Task 2: Rename internal test/build identities

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `QuantaPDF::QuantaPDF`.
- Produces: `quantapdf_test_*` executable targets and `quantapdf.*` CTest names.

- [x] Replace test target prefixes with `quantapdf_test_`.
- [x] Link every test through `QuantaPDF::QuantaPDF`.
- [x] Rename registered CTest names to `quantapdf.*`.
- [x] Rename product-prefixed C functions used for test-main redirection consistently.

### Task 3: Rename documentation and remove the prototype

**Files:**
- Delete: `libpdf.c`
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-08-27-quantapdf-v2-design.md`

**Interfaces:**
- Produces: QuantaPDF product copy and historical records with QuantaPDF filenames and identifiers.

- [x] Delete the unreferenced MuPDF 1.3 prototype.
- [x] Change the README title, product copy, library filename, and supported include to QuantaPDF.
- [x] Rename all historical document filenames and text occurrences.
- [x] Replace the legacy-retention statement with the prototype-removal decision.

### Task 4: Verify migration consistency

**Files:**
- Inspect: all modified files and repository status.

**Interfaces:**
- Consumes: the completed rename.
- Produces: static consistency evidence and, once presets exist, build/test evidence.

- [x] Search the full repository for any superseded product identifier in content and paths.
- [x] Confirm `libpdf.c` is absent and no current document claims it remains present.
- [x] Run `git diff --check`.
- [x] Query CMake presets and record the existing preset blocker without bypassing it.
