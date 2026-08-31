# QuantaPDF Immutable CropBox Transform V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable, batch, shrink-only CropBox transform that preserves the complete PDF graph while establishing the page-space mapping contract required by later Phase 6 transforms.

**Architecture:** Keep the source `quantapdf_document` immutable. Strictly preflight all requests against the source PDF, serialize the complete source, reopen it in a fresh private MuPDF context with JavaScript disabled, write only page-local `/CropBox` entries for changed requests, deterministically serialize the private document, and publish an independent `quantapdf_output`. Do not use page grafting, content translation, session-local editor refs, page-wide form update, or interactive runtime execution.

**Tech Stack:** C11, MuPDF 1.28.2 through the repository's pinned vcpkg overlay, CMake/CTest, Linux ASan+UBSan, GitHub Actions Linux/macOS/Windows DLL proof.

**Spec:** `docs/superpowers/specs/2026-08-29-quantapdf-cropbox-transform-design.md`

## Global Constraints

- Baseline branch point is `master` commit `10aace7bae934f48f0fbcdefad5a9bb42518293d`; its content tree is `ef426ab30f07242806d95da98940021237d6d4f8`.
- Work only on `feat/cropbox-transform` until explicit integration authorization.
- Current suite baseline is 21 CTests; this feature adds exactly one new CTest named `quantapdf.pdf_crop`, yielding 22 CTests.
- Public input rectangles are in current source-page Fitz page space, never raw PDF user space.
- V1 is shrink-only relative to the source page's effective visible region: effective `/CropBox` intersected with effective `/MediaBox`, with `/MediaBox` fallback when CropBox is absent.
- `/MediaBox`, `/BleedBox`, `/TrimBox`, `/ArtBox`, `/Rotate`, `/UserUnit`, `/Contents`, `/Resources`, `/Annots`, `/AcroForm`, `/Outlines`, `/Names`, and destination objects are never rewritten by this feature.
- The only semantic PDF write is a page-local `/CropBox` on changed pages.
- Objects outside the new visible region remain structurally present; crop is visibility/clipping semantics, not deletion.
- MuPDF 1.28.2 `pdf_page_obj_transform()` matrix is treated as public Fitz page space -> raw PDF user space. Use it directly to map requested public crop corners to PDF coordinates; use its inverse only when deriving public observations from raw PDF boxes.
- `/MediaBox`, `/CropBox`, and `/Rotate` follow page-tree inheritance. `/UserUnit` is page-local for this design and must not be searched through page-tree parents.
- Raw CropBox extending beyond MediaBox is not automatically malformed. The effective visible raw box is the normalized intersection of effective CropBox and effective MediaBox and must have positive area.
- Missing BleedBox/TrimBox/ArtBox entries remain missing; their PDF default behavior may consequently follow the new CropBox. The transform must not materialize those entries.
- Encrypted input and already-signed input are `QUANTAPDF_ERROR_UNSUPPORTED`.
- No JavaScript, form event, validation, formatting, calculation, activation action, annotation update, Widget appearance regeneration, or page-wide form recalculation may run.
- No MuPDF type crosses the public ABI; no MuPDF exception crosses the C ABI.
- `out_output` resets to `NULL` on every failure path.
- An all-no-op batch still returns a canonical immutable output equal to the repository's deterministic full serialization of the unchanged source; repeated calls on the same source are byte-identical, but need not match original input-file bytes.
- Do not edit `.github/workflows/ci.yml`; use the existing PR workflow and `full-ci` label gate.
- Do not mark the PR ready, merge it, close #49, or mark #48/#2 complete before explicit integration authorization and integrated-master proof.

## File Structure

The implementation should remain crop-specific instead of creating a generic transform framework before a second transform proves the abstraction.

- `include/quantapdf/quantapdf.h` — public request type and `quantapdf_crop_pages()` declaration only.
- `src/pdf_crop_internal.h` — crop-private resolved-page/request-plan structs and strict-preflight helper declarations; no public types beyond consuming `quantapdf_page_crop`.
- `src/pdf_crop_preflight.c` — strict page-tree/page-box resolution, source visible-region calculation, public->PDF rectangle mapping, duplicate/range/finite/shrink-only validation, and crop-local encrypted/signed fail-closed checks. No writes.
- `src/pdf_crop.c` — public API orchestration, source canonical serialization, private-context reopen, private re-resolution, raw `/CropBox` writes, deterministic serialization, and cleanup.
- `CMakeLists.txt` — add `src/pdf_crop.c` and `src/pdf_crop_preflight.c` to the library; no workflow changes.
- `tests/test_pdf_crop.c` — public-contract test driver: reset/error behavior, no-op/determinism, geometry mapping, source immutability, output lifetime, multi-page batching, and existing-read-surface preservation.
- `tests/test_pdf_crop_raw.c` — test-only MuPDF parser for structural assertions about local `/CropBox` presence/absence and untouched dictionaries. It is compiled into the same `quantapdf_test_pdf_crop` executable.
- `tests/test_pdf_crop_internal.h` — declarations shared by the two crop test sources.
- `tests/fixtures/crop-interactive.pdf` — deterministic two-page interactive fixture containing text, an image occurrence, URI link, internal link, ordinary annotation, Widget/AcroForm value, outline destination, and at least one object that will fall partly/fully outside the crop.
- `tests/fixtures/crop-inherited.pdf` — deterministic page-tree fixture with inherited MediaBox/CropBox/Rotate and no page-local CropBox on the target page.
- `tests/fixtures/crop-rotate-90.pdf` — deterministic `/Rotate 90` fixture.
- `tests/fixtures/crop-userunit.pdf` — deterministic page-local non-default `/UserUnit` fixture.
- `tests/fixtures/crop-cropbox-outside-media.pdf` — valid fixture whose raw CropBox extends outside MediaBox but has a non-empty effective intersection.
- `tests/fixtures/crop-malformed-box.pdf` — malformed four-value box environment for `FORMAT` coverage.
- `tests/fixtures/crop-malformed-rotate.pdf` — invalid Rotate representation for `FORMAT` coverage.
- `tests/fixtures/crop-malformed-userunit.pdf` — invalid page-local UserUnit for `FORMAT` coverage.
- Reuse `tests/fixtures/annotation-mutation-signed.pdf`, `tests/fixtures/encrypted-one-page.pdf`, and `tests/fixtures/composition-non-pdf.txt` for signed/encrypted/non-PDF policy coverage.

