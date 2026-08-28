# ExtractPDF PDF Page Annotation Enumeration V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PDF-only immutable page annotation snapshot with tolerant `/Annots` collection semantics, strict surviving-item materialization, deterministic order, snapshot-local identity, independent lifetime, and atomic failure publication.

**Architecture:** Down-cast the existing loaded `fz_page` to `pdf_page` and walk the page object's `/Annots` array read-only. Filter non-dictionaries and Link/Popup/Widget entries without invoking MuPDF annotation synchronization/resynthesis; strictly validate and copy the surviving annotation's Rect/F/Contents into an ExtractPDF-owned array/string arena, then publish only after the complete snapshot succeeds.

**Tech Stack:** C11, MuPDF 1.28.2 pinned through existing vcpkg overlay, CMake/CTest, Linux ASan/UBSan, macOS CI, Windows DLL CI, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotations-design.md`

## Global Constraints

- Base is integrated outline master exact SHA `a83639752e629225b34b052d537a5d2e61220711`, verified by push workflow #175 (`33176336925`).
- Work on `feat/pdf-annotations`; child issue is #35; umbrella is #2.
- PDF-only V1; non-PDF page -> `EXTRACTPDF_ERROR_UNSUPPORTED`.
- Reuse existing `extractpdf_page`; no second public PDF page handle.
- Immutable `extractpdf_annotation_page` snapshot owns all public item/string data and retains no MuPDF/PDF/page/document pointer.
- Collection tolerance: missing/non-array `/Annots` -> empty; non-dictionary entries ignored; Link/Popup/Widget ignored; absent/non-name/unrecognized subtype -> UNKNOWN.
- Surviving item strictness: Rect exactly four finite numbers; present F is uint32-representable integer; present Contents is string; malformed survivor -> FORMAT.
- Preserve original `/Annots` relative order among survivors exactly.
- Snapshot indices are local coordinates only, never persistent annotation identity or mutation selectors.
- Empty result -> `OK + non-NULL count-0 snapshot`.
- Extraction resets `*out_annotations = NULL` before validation/fallible work and publishes only after complete success.
- Do not call `pdf_sync_annots`, `pdf_load_annots`, update/resynthesis, or mutation APIs for enumeration.
- RED must contain no production annotation ABI or implementation.
- Exact GREEN head requires Linux static/all CTests, Linux ASan/UBSan/all CTests, macOS build/test, and Windows DLL build/test.

---

## File Structure

**Create during RED**
- `tests/fixtures/annotations-mixed.pdf`
- `tests/fixtures/annotations-nonarray.pdf`
- `tests/fixtures/annotations-filtered-only.pdf`
- `tests/fixtures/annotations-late-malformed.pdf`
- `tests/test_pdf_annotations.c`

**Modify during RED**
- `tests/CMakeLists.txt`

**Create during GREEN**
- `src/pdf_annotations.c`

**Modify during GREEN**
- `include/extractpdf/extractpdf.h`
- `CMakeLists.txt`

**Reuse unchanged**
- `src/internal.h`
- `src/page.c`
- existing valid no-annotation PDF fixture
- `.github/workflows/ci.yml`

---

### Task 1: Strict RED for annotation snapshot contract

**Files:**
- Create four deterministic annotation fixtures listed above.
- Create `tests/test_pdf_annotations.c`.
- Modify `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes existing `extractpdf_open`, `extractpdf_load_page`, `extractpdf_drop_page`, `extractpdf_close`, status enum, `extractpdf_rect`.
- Produces compile-time references to the not-yet-existing `extractpdf_annotation_page`, `extractpdf_annotation_type`, `extractpdf_annotation_info`, `extractpdf_extract_annotations`, `extractpdf_annotation_count`, `extractpdf_annotation_get_info`, `extractpdf_annotation_contents`, `extractpdf_drop_annotation_page`.

- [ ] **Step 1: Check in deterministic fixtures**

Use a one-page `%PDF-1.4` direct-object fixture writer with explicit xref offsets. Keep annotation objects indirect except for the deliberate scalar entries. All pages use `/MediaBox [0 0 200 200]`.

`annotations-mixed.pdf` page `/Annots`:

```pdf
[5 0 R 17 6 0 R 7 0 R 8 0 R 9 0 R 10 0 R]
```

Objects:

```pdf
5 0 obj << /Type /Annot /Subtype /Text /Rect [10 20 30 40] /F 4 /Contents (alpha) >> endobj
6 0 obj << /Type /Annot /Subtype /Link /Rect [1 1 2 2] /A << /S /URI /URI (https://example.com) >> >> endobj
7 0 obj << /Type /Annot /Subtype /FutureThing /Rect [50 60 70 80] /F 64 /Contents (unknown) >> endobj
8 0 obj << /Type /Annot /Subtype /Widget /Rect [2 2 3 3] /FT /Tx >> endobj
9 0 obj << /Type /Annot /Subtype /Popup /Rect [3 3 4 4] >> endobj
10 0 obj << /Type /Annot /Subtype /Highlight /Rect [90 100 120 130] /Contents (bravo) >> endobj
```

