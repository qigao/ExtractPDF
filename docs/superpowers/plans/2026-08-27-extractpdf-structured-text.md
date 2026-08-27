# ExtractPDF Structured Text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable, MuPDF-independent structured-text snapshot with indexed block/line/span traversal, stable geometry/style metadata, and internal character quads reserved for later search.

**Architecture:** `extractpdf_extract_structured_text()` asks MuPDF for one `fz_stext_page`, then projects only text blocks into ExtractPDF-owned flat arrays and UTF-8 storage. Public callers traverse opaque snapshot data through count/info/text accessors; no public pointer references MuPDF memory after extraction returns.

**Tech Stack:** C11, MuPDF 1.28.2 Fitz structured text API, CMake/CTest, Linux ASan/UBSan, GitHub Actions Linux-first CI.

**Spec:** `docs/superpowers/specs/2026-08-27-extractpdf-structured-text-design.md`

## Global Constraints

- No MuPDF types or numeric MuPDF flags in `include/extractpdf/extractpdf.h`.
- Structured-text coordinates use the existing Fitz page-space contract: CropBox top-left origin, points, Y down.
- Snapshot lifetime is independent of `extractpdf_page` and `extractpdf_document` after successful extraction.
- V1 exposes text blocks only; image/vector/grid/structure blocks are skipped.
- V1 extraction uses MuPDF structured-text default options (`flags = 0`).
- Span style identity is font identity + size + ARGB + bidi + internal character flags.
- Public `*_info` structs are versioned with `struct_size` and append-only.
- Linux normal CTest + ASan/UBSan is required for each RED/GREEN slice; Windows/macOS wait for the Phase 3 `full-ci` checkpoint.
- Search APIs are out of scope, but internal character Unicode/quads must be retained for later reuse.

---

### Task 1: Lock Snapshot Traversal Contract With a Real RED