---

### Task 1: Lock the strict compile RED and open the draft PR

**Files:**
- Create: `tests/test_pdf_crop.c`
- Create: `tests/fixtures/crop-interactive.pdf`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing public `quantapdf_document`, `quantapdf_page`, `quantapdf_output`, `quantapdf_rect`, open/page/output APIs.
- Produces: a test-only dependency on the approved but still-absent public symbols `quantapdf_page_crop` and `quantapdf_crop_pages()`; draft PR tracking for #49.

- [ ] **Step 1: Add the deterministic primary fixture**

Create `tests/fixtures/crop-interactive.pdf` as a hand-authored deterministic two-page PDF. Lock these semantic objects so later tests can compare source vs transformed observations instead of depending on object numbers:

```text
Catalog
  /Pages -> two pages
  /Outlines -> one item targeting page 2 with /XYZ destination
  /AcroForm -> one Text field with one Widget on page 1, value (CROP-VALUE)

Page 1
  MediaBox [0 0 400 300]
  CropBox  [0 0 400 300]
  Contents: text literal CROP-TEXT plus one image XObject invocation
  Annots:
    - URI Link to https://example.com/crop
    - internal Link to page 2 /XYZ
    - Square annotation with /Contents (CROP-ANNOT)
    - Text-field Widget owned by the AcroForm field

Page 2
  MediaBox [0 0 400 300]
  CropBox  [0 0 400 300]
  Contents: text literal CROP-TARGET
```

Place the Square annotation so its public source bounds cross the future left/top crop boundary; place the Widget, image, and URI link fully inside. Use ordinary unrotated pages in this first fixture.

- [ ] **Step 2: Add the compile-RED test driver**

Create `tests/test_pdf_crop.c` with a minimal public call that cannot compile until the approved ABI exists:

```c
#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = output_sentinel();
    quantapdf_page_crop crop;

    if (quantapdf_open(CROP_INTERACTIVE_PDF, NULL, &document) != QUANTAPDF_OK)
        return 1;

    crop.struct_size = sizeof(crop);
    crop.page_index = 0;
    crop.bounds.x0 = 50.0f;
    crop.bounds.y0 = 40.0f;
    crop.bounds.x1 = 350.0f;
    crop.bounds.y1 = 260.0f;

    if (quantapdf_crop_pages(document, &crop, 1, &output) != QUANTAPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid crop failed\n");
        quantapdf_close(document);
        return 1;
    }

    quantapdf_drop_output(output);
    quantapdf_close(document);
    return 0;
}
```

- [ ] **Step 3: Register exactly one new CTest**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(quantapdf_test_pdf_crop test_pdf_crop.c)
target_link_libraries(quantapdf_test_pdf_crop PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_crop PRIVATE
  CROP_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-interactive.pdf")
add_test(NAME quantapdf.pdf_crop COMMAND quantapdf_test_pdf_crop)
set_tests_properties(quantapdf.pdf_crop PROPERTIES TIMEOUT 60)
```

Do not modify root `CMakeLists.txt`, public headers, `src/`, or workflows in this task.

- [ ] **Step 4: Run the build and prove attributable compile RED**

After the repository is configured with the existing Linux vcpkg command from `.github/workflows/ci.yml`, run:

```bash
cmake --build build --parallel 2
```

Expected: the `quantapdf` library and every existing test executable #1-#21 build; only `quantapdf_test_pdf_crop` fails because `quantapdf_page_crop` and/or `quantapdf_crop_pages` do not exist. There must be no fixture parse/runtime failure yet because the new target must not link.

- [ ] **Step 5: Commit the tests-only RED**

```bash
git add tests/CMakeLists.txt tests/test_pdf_crop.c tests/fixtures/crop-interactive.pdf
git commit -m "test: lock immutable CropBox transform contract"
```

- [ ] **Step 6: Open the canonical draft PR and collect Linux RED evidence**

Create a **draft** PR from `feat/cropbox-transform` to `master` titled:

```text
feat: add immutable CropBox page transform
```

The PR body must link #49, #48, and the design spec. Keep it draft for the entire implementation/review phase.

The PR workflow must reproduce the same attributable RED: existing library/#1-#21 targets build, only the new crop target fails on the absent approved ABI. Record the workflow run ID in #49/PR comments.

**Task gate:** do not proceed if any old target fails or the RED is caused by fixture/CMake mistakes instead of the missing ABI.

---

### Task 2: Add the public ABI and a runtime-RED shell

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_crop.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: public `quantapdf_rect`, `quantapdf_document`, `quantapdf_output`.
- Produces:

```c
typedef struct quantapdf_page_crop {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_crop;

QUANTAPDF_API quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output);
```

- [ ] **Step 1: Add the exact approved public declarations**

Place `quantapdf_page_crop` near the existing geometry/output public structures and add `quantapdf_crop_pages()` alongside the immutable composition/output functions. Do not add transform enums/options or MuPDF-facing types.

- [ ] **Step 2: Add the smallest implementation shell**

Create `src/pdf_crop.c`:

```c
#include "pdf_internal.h"

quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || crops == NULL || crop_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    return QUANTAPDF_ERROR_UNSUPPORTED;
}
```

- [ ] **Step 3: Add only `src/pdf_crop.c` to the library source list**

In root `CMakeLists.txt`, add:

```cmake
src/pdf_crop.c
```

Do not add preflight helpers yet.

- [ ] **Step 4: Build and run tests to prove compile GREEN / runtime RED**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: all 22 executables build; existing #1-#21 pass; only `quantapdf.pdf_crop` fails with `valid crop failed` because the shell returns `QUANTAPDF_ERROR_UNSUPPORTED`.

- [ ] **Step 5: Commit the ABI shell**

```bash
git add include/quantapdf/quantapdf.h src/pdf_crop.c CMakeLists.txt
git commit -m "feat: add CropBox transform ABI shell"
```

**Task gate:** review the public ABI before adding semantic implementation. No additional public options may appear without returning to the design gate.

---

### Task 3: Implement strict preflight, error reset, security policy, and canonical no-op output

**Files:**
- Create: `src/pdf_crop_internal.h`
- Create: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_crop.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_pdf_crop.c`
- Create: `tests/fixtures/crop-malformed-box.pdf`
- Create: `tests/fixtures/crop-malformed-rotate.pdf`
- Create: `tests/fixtures/crop-malformed-userunit.pdf`
- Modify: `tests/CMakeLists.txt`
- Reuse: `tests/fixtures/annotation-mutation-signed.pdf`
- Reuse: `tests/fixtures/encrypted-one-page.pdf`
- Reuse: `tests/fixtures/composition-non-pdf.txt`