`annotations-nonarray.pdf`: page has `/Annots 17`.

`annotations-filtered-only.pdf`: page `/Annots [17 5 0 R 6 0 R 7 0 R]`, where objects 5/6/7 are Link/Widget/Popup.

`annotations-late-malformed.pdf`: page `/Annots [5 0 R 6 0 R]`; object 5 is valid Text with `/Contents (first)`, object 6 is Highlight with valid Rect but `/Contents 123`.

- [ ] **Step 2: Write the failing C test**

The test must assert:

```c
/* mixed tolerance + exact surviving order */
CHECK(extractpdf_extract_annotations(page, &annotations) == EXTRACTPDF_OK);
CHECK(extractpdf_annotation_count(annotations, &count) == EXTRACTPDF_OK);
CHECK(count == 3);
expect_info(annotations, 0, EXTRACTPDF_ANNOTATION_TEXT, 10, 160, 30, 180, 4);
expect_contents(annotations, 0, "alpha");
expect_info(annotations, 1, EXTRACTPDF_ANNOTATION_UNKNOWN, 50, 120, 70, 140, 64);
expect_contents(annotations, 1, "unknown");
expect_info(annotations, 2, EXTRACTPDF_ANNOTATION_HIGHLIGHT, 90, 70, 120, 100, 0);
expect_contents(annotations, 2, "bravo");
```

The y values above prove PDF user-space -> Fitz page-space conversion for a 200-point unrotated page: `[x0 y0 x1 y1]` maps to `[x0 200-y1 x1 200-y0]`.

Also assert:

```c
/* lifetime */
extractpdf_drop_page(page);
extractpdf_close(document);
/* all count/info/contents accessors still work */

/* empty variants */
/* no /Annots, non-array /Annots, filtered-only -> OK + non-NULL + count 0 */

/* atomicity */
annotations = sentinel;
CHECK(extractpdf_extract_annotations(late_malformed_page, &annotations) == EXTRACTPDF_ERROR_FORMAT);
CHECK(annotations == NULL);
annotations = sentinel;
CHECK(extractpdf_extract_annotations(late_malformed_page, &annotations) == EXTRACTPDF_ERROR_FORMAT);
CHECK(annotations == NULL);

/* argument/reset */
/* NULL extraction args; count reset; get_info neutral reset; contents NULL/0 reset; invalid index; drop(NULL) */
```

Create two independent snapshots of `annotations-mixed.pdf` and assert equal values/order only; do not expose or compare any public object identity.

- [ ] **Step 3: Register only the new RED test target**

Append to `tests/CMakeLists.txt` following the existing PDF test pattern:

```cmake
add_executable(extractpdf_test_pdf_annotations test_pdf_annotations.c)
target_link_libraries(extractpdf_test_pdf_annotations PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_annotations PRIVATE
  ANNOTATIONS_MIXED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-mixed.pdf"
  ANNOTATIONS_NONARRAY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-nonarray.pdf"
  ANNOTATIONS_FILTERED_ONLY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-filtered-only.pdf"
  ANNOTATIONS_LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-late-malformed.pdf")
add_test(NAME extractpdf.pdf_annotations COMMAND extractpdf_test_pdf_annotations)
```

Reuse the exact existing no-annotation fixture macro/pattern already present in the test CMake file rather than introducing a duplicate PDF.

- [ ] **Step 4: Verify RED**

Run the repository's normal Linux configure/build path.

Expected boundary:

```text
ExtractPDF library builds                             PASS
all pre-existing test targets through pdf_outline    PASS build/link
extractpdf_test_pdf_annotations                      FAIL compile
```

The new target must fail specifically on absent annotation type/struct/API declarations. If a fixture parse error, unrelated target, runtime test, crash, or timeout is reached, RED is invalid and must be fixed before continuing.

- [ ] **Step 5: Commit RED**

```bash
git add tests/fixtures/annotations-*.pdf tests/test_pdf_annotations.c tests/CMakeLists.txt
git commit -m "test: lock PDF annotation snapshot contract"
```

Record exact RED SHA and workflow/run evidence in issue #35 / later PR body.

---

### Task 2: Minimal public ABI and immutable implementation

**Files:**
- Modify `include/extractpdf/extractpdf.h`.
- Create `src/pdf_annotations.c`.
- Modify `CMakeLists.txt`.
- Test unchanged: `tests/test_pdf_annotations.c`.

**Interfaces:**
- Produces exactly the public API in the design spec.
- Uses `pdf_page_from_fz_page`, raw `pdf_page->obj`, `pdf_dict_get`, `pdf_array_len/get`, PDF object type predicates/converters, `pdf_page_obj_transform`, Fitz matrix inversion/rectangle transform, existing `extractpdf_status_from_mupdf`.