**Files:**
- Create: `tests/fixtures/structured-text.pdf`
- Create: `tests/test_structured_text.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `extractpdf_document`, `extractpdf_page`, `extractpdf_rect`, and page lifecycle.
- Produces the required public names for Task 2: `extractpdf_text_page`, `extractpdf_text_block_info`, `extractpdf_text_line_info`, `extractpdf_text_span_info`, `extractpdf_extract_structured_text`, block/line/span count/info accessors, `extractpdf_text_span_text`, and `extractpdf_drop_text_page`.

- [ ] **Step 1: Add a deterministic valid PDF fixture**

Create one page containing a single line with two adjacent style runs: `Hello ` in Helvetica 18pt black and `Caf\351` in Helvetica 12pt red using WinAnsi encoding. Author exact stream length and xref offsets; do not rely on MuPDF repair mode.

- [ ] **Step 2: Add the first contract test**

The test must assert successful extraction, exactly one text block, one line, two spans, exact UTF-8 text (`Hello ` and `Caf\xC3\xA9`), 18/12 pt span sizes, black/red ARGB, horizontal writing mode, LTR bidi, finite/ordered geometry, and snapshot validity after dropping page/document.

- [ ] **Step 3: Wire the test into CTest**

Add `extractpdf.structured_text` with a 30-second timeout and the fixture path compile definition. Include the target in Windows DLL staging even though Windows will remain skipped during Linux-first development.

- [ ] **Step 4: Verify RED**

Run the PR Linux workflow. Expected failure: compile/link errors only because the structured-text public types/functions do not exist. Any PDF repair warning, fixture parse failure, or unrelated syntax error must be fixed before production code is written.

- [ ] **Step 5: Commit the RED**

Commit only the fixture/test/CMake wiring with message `test: define structured text snapshot contract`.

---

### Task 2: Implement Immutable Snapshot and Style Spans

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Create: `src/structured_text.c`
- Modify: `src/internal.h`
- Modify: `CMakeLists.txt`
- Test: `tests/test_structured_text.c`

**Interfaces:**
- Consumes: MuPDF `fz_new_stext_page_from_page`, `fz_stext_page` text blocks/lines/chars, existing status mapping.
- Produces: the full V1 structured-text API from the design spec.

- [ ] **Step 1: Add only the public declarations demanded by the RED**

Add `<stdint.h>`, opaque `extractpdf_text_page`, and the three versioned info structs. Add the extraction, count/info/text, and drop declarations exactly as specified.

- [ ] **Step 2: Define focused internal storage**

Use flat ExtractPDF-owned arrays for blocks, lines, spans, and chars. Store array ranges by index/count, not linked pointers. Store UTF-8 span strings in snapshot-owned allocations or one snapshot-owned string arena. Internal chars retain Unicode code point plus four-point quad even though no V1 character accessor exists.

- [ ] **Step 3: Project MuPDF text blocks into the snapshot**

Create an `fz_stext_page` with default options inside `fz_try/fz_catch`. Count text blocks/lines/chars, allocate with overflow checks, then copy block and line metadata. Skip non-text blocks.

- [ ] **Step 4: Build spans while copying characters**

Start a new span whenever font pointer, size, ARGB, bidi, or character flags changes. Append each Unicode code point as UTF-8. Span bounds are the axis-aligned union of character quads. Preserve the character Unicode/quad/style association internally for later search reuse.

- [ ] **Step 5: Implement indexed accessors**

Count accessors return `size_t`. Info accessors validate indices and `struct_size`, preserve the input `struct_size`, and fill the V1 fields. `extractpdf_text_span_text` returns a borrowed NUL-terminated snapshot-owned pointer with size excluding NUL.

- [ ] **Step 6: Implement cleanup and exception unwinding**

`extractpdf_drop_text_page(NULL)` is safe. Any allocation failure or caught MuPDF exception drops the upstream structured text and all partial ExtractPDF allocations. After successful return, the snapshot contains no pointer into MuPDF memory.

- [ ] **Step 7: Verify GREEN**

Run Linux build, full CTest, sanitizer build, and sanitizer CTest. Expected: `extractpdf.structured_text` and all existing tests pass with no sanitizer findings.

- [ ] **Step 8: Commit**

Commit with message `feat: add immutable structured text snapshot`.

---

### Task 3: Harden Error, Empty-Page, and Versioned-Struct Behavior

**Files:**
- Modify: `tests/test_structured_text.c`
- Modify only if required by RED: `src/structured_text.c`

**Interfaces:**
- Consumes: Task 2 public API.
- Produces: locked failure/reset/lifetime semantics suitable for reuse by later search code.

- [ ] **Step 1: Add a second RED for failure semantics**

Test NULL snapshot/output arguments, out-of-range block/line/span indices, undersized `struct_size`, output resets (`count=0`, text pointer `NULL`, text size `0`), and `extractpdf_drop_text_page(NULL)`.

- [ ] **Step 2: Add empty-page snapshot coverage**

Extract from the existing blank one-page fixture; require success with a non-NULL snapshot and zero text blocks.

- [ ] **Step 3: Add V1 struct-size compatibility coverage**

For every info struct, initialize `struct_size` to exactly the V1 minimum, fill the remainder with sentinel bytes, call the accessor, and prove only fields covered by the supplied size are written. Future appended fields must therefore remain safe.

- [ ] **Step 4: Verify the new test fails for any missing behavior**

If all behavior already passes from Task 2, introduce no production change merely to force a failure; instead isolate the first unimplemented reset/versioning case before proceeding. The TDD requirement is that every production correction has a demonstrated failing assertion first.

- [ ] **Step 5: Make only the minimal corrections required by the RED**

Do not add search, font names, image blocks, options, or public character accessors.

- [ ] **Step 6: Run final Linux proof**

Require build + all CTest + ASan/UBSan build + sanitizer CTest on the exact head.

- [ ] **Step 7: Update tracking records**

Update Issue #9 and the stacked PR body with RED/GREEN workflow IDs and exact head. Do not run Windows/macOS yet; defer them to the Phase 3 Content checkpoint.