**Interfaces:**
- Produces crop-private types:

```c
typedef struct quantapdf_pdf_crop_page_view {
    pdf_obj *page_obj;              /* borrowed from the current document */
    fz_rect media_pdf;              /* normalized effective MediaBox */
    fz_rect crop_pdf;               /* normalized raw effective CropBox/fallback */
    fz_rect visible_pdf;            /* non-empty intersection(media_pdf, crop_pdf) */
    fz_matrix public_to_pdf;        /* MuPDF page transform: Fitz -> PDF */
    quantapdf_rect visible_public; /* effective visible region in public space */
    int rotate_degrees;
    float user_unit;
} quantapdf_pdf_crop_page_view;

typedef struct quantapdf_pdf_crop_plan {
    int page_index;
    quantapdf_rect requested_public;
    fz_rect requested_pdf;
    int changed;
} quantapdf_pdf_crop_plan;

quantapdf_status quantapdf_pdf_crop_resolve_page(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    quantapdf_pdf_crop_page_view *out_view);

quantapdf_status quantapdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_pdf_crop_plan *plans,
    int *out_any_changed);
```

`pdf_obj *page_obj` is context-local and borrowed; it must never be copied into a plan or across contexts.

- [ ] **Step 1: Expand tests so validation/no-op precede the still-unimplemented changed crop**

Add helpers modeled on existing composition tests:

```c
static int expect_crop_error(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t count,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();
    quantapdf_status status =
        quantapdf_crop_pages(document, crops, count, &output);

    if (status != expected || output != NULL) {
        if (output != NULL && output != output_sentinel())
            quantapdf_drop_output(output);
        return 0;
    }
    return 1;
}
```

Test, in this order, before the changed crop assertion:

```text
NULL out_output -> ARGUMENT
NULL document -> ARGUMENT and output reset
NULL crops -> ARGUMENT and output reset
crop_count == 0 -> ARGUMENT and output reset
short struct_size -> ARGUMENT
negative/high page index -> ARGUMENT
duplicate page index -> ARGUMENT
NaN / +Inf / -Inf -> ARGUMENT
zero-width / zero-height / inverted -> ARGUMENT
request outside current visible region -> ARGUMENT
non-PDF -> UNSUPPORTED
encrypted PDF -> UNSUPPORTED
already-signed PDF -> UNSUPPORTED
malformed box -> FORMAT
malformed Rotate -> FORMAT
malformed UserUnit -> FORMAT
all-no-op request -> OK with non-NULL deterministic output
repeat all-no-op request -> byte-identical output
source page bounds still unchanged
```

After those checks, retain the existing changed crop call; it should remain the only RED after this task.

- [ ] **Step 2: Add malformed fixtures with one fault each**

Create three minimal static PDFs:

```text
crop-malformed-box.pdf
  valid Catalog/Pages/Page skeleton
  page /MediaBox contains a non-number as one of the four entries

crop-malformed-rotate.pdf
  valid MediaBox
  page-tree /Rotate 45

crop-malformed-userunit.pdf
  valid MediaBox
  page-local /UserUnit 0
```

Do not combine faults in one fixture; each expected status must be attributable.

- [ ] **Step 3: Register fixture paths without adding another CTest**

Extend `target_compile_definitions(quantapdf_test_pdf_crop ...)` with:

```cmake
CROP_MALFORMED_BOX_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-box.pdf"
CROP_MALFORMED_ROTATE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-rotate.pdf"
CROP_MALFORMED_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-userunit.pdf"
SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
```

- [ ] **Step 4: Implement strict crop-private page resolution**

Create `src/pdf_crop_preflight.c`. The resolver must walk `/Parent` itself with a 256-level bound and cycle detection, resolving nearest `/MediaBox`, `/CropBox`, and `/Rotate`. It must read `/UserUnit` only from the page object.

Normalize each four-number box to `min/max` coordinates. Compute:

```c
visible_pdf.x0 = fmaxf(media_pdf.x0, crop_pdf.x0);
visible_pdf.y0 = fmaxf(media_pdf.y0, crop_pdf.y0);
visible_pdf.x1 = fminf(media_pdf.x1, crop_pdf.x1);
visible_pdf.y1 = fminf(media_pdf.y1, crop_pdf.y1);
```

If the intersection has non-positive width/height, return `QUANTAPDF_ERROR_FORMAT`.

Validate Rotate as an integer multiple of 90. Validate page-local UserUnit as finite and `> 0`; absence means `1.0f`.

Call:

```c
pdf_page_obj_transform(ctx, page_obj, NULL, &view.public_to_pdf);
```

Treat the returned matrix as Fitz/public -> PDF. Derive `visible_public` by transforming `visible_pdf` through `fz_invert_matrix(view.public_to_pdf)`, transforming all four rectangle corners and normalizing the result.

- [ ] **Step 5: Implement request planning and public->PDF crop mapping**

For each `quantapdf_page_crop`:

1. validate `struct_size >= offsetof(quantapdf_page_crop, bounds) + sizeof(quantapdf_rect)`;
2. validate finite ordered public bounds;
3. validate page index and uniqueness;
4. resolve `quantapdf_pdf_crop_page_view`;
5. require requested public bounds to be contained by `visible_public`;
6. determine `changed` by exact float equality against `visible_public`;
7. transform all four requested public corners using `view.public_to_pdf`;
8. normalize those four transformed points into `requested_pdf`;
9. require the normalized requested PDF rectangle to be contained by `visible_pdf` within exact fixture arithmetic; do not silently clamp it.

The build-plan function performs no writes.

- [ ] **Step 6: Add crop-local encrypted/signed fail-closed policy**

Do not refactor Phase 5 editor code in this task. Implement a crop-local security preflight equivalent to the existing editor policy:

```text
Root trailer contains /Encrypt -> UNSUPPORTED
any AcroForm signature field for which pdf_signature_is_signed(...) is true -> UNSUPPORTED
```

Use `pdf_walk_tree()` over `Root/AcroForm/Fields` exactly as the existing editor does, but keep the helper crop-private to avoid premature generic transform infrastructure.

- [ ] **Step 7: Implement canonical all-no-op output**

In `quantapdf_crop_pages()`:

```c
source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
if (source_pdf == NULL)
    return QUANTAPDF_ERROR_UNSUPPORTED;
```

Allocate `crop_count` plan entries with overflow checks. Run security preflight and `quantapdf_pdf_crop_build_plan()` before any serialization.

If `any_changed == 0`, return:

```c
return quantapdf_serialize_pdf(document->ctx, source_pdf, out_output);
```

This must not materialize inherited CropBox entries.

If `any_changed != 0`, continue returning `QUANTAPDF_ERROR_UNSUPPORTED` for now so the changed-crop case remains the next attributable RED.

- [ ] **Step 8: Build and run the suite**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: old #1-#21 pass; validation/security/malformed/no-op assertions inside `quantapdf.pdf_crop` pass; the target still fails only at the first changed crop with `valid crop failed`.

- [ ] **Step 9: Commit strict preflight/no-op support**

```bash
git add \
  src/pdf_crop_internal.h src/pdf_crop_preflight.c src/pdf_crop.c CMakeLists.txt \
  tests/test_pdf_crop.c tests/CMakeLists.txt \
  tests/fixtures/crop-malformed-box.pdf \
  tests/fixtures/crop-malformed-rotate.pdf \
  tests/fixtures/crop-malformed-userunit.pdf
git commit -m "feat: preflight immutable CropBox transforms"
```

**Task gate:** source preflight must be read-only; grep/diff review must show no `pdf_dict_put`, `pdf_dict_del`, content rewrite, annotation update, or page graft in `src/pdf_crop_preflight.c`.

---

### Task 4: Implement isolated unrotated CropBox mutation and full interactive preservation

**Files:**
- Modify: `src/pdf_crop.c`
- Modify: `tests/test_pdf_crop.c`
- Create: `tests/test_pdf_crop_internal.h`
- Create: `tests/test_pdf_crop_raw.c`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/fixtures/crop-inherited.pdf`

**Interfaces:**
- Consumes: `quantapdf_pdf_crop_build_plan()` and `quantapdf_pdf_crop_resolve_page()` from Task 3; deterministic `quantapdf_serialize_pdf()`.
- Produces: changed-page immutable transform for identity/unrotated page transforms; test-only raw structural parser.

- [ ] **Step 1: Extend the public test to capture source observations before the transform**

Before calling the changed crop, capture through existing public APIs:

```text
page 1 bounds + CropBox
plain/structured text geometry containing CROP-TEXT
first image occurrence geometry
URI link hotspot + exact URI bytes
internal link target page + target coordinates
Square annotation type/flags/contents + bounds
form field name/type/value + Widget page/bounds/flags
outline title/hierarchy/destination kind/page/coordinates
```

Store scalar/string copies owned by the test so they survive closing/reopening handles.

Use crop requests:

```c
quantapdf_page_crop crops[2] = {
    { sizeof(quantapdf_page_crop), 0, { 50.0f, 40.0f, 350.0f, 260.0f } },
    { sizeof(quantapdf_page_crop), 1, { 20.0f, 30.0f, 380.0f, 270.0f } }
};
```

The fixture is unrotated, so expected post-crop public geometry is the source geometry translated by each page's crop origin.

- [ ] **Step 2: Add source-immutability and output-lifetime assertions**

After the transform succeeds:

1. re-query the still-open source and require its original page bounds/objects to be unchanged;
2. obtain output bytes with `quantapdf_output_data()`;
3. close the source document;
4. write only the already-materialized output bytes to a temporary test path;
5. reopen that output and perform all transformed observations.

Do not call source APIs after source close.

- [ ] **Step 3: Assert full-document interactive preservation through public APIs**

For each page-local rectangle/point `g` on page 1, assert:

```text
new(g).x = old(g).x - 50
new(g).y = old(g).y - 40
```

For page 2 destination points, assert subtraction by `(20, 30)`.

Also require all non-geometric semantics to be unchanged:

```text
URI bytes
annotation type/flags/contents
form field name/type/value
Widget field association/flags/button-option semantics
outline title/hierarchy/destination kind/target page
internal-link target page
```

The deliberately out-of-crop Square annotation must still enumerate and may have negative/out-of-bounds public coordinates.

- [ ] **Step 4: Add inherited-CropBox fixture and assertions**

Create `tests/fixtures/crop-inherited.pdf` with:

```text
Pages node:
  /MediaBox [0 0 400 300]
  /CropBox  [10 20 390 280]
  /Rotate   0