- [ ] **Step 1: Add the public declarations only**

Add the opaque handle, stable enum, info struct, extraction/count/info/contents/drop declarations from the spec. Do not expose MuPDF enums or PDF object identity.

- [ ] **Step 2: Add private snapshot records**

`src/pdf_annotations.c` owns:

```c
typedef struct extractpdf_annotation_internal {
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t contents_offset;
    size_t contents_size;
    int has_contents;
} extractpdf_annotation_internal;

struct extractpdf_annotation_page {
    extractpdf_annotation_internal *items;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};
```

No pointer back to page/document/PDF/MuPDF is allowed.

- [ ] **Step 3: Implement tolerant survivor classification**

For each raw `/Annots` element in index order:

```text
not dictionary -> skip
Subtype Link    -> skip
Subtype Popup   -> skip
Subtype Widget  -> skip
otherwise       -> survivor
```

Map known subtype names explicitly to the stable ExtractPDF enum; missing/non-name/other name -> UNKNOWN.

Do not call `pdf_sync_annots` or any annotation resynthesis/update function.

- [ ] **Step 4: Implement strict survivor validation/materialization**

For each survivor:

1. Validate `/Rect` is an array of length 4 and every member is numeric + finite.
2. Obtain the page transform; invert it to map PDF-user-space rectangle into Fitz page space; normalize rectangle endpoints.
3. If `/F` absent use zero; if present require integer in `[0, UINT32_MAX]`.
4. If `/Contents` absent mark `has_contents = 0`; if present require string, decode PDF text to UTF-8, deep-copy with trailing NUL into the snapshot string arena.
5. Check every count/allocation/string-size multiplication/addition for `SIZE_MAX` overflow; return NOMEM on overflow/allocation failure.

Any FORMAT/NOMEM/MuPDF failure disposes the whole private snapshot and returns without publication.

- [ ] **Step 5: Implement accessors and atomic publication**

Extraction begins:

```c
if (out_annotations == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
*out_annotations = NULL;
```

Only after complete success:

```c
*out_annotations = snapshot;
return EXTRACTPDF_OK;
```

`count` resets `*out_count = 0` when possible before validation.

`get_info` validates `struct_size`, resets type/bounds/flags, then validates handle/index and copies known fields.

`contents` resets pointer/size to NULL/0, returns `OK + NULL + 0` for absent contents, and borrowed snapshot-owned UTF-8 for present contents.

`drop(NULL)` is a no-op.

- [ ] **Step 6: Wire the implementation into the root library**

Add only `src/pdf_annotations.c` to `add_library(extractpdf ...)`.

- [ ] **Step 7: Verify GREEN locally/CI**

Run Linux strict static + all CTests and Linux sanitizer build + all CTests. Expected: all tests including `extractpdf.pdf_annotations` pass with no sanitizer failures.

- [ ] **Step 8: Commit GREEN**

```bash
git add include/extractpdf/extractpdf.h src/pdf_annotations.c CMakeLists.txt
git commit -m "feat: expose immutable PDF annotation snapshot"
```

---

### Task 3: Exact-head cross-platform proof and review

**Files:** no intended production changes.

- [ ] **Step 1: Confirm exact diff scope**

Diff from integrated base must contain only:

```text
docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotations-design.md
docs/superpowers/plans/2026-08-28-extractpdf-pdf-annotations.md
tests/fixtures/annotations-mixed.pdf
tests/fixtures/annotations-nonarray.pdf
tests/fixtures/annotations-filtered-only.pdf
tests/fixtures/annotations-late-malformed.pdf
tests/test_pdf_annotations.c
tests/CMakeLists.txt
include/extractpdf/extractpdf.h
src/pdf_annotations.c
CMakeLists.txt
```

No page/link/outline/metadata/composition/output implementation edits are expected.

- [ ] **Step 2: Run same-SHA full-ci**

Require on one unchanged GREEN SHA:

```text
Linux static + all CTests       PASS
Linux ASan/UBSan + all CTests   PASS
macOS configure/build/test      PASS
Windows DLL configure/build/test PASS
```

Windows must execute `extractpdf.pdf_annotations`.

- [ ] **Step 3: Fresh review against the five locked boundaries**

Review explicitly for:

```text
malformed/tolerance
empty snapshot
order
identity
error atomicity
```

Reject any implementation that sorts survivors, exposes object identity, returns NULL on successful empty extraction, silently defaults malformed surviving public fields, retains page/document pointers, or publishes a partial snapshot.

- [ ] **Step 4: Update issue #35 / PR evidence**

Record RED SHA/run, GREEN SHA/run, full-ci run, exact scope, and review disposition. Keep mutation/forms explicitly deferred.
