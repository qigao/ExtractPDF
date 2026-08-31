# QuantaPDF Design Review Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the ABI, exception-safety, signature-detection, and documentation gaps found in the 2026-08-31 architecture review.

**Architecture:** Keep public handles and existing function signatures intact. Add one private, read-only rewrite-security module shared by crop, trim, poster split, and PDF edit; put exception translation at complete operation boundaries; and freeze every public struct used as an array element at its V1 layout because `struct_size` alone cannot provide array stride compatibility.

**Tech Stack:** C11, MuPDF 1.28.2, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-design.md` plus its destination/action correction and the existing crop, trim, search, and AcroForm specifications.

## Global Constraints

- The public ABI remains MuPDF-free and existing exported function signatures remain unchanged.
- Source documents remain immutable; rewrite outputs remain independently owned.
- No MuPDF exception may escape an `quantapdf_*` public function.
- Encrypted and signed PDFs fail closed for rewrite/editor entry points.
- Array-element structs are fixed-layout V1 types; extensions require a new type/API or an explicit-stride API.
- Production changes require focused regression tests and the complete CTest suite.

---

### Task 1: Freeze array-element ABI contracts

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Modify: `src/search.c`
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_trim_preflight.c`
- Modify: `src/pdf_poster_preflight.c`
- Modify: `src/pdf_edit_form_values.c`
- Modify: `src/pdf_edit_form_buttons.c`
- Modify: `src/pdf_edit_form_choices.c`
- Test: relevant existing search, crop, trim, poster, and form-mutation tests

**Interfaces:**
- Consumes: existing `struct_size` fields.
- Produces: named `QUANTAPDF_*_V1_MIN_SIZE`/`QUANTAPDF_*_V1_SIZE`
  constants and bounded V1-size validation for structs traversed as C arrays.

- [x] Add public V1-size constants for page crop, page trim, poster split, search result, and form value input.
- [x] Add tests proving oversized array elements fail with `QUANTAPDF_ERROR_ARGUMENT` rather than being accepted as forward-compatible.
- [x] Bound array-element sizes to their V1 minimum and maximum sizes.
- [ ] Run the affected tests and confirm expected status/output reset behavior.

### Task 2: Centralize rewrite signature and encryption policy

**Files:**
- Create: `src/pdf_rewrite_security.h`
- Create: `src/pdf_rewrite_security.c`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_trim_preflight.c`
- Modify: `src/pdf_poster_preflight.c`
- Modify: `src/pdf_edit.c`
- Test: `tests/test_pdf_poster_split_policy.c` and raw fixture helpers

**Interfaces:**
- Produces: `quantapdf_pdf_rewrite_check_security(fz_context *, pdf_document *)`.
- Semantics: reject trailer `/Encrypt`, signed AcroForm fields, and catalog `/Perms` signature entries (`DocMDP`, `UR`, `UR3`); map malformed inspected state deterministically.

- [x] Add a deterministic PDF fixture with a catalog `/Perms/DocMDP` signature dictionary and no signed AcroForm field.
- [x] Add a poster-split test expecting `QUANTAPDF_ERROR_UNSUPPORTED` and a NULL output.
- [x] Implement the shared security check with one MuPDF exception boundary.
- [x] Replace four duplicated scanners with the shared helper.
- [ ] Run poster, crop, trim, and edit tests.

### Task 3: Contain every poster-split MuPDF exception

**Files:**
- Modify: `src/pdf_poster.c`
- Modify: `src/pdf_poster_annotations.c`
- Modify: `src/pdf_poster_navigation.c`
- Modify: `src/pdf_poster_widget_provenance.c`
- Test: `tests/test_pdf_poster_split_policy.c`

**Interfaces:**
- Produces: status-returning source/private preflight wrappers; no `fz_throw` reaches the ABI boundary.

- [x] Add a testing-only fault hook that throws during each previously unguarded preflight phase.
- [x] Add tests proving each fault returns a status, resets output, and leaves the source usable.
- [x] Wrap the full preflight sequence in `fz_try`/`fz_catch` and translate with `quantapdf_status_from_mupdf`.
- [x] Use `fz_always` or cleanup labels for mark-bit/heap resources owned inside potentially throwing helpers.
- [ ] Run the poster-split test target.

### Task 4: Refresh consumer documentation

**Files:**
- Modify: `README.md`
- Modify: affected design specifications under `docs/superpowers/specs/`

**Interfaces:**
- Produces: one accurate supported-feature list and explicit fixed-array versus append-only-single-struct rules.

- [x] Replace the obsolete “next Content phase” section with the implemented API families.
- [x] Document ownership for document-derived handles, immutable snapshots, outputs, borrowed strings/data, and allocated strings.
- [x] Correct forward-compatibility language for crop, trim, poster, search, and form-value arrays.
- [x] Check all referenced function/type names against the public header.

### Task 5: Verification

**Files:**
- Inspect: all modified files and `git diff`.

**Interfaces:**
- Consumes: configured CMake preset/toolchain available in the environment.
- Produces: fresh build and test evidence.

- [ ] Configure the project with the repository's MuPDF/vcpkg dependency path.
- [ ] Build with warnings-as-errors.
- [ ] Run focused poster, crop, trim, search, and form-mutation tests.
- [ ] Run the complete CTest suite with `--output-on-failure`.
- [x] Inspect `git diff --check` and confirm no unrelated files changed.