Child Page:
  no local /MediaBox
  no local /CropBox
  no local /Rotate
```

Test a changed crop and an all-no-op crop. The changed output must materialize a page-local `/CropBox`; the no-op output must not.

- [ ] **Step 5: Add the raw structural test helper**

Create `tests/test_pdf_crop_internal.h`:

```c
#ifndef QUANTAPDF_TEST_PDF_CROP_INTERNAL_H
#define QUANTAPDF_TEST_PDF_CROP_INTERNAL_H

#include <stddef.h>

int crop_raw_expect_local_cropbox(
    const unsigned char *data,
    size_t size,
    int page_index,
    int expect_present,
    const float expected[4]);

int crop_raw_expect_preserved_graph(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size);

#endif
```

Create `tests/test_pdf_crop_raw.c` as test-only MuPDF code. It may parse serialized bytes and use `pdf_lookup_page_obj()`/dictionary reads. It must not be added to the library.

`crop_raw_expect_local_cropbox()` verifies exact local-key presence via page dictionary enumeration rather than inherited lookup. For changed pages it verifies `[llx lly urx ury]`; for no-op inherited pages it verifies no local CropBox key.

`crop_raw_expect_preserved_graph()` compares semantic object content for `/Contents`, `/Resources`, `/Annots`, root `/AcroForm`, and root `/Outlines` without assuming indirect object numbers are stable across serialization. Serialize/format object values in the helper or recursively compare resolved object content as appropriate; do not compare raw object numbers.

- [ ] **Step 6: Link the crop test helper privately to MuPDF while keeping one CTest**

Change the test target to:

```cmake
add_executable(quantapdf_test_pdf_crop
  test_pdf_crop.c
  test_pdf_crop_raw.c)
target_link_libraries(quantapdf_test_pdf_crop PRIVATE
  QuantaPDF::QuantaPDF
  unofficial::libmupdf::libmupdf)
```

Add compile definitions for `CROP_INHERITED_PDF` and a temporary crop-output path in `${CMAKE_CURRENT_BINARY_DIR}`.

- [ ] **Step 7: Run tests first and confirm the changed-crop RED**

Before implementing mutation, run:

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^quantapdf\.pdf_crop$' --output-on-failure
```

Expected: the new preservation assertions cannot proceed because changed crop still returns `UNSUPPORTED`; failure remains at `valid crop failed`.

- [ ] **Step 8: Implement the complete-PDF private reopen path**

In `src/pdf_crop.c`, for `any_changed != 0`:

```text
1. quantapdf_serialize_pdf(source ctx, source_pdf, &seed_output)
2. fz_new_context(...)
3. install discard error/warning callbacks consistent with the editor's isolation policy
4. fz_open_memory(private_ctx, seed_output bytes)
5. pdf_open_document_with_stream(private_ctx, stream)
6. pdf_disable_js(private_ctx, private_document)
7. for every request, re-resolve the page in the private graph and rebuild that request's plan from the original public rectangle
8. no-op request: perform no write
9. changed request: raw local /CropBox write
10. quantapdf_serialize_pdf(private_ctx, private_document, out_output)
11. drop stream/document/context/seed output on all paths
```

Do not enable the MuPDF journal: source atomicity is achieved by isolation and no private graph escapes on failure.

- [ ] **Step 9: Add a narrow raw CropBox writer**

Use only the resolved page dictionary and create an array of four real values:

```c
static void quantapdf_pdf_crop_put_box(
    fz_context *ctx,
    pdf_obj *page_obj,
    fz_rect box)
{
    pdf_obj *array = pdf_new_array(ctx, NULL, 4);
    fz_try(ctx)
    {
        pdf_array_push_real(ctx, array, box.x0);
        pdf_array_push_real(ctx, array, box.y0);
        pdf_array_push_real(ctx, array, box.x1);
        pdf_array_push_real(ctx, array, box.y1);
        pdf_dict_put(ctx, page_obj, PDF_NAME(CropBox), array);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, array);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}
```

If MuPDF requires the private document parameter in `pdf_new_array()` for this version, pass `private_document`; keep the responsibility identical.

At this checkpoint deliberately reject changed pages whose resolved transform includes nonzero Rotate or non-default UserUnit with `QUANTAPDF_ERROR_UNSUPPORTED`; Task 5 removes this temporary implementation limitation after its RED fixtures are added. Do not reject no-op requests solely due to Rotate/UserUnit.

- [ ] **Step 10: Run static and sanitizer tests**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: all 22 CTests pass for the unrotated/inherited contract, including full interactive preservation and raw graph assertions. No rotated/UserUnit changed-case fixture exists yet.

- [ ] **Step 11: Commit the first semantic GREEN**

```bash
git add \
  src/pdf_crop.c \
  tests/test_pdf_crop.c tests/test_pdf_crop_raw.c tests/test_pdf_crop_internal.h \
  tests/CMakeLists.txt tests/fixtures/crop-inherited.pdf
git commit -m "feat: apply isolated CropBox transforms"
```

**Task gate:** reviewer must confirm the changed-page semantic write surface is only `/CropBox`; no page graft, content rewrite, annotation mutation, form runtime, or destination rewrite is present.

---

### Task 5: Generalize mapping for Rotate/UserUnit and lock effective-box/default-box semantics

**Files:**
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_crop.c`
- Modify: `tests/test_pdf_crop.c`
- Modify: `tests/test_pdf_crop_raw.c`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/fixtures/crop-rotate-90.pdf`
- Create: `tests/fixtures/crop-userunit.pdf`
- Create: `tests/fixtures/crop-cropbox-outside-media.pdf`

**Interfaces:**
- Consumes: `public_to_pdf` matrix and request planning from Task 3; isolated writer from Task 4.
- Produces: fully general valid V1 mapping for inherited Rotate and page-local UserUnit, plus raw/effective box semantics required by the spec.

- [ ] **Step 1: Add `/Rotate 90` and `/UserUnit` fixtures before removing the temporary limitation**

Create:

```text
crop-rotate-90.pdf
  MediaBox [0 0 400 300]
  CropBox  [0 0 400 300]
  Rotate 90
  one visible text marker and one Square annotation

crop-userunit.pdf
  MediaBox [0 0 200 150]
  CropBox  [0 0 200 150]
  page-local UserUnit 2
  one text marker and one Link annotation
```

In tests, query each source page's public bounds first and choose crop rectangles inside those returned bounds. Do not hand-assume public width/height under Rotate/UserUnit.

- [ ] **Step 2: Add a valid raw-CropBox-outside-MediaBox fixture**

Create:

```text
crop-cropbox-outside-media.pdf
  MediaBox [0 0 300 200]
  CropBox [-20 -10 280 190]
  no BleedBox
  no TrimBox
  no ArtBox
```

The effective visible PDF box is the non-empty MediaBox/CropBox intersection. The transform must accept a shrink-only request inside the resulting public visible region; it must not return `FORMAT` merely because the raw CropBox extends outside MediaBox.

- [ ] **Step 3: Add RED assertions for changed rotated/UserUnit pages**

Run before implementation:

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^quantapdf\.pdf_crop$' --output-on-failure
```

Expected: rotated or UserUnit changed crop returns the temporary `QUANTAPDF_ERROR_UNSUPPORTED`, making the new case the attributable RED; all existing unrotated crop assertions still pass before it.

- [ ] **Step 4: Remove the temporary identity-transform restriction**

Do not implement rotation with manual switch/case translations. For every changed request, use the private page's resolved `public_to_pdf` matrix directly:

```c
fz_point corners[4] = {
    { request.x0, request.y0 },
    { request.x1, request.y0 },
    { request.x0, request.y1 },
    { request.x1, request.y1 }
};

for (i = 0; i < 4; ++i)
    corners[i] = fz_transform_point(corners[i], view.public_to_pdf);
```

Normalize the four transformed corners to the raw PDF `/CropBox`. This same path must work for identity, Rotate 90/180/270, non-default UserUnit, and combinations that MuPDF reports through the page transform.

- [ ] **Step 5: Lock effective visible intersection behavior**

Keep separate values for raw effective CropBox and `visible_pdf = intersection(MediaBox, CropBox)`. Shrink-only public validation is against `visible_public` derived from the intersection.

Writing the requested changed CropBox is allowed to create a raw box wholly contained by `visible_pdf`; do not expand back into the source raw CropBox area outside MediaBox.

- [ ] **Step 6: Lock default Bleed/Trim/Art behavior structurally**

Extend `crop_raw_expect_local_cropbox()` or add a test-only helper that verifies the output page still has **no local** `/BleedBox`, `/TrimBox`, or `/ArtBox` when those keys were absent in the source. Do not assert their effective values stay the same; per PDF defaults they may now follow the new CropBox.

- [ ] **Step 7: Run static + sanitizer proof**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: 22/22 static and 22/22 ASan/UBSan pass; no unsupported guard remains for valid Rotate/UserUnit transforms.

- [ ] **Step 8: Commit generalized geometry support**

```bash
git add \
  src/pdf_crop_preflight.c src/pdf_crop.c \
  tests/test_pdf_crop.c tests/test_pdf_crop_raw.c tests/CMakeLists.txt \
  tests/fixtures/crop-rotate-90.pdf \
  tests/fixtures/crop-userunit.pdf \
  tests/fixtures/crop-cropbox-outside-media.pdf
git commit -m "feat: support transformed CropBox page spaces"
```

**Task gate:** no coordinate rewrite may appear in annotations, links, Widgets, outlines, destinations, content streams, or resources. The only raw coordinate write remains the page-local CropBox array.

---

### Task 6: Lock batch determinism, mixed no-op semantics, and final public/raw contract

**Files:**
- Modify: `tests/test_pdf_crop.c`
- Modify: `tests/test_pdf_crop_raw.c`
- Modify: `src/pdf_crop.c` only if a test exposes a real contract defect
- Modify: `src/pdf_crop_preflight.c` only if a test exposes a real contract defect

**Interfaces:**
- Consumes: complete V1 implementation from Tasks 3-5.
- Produces: final deterministic/failure-atomic contract with no remaining untested spec branch.

- [ ] **Step 1: Add mixed and repeated batch characterization before changing production**

Add tests for:

```text
A. all-no-op batch on two pages
   -> OK
   -> no page gains a local CropBox because of request membership
   -> two calls on the same unchanged source produce byte-identical output

B. mixed batch: page 0 no-op, page 1 changed
   -> page 0 structural page dictionary untouched
   -> page 1 gains expected local CropBox

C. two changed pages
   -> both output page geometries correct
   -> one canonical output

D. batch where first request is valid and second is invalid
   -> ARGUMENT/FORMAT as appropriate
   -> out_output == NULL
   -> source remains unchanged
   -> no partial output exists
```

- [ ] **Step 2: Add exact output-reset assertions for every failure class**

For `ARGUMENT`, `FORMAT`, `UNSUPPORTED`, and non-PDF cases, initialize:

```c
quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
```

and require `output == NULL` after the call.

- [ ] **Step 3: Add canonical-output determinism checks**

For one changed two-page batch, call `quantapdf_crop_pages()` twice on the same unchanged source and compare:

```c
size_a == size_b
memcmp(data_a, data_b, size_a) == 0
```

Also prove all-no-op output need not equal the original file bytes by only requiring it to reopen and equal a second canonical no-op output; do not encode original-file byte equality into the test.

- [ ] **Step 4: Run tests before production edits**

```bash
ctest --test-dir build -R '^quantapdf\.pdf_crop$' --output-on-failure
```

Expected: PASS if Tasks 3-5 implemented the spec correctly. If it fails, treat the failing assertion as a real contract defect; do not weaken the test unless it contradicts the approved spec.

- [ ] **Step 5: Make only the minimal production correction if required**

Allowed production edit scope is only `src/pdf_crop.c` and `src/pdf_crop_preflight.c`. No new public ABI, no workflow changes, no refactor into a generic transform framework.

- [ ] **Step 6: Run full Linux static and sanitizer suites**

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-asan --output-on-failure
```

Expected: 22/22 static, 22/22 ASan/UBSan.

- [ ] **Step 7: Commit the final contract tests**

```bash
git add tests/test_pdf_crop.c tests/test_pdf_crop_raw.c
git add src/pdf_crop.c src/pdf_crop_preflight.c
git commit -m "test: lock CropBox batch determinism"
```

If production files did not change, do not stage them.

---

### Task 7: Freeze the candidate, run exact-head proof, and STOP before integration

**Files:**
- No intended source/test changes.
- PR/issue metadata/comments only.

**Interfaces:**
- Consumes: final feature head from Task 6.
- Produces: frozen exact-SHA Linux + sanitizer + macOS + Windows proof and Critical/Important review decision.

- [ ] **Step 1: Freeze and record the exact candidate SHA**

Record:

```bash
git rev-parse HEAD
```

After this point, any source/test/spec change invalidates the proof and requires a fresh run. Do not amend/rebase/force-push the proven head.

- [ ] **Step 2: Audit the net feature scope against the branch base**

Compare `10aace7bae934f48f0fbcdefad5a9bb42518293d...HEAD`.

Expected paths are limited to:

```text
docs/superpowers/specs/2026-08-29-quantapdf-cropbox-transform-design.md
docs/superpowers/plans/2026-08-29-quantapdf-cropbox-transform.md
include/quantapdf/quantapdf.h
CMakeLists.txt
src/pdf_crop.c
src/pdf_crop_internal.h
src/pdf_crop_preflight.c
tests/CMakeLists.txt
tests/test_pdf_crop.c
tests/test_pdf_crop_raw.c
tests/test_pdf_crop_internal.h
tests/fixtures/crop-interactive.pdf
tests/fixtures/crop-inherited.pdf
tests/fixtures/crop-rotate-90.pdf
tests/fixtures/crop-userunit.pdf
tests/fixtures/crop-cropbox-outside-media.pdf
tests/fixtures/crop-malformed-box.pdf
tests/fixtures/crop-malformed-rotate.pdf
tests/fixtures/crop-malformed-userunit.pdf
```

Reused fixtures must not be modified.

- [ ] **Step 3: Perform forbidden-surface audit**

Search exact head and require:

```text
src/pdf_crop_preflight.c:
  no pdf_dict_put / pdf_dict_del / pdf_graft_mapped_page

src/pdf_crop.c:
  no pdf_graft_mapped_page
  no pdf_set_field_value or other high-level form setter
  no pdf_calculate_form
  no pdf_update_page / pdf_update_open_pages
  no annotation create/update/delete
  no content-stream rewrite
  only semantic dictionary write key is PDF_NAME(CropBox)

whole crop implementation:
  no JavaScript/event execution API
  no workflow file edit
```

Also confirm the public header exposes no MuPDF type.

- [ ] **Step 4: Require the current PR-head Linux workflow to be GREEN**

The exact feature SHA must have a fresh PR workflow with Linux:

```text
static configure/build/test -> 22/22
ASan/UBSan configure/build/test -> 22/22
```

Do not infer success from an earlier SHA.

- [ ] **Step 5: Trigger same-SHA `full-ci` using only the existing PR label mechanism**

Add the existing `full-ci` label to the **draft** PR. Do not edit `.github/workflows/ci.yml`.

Require the new workflow's `head_sha` to equal the frozen candidate and verify:

```text
Linux static 22/22             PASS
Linux ASan/UBSan 22/22         PASS
macOS configure/build/test     PASS
Windows DLL configure/build/test PASS
```

For Windows logs, explicitly verify `quantapdf.dll`, `quantapdf_test_pdf_crop.exe`, and `quantapdf.pdf_crop` as test 22/22 with 100% of 22 CTests passing.

- [ ] **Step 6: Perform final Critical/Important review**

Review these invariants against exact source and tests:

```text
public crop inputs are Fitz page-space
source immutable
all requests preflight before private writes
full-document copy, not page graft
private context re-resolves pages; no source pdf_obj pointer crosses contexts
only changed pages receive local CropBox
no-op inherited CropBox remains inherited
Rotate/UserUnit mapping uses page transform, not hand-coded per-object moves
raw CropBox outside MediaBox handled by effective intersection
missing Bleed/Trim/Art keys remain missing
links/annotations/Widgets/AcroForm/outline/destinations preserved structurally
objects outside crop remain enumerable
no JS/form runtime execution
encrypted/signed fail closed
output reset on failure
canonical deterministic outputs
22nd CTest adds coverage without weakening old 21
```

Fetch reviews and inline threads; no unresolved Critical/Important blocker may remain.

- [ ] **Step 7: Record the final checkpoint and STOP**

Post exact SHA, workflow IDs, 22/22 evidence, scope audit, and review conclusion to #49 and the draft PR.

Stop in this state:

```text
#49 CropBox V1
  ✅ design/spec
  ✅ implementation plan
  ✅ strict compile RED
  ✅ minimal implementation
  ✅ Linux static 22/22
  ✅ Linux ASan/UBSan 22/22
  ✅ same-SHA macOS
  ✅ same-SHA Windows DLL
  ✅ scope/forbidden-surface audit
  ✅ Critical/Important review
  ↓
STOP — explicit integration authorization required
```

Do **not** mark ready, merge, close #49, or begin poster split.

---

### Task 8: Integrate only after explicit authorization

**Files:**
- No intended source changes.
- GitHub PR/issue/roadmap metadata only.

**Interfaces:**
- Consumes: frozen feature SHA with Task 7 exact-head proof.
- Produces: exact integrated-master merge SHA plus fresh master-push 22/22 proof; closes #49 only after that proof.

- [ ] **Step 1: Re-read all merge preconditions immediately before integration**

Require:

```text
PR head == exact frozen/proven feature SHA
PR base == master
no new unresolved review/thread blocker
same-SHA full-ci success still associated with that feature SHA
master current head is known
```

If feature head moved, STOP and repeat Task 7. If master moved, inspect whether the proven feature still merges cleanly; do not silently rebase the proven feature head.

- [ ] **Step 2: Prefer normal exact-head PR merge**

Mark ready if the connector supports it, then merge with an expected-head guard equal to the frozen feature SHA. Use merge-commit semantics, not squash/rebase, unless repository policy has changed explicitly.

- [ ] **Step 3: If the known draft->ready connector GraphQL wrapper is broken, preserve exact feature content**

Do not modify or force-push the frozen feature branch merely to work around the UI wrapper.

Allowed fallback, only after explicit integration authorization:

```text
- re-read current master and require no unexpected concurrent drift during the integration transaction;
- create a two-parent merge commit whose first parent is current master and second parent is the exact frozen feature SHA;
- set the merge commit tree equal to the frozen feature tree when the feature is directly based on that master, or otherwise use only a reviewed clean merge result;
- update master with force=false only;
- verify GitHub associates the resulting commit with the canonical PR.
```

If a clean exact integration cannot be proven, STOP instead of inventing a new carrier implementation.

- [ ] **Step 4: Require fresh integrated-master push proof on the exact merge SHA**

The `master` push workflow must run on the exact merge SHA and pass:

```text
Linux static 22/22
Linux ASan/UBSan 22/22
macOS 22/22
Windows DLL 22/22
```

Inspect Windows logs for `quantapdf.dll`, `quantapdf_test_pdf_crop.exe`, test 22/22, and `100% tests passed out of 22`.

- [ ] **Step 5: Close and update tracking only after integrated proof**

After the exact merge SHA is GREEN:

```text
close #49 as completed
comment on #48 that CropBox V1 is integrated and the coordinate invariant is established
comment/update #2 Phase 6 crop/trim status without marking MediaBox physical trim complete
record merge SHA + integrated workflow ID in PR/#49
```

Do not start Poster Split automatically; it is a separate design/spec cycle under #48.

---

## Plan Self-Review Checklist

Before execution begins, verify this plan against the approved spec:

- Public ABI and ownership: Task 2.
- Fitz coordinate contract and public->PDF mapping: Tasks 3 and 5.
- strict page-tree/box/Rotate/UserUnit preflight: Task 3.
- CropBox/MediaBox intersection semantics: Tasks 3 and 5.
- page-local UserUnit rule: Tasks 3 and 5.
- batch uniqueness/finite/shrink-only validation: Task 3.
- encrypted/signed policy: Task 3.
- full-document isolated private rewrite: Task 4.
- private re-resolution/no cross-context object identity: Task 4.
- only local `/CropBox` semantic write: Tasks 4 and 7.
- no-op canonical output and inherited no-op non-materialization: Tasks 3, 4, and 6.
- full interactive preservation across text/image/link/annotation/Widget/form/outline/destinations: Task 4.
- objects outside crop remain present: Task 4.
- Rotate/UserUnit: Task 5.
- missing Bleed/Trim/Art default behavior: Task 5.
- malformed/error/output-reset behavior: Tasks 3 and 6.
- deterministic repeated outputs: Task 6.
- 21 -> 22 CTests and sanitizer coverage: Tasks 1-7.
- same-SHA Linux/macOS/Windows proof: Task 7.
- explicit integration authorization and integrated-master proof: Task 8.

No Phase 6 Poster Split, flatten, MediaBox physical trim, content translation, encryption support, incremental save, or generic object editing is part of this plan.
