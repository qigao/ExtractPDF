# ExtractPDF Immutable MediaBox Physical Trim V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable, batch, shrink-only MediaBox physical trim transform that preserves existing CropBox/interactive/document-root structures and extends the suite from 22 to 23 CTests.

**Architecture:** Reuse CropBox V1's proven full-document isolation and deterministic serializer, but keep MediaBox trim as a separate public primitive and writer. Extract only strict page-box resolution/mapping into a small private `pdf_page_box_common.[ch]` helper so CropBox and MediaBox share one proven coordinate model without creating a generic transform framework.

**Tech Stack:** C11, MuPDF 1.28.2, CMake/CTest, GitHub Actions Linux + ASan/UBSan + macOS + Windows DLL.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-design.md`

## Global Constraints

- Baseline master is `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`, tree `594499cfea3071f210b5b8781d73e942ba94a94d`.
- Public request coordinates are current ExtractPDF/Fitz page space; callers never supply raw PDF coordinates.
- MuPDF page matrix project invariant: PDF user space -> ExtractPDF/Fitz public page space; inverse maps public -> PDF.
- V1 is shrink-only against current public MediaBox.
- A real local/inherited CropBox remains structurally unchanged; output visible region is `intersection(new MediaBox, preserved CropBox)` and may have non-zero public origin.
- If no CropBox exists through inheritance, CropBox falls back to new MediaBox and output page frame re-anchors to `(0,0)`.
- BleedBox/TrimBox/ArtBox are opaque preservation state: do not write, materialize, normalize, repair, or trim-specific validate them.
- Only changed pages receive page-local `/MediaBox` writes.
- No page graft, content-stream transformation, per-object geometry rewrite, annotation/form mutation, appearance regeneration, JavaScript, form events, validation, formatting, calculation, or activation.
- Source document remains immutable; output owns independent bytes and survives source close.
- Encrypted and already-signed PDF inputs fail closed with `EXTRACTPDF_ERROR_UNSUPPORTED`.
- All requests preflight before private writes; private canonical reparse must re-resolve/revalidate every target before first write.
- No MuPDF types in public headers; no mutable process-global/TLS PDF state; no MuPDF exception crosses the C ABI.
- No `.github/workflows/ci.yml` changes are authorized.
- Current suite is 22 CTests; this slice adds exactly one new CTest: `extractpdf.pdf_trim`, yielding 23 total.
- Do not start poster split, flatten, optimize/gc, image recompression, or security rewrite work in this branch.

---

## File Structure

### New production files

- `src/pdf_page_box_common.h` — strict reusable page-box view and security-independent raw/public mapping helpers only.
- `src/pdf_page_box_common.c` — MediaBox/CropBox inheritance resolution, provenance, Rotate/UserUnit validation, effective-visible intersection, PDF/public mapping.
- `src/pdf_trim_internal.h` — trim-only plan type and private function declarations.
- `src/pdf_trim_preflight.c` — MediaBox-specific request validation, no-op classification, post-trim CropBox-intersection validation, private-plan consistency.
- `src/pdf_trim.c` — public orchestration, private full-document reopen, `/MediaBox` writer, deterministic publication.

### Existing production files modified

- `include/extractpdf/extractpdf.h` — approved `extractpdf_page_trim` and `extractpdf_trim_pages()` ABI only.
- `src/pdf_crop_internal.h` — remove page-box view ownership after common extraction; retain crop-only plan declarations.
- `src/pdf_crop_preflight.c` — delegate strict page-box resolution to common helper without changing CropBox policy/results.
- `src/pdf_crop.c` — no semantic change intended; only include/function-name adjustments if common helper naming requires them.
- `CMakeLists.txt` — add common and trim production sources.

### New tests/fixtures

- `tests/test_pdf_trim.c` — public ABI, argument/security/no-op/batch/source-lifetime tests and main runner.
- `tests/test_pdf_trim_raw.c` — raw local MediaBox/CropBox/default-box/preservation observations using MuPDF privately.
- `tests/test_pdf_trim_internal.h` — test-only helper declarations.
- `tests/test_pdf_trim_transforms.c` — Rotate/UserUnit + preserved-CropBox public coordinate cases.
- `tests/fixtures/trim-interactive.pdf` — deterministic two-page no-CropBox interactive fixture.
- `tests/fixtures/trim-preserved-crop.pdf` — explicit/inherited CropBox cases including physical-only and clipped-visible cases.
- `tests/fixtures/trim-rotate-90.pdf` — Rotate 90 fixture.
- `tests/fixtures/trim-userunit.pdf` — page-local UserUnit fixture.
- `tests/fixtures/trim-default-boxes.pdf` — absent/present Bleed/Trim/Art raw preservation fixture.
- `tests/fixtures/trim-malformed-box.pdf` — malformed consumed MediaBox/CropBox fixture.
- `tests/fixtures/trim-malformed-rotate.pdf` — invalid inherited Rotate fixture.
- `tests/fixtures/trim-malformed-userunit.pdf` — invalid page-local UserUnit fixture.

### Existing test file modified

- `tests/CMakeLists.txt` — register exactly one new executable/CTest, link private MuPDF for raw helper, copy `extractpdf.dll` post-build on Windows as needed.

---

### Task 1: Lock the MediaBox ABI compile RED and open the draft PR

**Files:**
- Create: `tests/test_pdf_trim.c`
- Create: `tests/fixtures/trim-interactive.pdf`
- Modify: `tests/CMakeLists.txt`
- Do not modify: public header, `src/`, root `CMakeLists.txt`, workflow files.

**Interfaces:**
- Consumes: current 22-test integrated baseline.
- Produces: one failing `extractpdf_test_pdf_trim` compile target referencing the approved-but-absent ABI.

- [ ] **Step 1: Create the deterministic two-page no-CropBox fixture**

Construct `trim-interactive.pdf` as a small hand-authored deterministic PDF with:

```text
Page 1 raw MediaBox [0 0 400 300]
Page 2 raw MediaBox [0 0 400 300]
No local/inherited CropBox anywhere.
No BleedBox/TrimBox/ArtBox unless a later dedicated fixture needs them.

Page 1:
  text marker: TRIM-TEXT
  one image XObject occurrence
  URI link: https://example.com/trim
  internal /XYZ link targeting page 2
  Square annotation, Contents=(TRIM-ANNOT)
  Text Widget attached to AcroForm field trim.text with value TRIM-VALUE

Page 2:
  text marker: TRIM-TARGET
  outline destination target
```

Keep all object numbers stable inside the fixture but never make production behavior depend on them.

- [ ] **Step 2: Write the initial compile-RED test**

In `tests/test_pdf_trim.c`, include `extractpdf/extractpdf.h` and reference the absent approved ABI exactly:

```c
extractpdf_page_trim trim;
extractpdf_output *output = NULL;

memset(&trim, 0, sizeof(trim));
trim.struct_size = sizeof(trim);
trim.page_index = 0;
trim.bounds.x0 = 40.0f;
trim.bounds.y0 = 30.0f;
trim.bounds.x1 = 360.0f;
trim.bounds.y1 = 270.0f;

if (extractpdf_trim_pages(document, &trim, 1, &output) != EXTRACTPDF_OK)
    return fail("valid trim failed");
```

The test must not contain fallback declarations that would let it compile before the public ABI exists.

- [ ] **Step 3: Register exactly one new CTest**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_pdf_trim test_pdf_trim.c)
target_link_libraries(extractpdf_test_pdf_trim PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_trim PRIVATE
  TRIM_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trim-interactive.pdf")
add_test(NAME extractpdf.pdf_trim COMMAND extractpdf_test_pdf_trim)
set_tests_properties(extractpdf.pdf_trim PROPERTIES TIMEOUT 60)
```

No existing CTest is renamed or removed.

- [ ] **Step 4: Commit the strict RED surface**

```bash
git add tests/test_pdf_trim.c tests/fixtures/trim-interactive.pdf tests/CMakeLists.txt
git commit -m "test: lock MediaBox trim contract"
```

- [ ] **Step 5: Create a canonical draft PR**

Create draft PR from `feat/mediabox-trim` to `master`:

```text
Title: feat: add immutable MediaBox physical trim transform
Body: Tracks #51. Child of #48 / #2. Links committed spec. State that current checkpoint is strict compile RED and integration requires explicit authorization.
```

- [ ] **Step 6: Require attributable Linux compile RED**

Expected latest PR workflow result:

```text
existing 22 targets build
new extractpdf_test_pdf_trim fails compilation only because:
  extractpdf_page_trim is unknown and/or
  extractpdf_trim_pages is undeclared
```

Do not proceed if any old target fails or the new failure is fixture/CMake related.

---

### Task 2: Add the public ABI shell and move RED from compile-time to runtime

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Create: `src/pdf_trim.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_trim.c`

**Interfaces:**
- Produces public ABI:

```c
typedef struct extractpdf_page_trim {
    size_t struct_size;
    int page_index;
    extractpdf_rect bounds;
} extractpdf_page_trim;

EXTRACTPDF_API extractpdf_status extractpdf_trim_pages(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_output **out_output);
```

- [ ] **Step 1: Add the exact approved ABI**

Place `extractpdf_page_trim` beside `extractpdf_page_crop`; place `extractpdf_trim_pages()` beside `extractpdf_crop_pages()`.

No flags/options/general page-box enum are added.

- [ ] **Step 2: Add the minimal shell**

Create `src/pdf_trim.c`:

```c
#include "internal.h"

extractpdf_status extractpdf_trim_pages(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_output **out_output)
{
    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || trims == NULL || trim_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

- [ ] **Step 3: Add only `src/pdf_trim.c` to root CMake**

Do not add common helpers yet.

- [ ] **Step 4: Run the latest PR workflow**

Expected:

```text
all 23 executables compile
CTest #1-#22 pass
extractpdf.pdf_trim fails at runtime with "valid trim failed"
```

- [ ] **Step 5: Commit the ABI shell**

```bash
git add include/extractpdf/extractpdf.h src/pdf_trim.c CMakeLists.txt
git commit -m "feat: add MediaBox trim ABI shell"
```

Review gate: public header exposes no MuPDF type and no API beyond the approved structure/function.

---

### Task 3: Extract the strict page-box common helper without changing CropBox behavior

**Files:**
- Create: `src/pdf_page_box_common.h`
- Create: `src/pdf_page_box_common.c`
- Modify: `src/pdf_crop_internal.h`
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_crop.c` only if include/name adjustment is required
- Modify: `CMakeLists.txt`
- Existing regression test: `extractpdf.pdf_crop`

**Interfaces:**

Create common view:

```c
typedef struct extractpdf_pdf_page_box_view {
    pdf_obj *page_obj;
    fz_rect media_pdf;
    fz_rect crop_pdf;
    fz_rect visible_pdf;
    fz_matrix pdf_to_public;
    extractpdf_rect media_public;
    extractpdf_rect visible_public;
    int has_explicit_crop;
    int rotate_degrees;
    float user_unit;
} extractpdf_pdf_page_box_view;
```

Create helper:

```c
extractpdf_status extractpdf_pdf_page_box_resolve(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_pdf_page_box_view *out_view);
```

- [ ] **Step 1: Characterize CropBox before extraction**

Run only existing CropBox test on current Task-2 head:

```bash
ctest --test-dir build -R '^extractpdf\.pdf_crop$' --output-on-failure
```

Expected PASS. Record this as the before-extraction behavior.

- [ ] **Step 2: Move only strict consumed-state logic into common helper**

Move from crop preflight into `pdf_page_box_common.c`:

```text
page dictionary validation
Parent traversal + cycle/depth protection
nearest MediaBox lookup
nearest CropBox lookup + explicit provenance
strict four-finite-number parse
normalized positive raw boxes
visible intersection
Rotate inherited validation
page-local UserUnit validation
pdf_to_public retrieval
media_public derivation
visible_public derivation
```

Do **not** move:

```text
crop request struct_size checks
duplicate page checks
crop shrink-only-to-visible policy
crop changed/no-op classification
crop security policy
crop writer/orchestration
```

- [ ] **Step 3: Adapt CropBox plan builder to the common view**

`extractpdf_pdf_crop_build_plan()` should consume `extractpdf_pdf_page_box_view` and preserve the exact integrated contract:

```c
public_to_pdf = fz_invert_matrix(view.pdf_to_public);
requested_pdf = normalize(transform(crops[index].bounds, public_to_pdf));
validate requested_pdf inside view.visible_pdf;
changed = requested_public != view.visible_public;
```

Keep `extractpdf_pdf_crop_plan` crop-specific.

- [ ] **Step 4: Add common source to CMake and run CropBox regression**

Expected:

```text
extractpdf.pdf_crop PASS
all existing 22 baseline CTests PASS
new trim test still runtime RED because shell returns UNSUPPORTED
```

Run both static and sanitizer suites before committing extraction.

- [ ] **Step 5: Audit forbidden accidental behavior**

`pdf_page_box_common.c` must contain no:

```text
pdf_dict_put
pdf_dict_del
pdf_graft_mapped_page
serializer call
JavaScript/form API
```

It is a read-only resolver only.

- [ ] **Step 6: Commit the extraction**

```bash
git add src/pdf_page_box_common.h src/pdf_page_box_common.c \
  src/pdf_crop_internal.h src/pdf_crop_preflight.c src/pdf_crop.c CMakeLists.txt
git commit -m "refactor: share strict PDF page-box resolution"
```

Reviewer gate: reject the task if CropBox behavior, statuses, no-op semantics, fixture expectations, or public ABI changed.

---

### Task 4: Lock strict MediaBox preflight, security, and no-op semantics

**Files:**
- Create: `src/pdf_trim_internal.h`
- Create: `src/pdf_trim_preflight.c`
- Modify: `src/pdf_trim.c`
- Create: `tests/fixtures/trim-malformed-box.pdf`
- Create: `tests/fixtures/trim-malformed-rotate.pdf`
- Create: `tests/fixtures/trim-malformed-userunit.pdf`
- Modify: `tests/test_pdf_trim.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

Trim plan:

```c
typedef struct extractpdf_pdf_trim_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_media_pdf;
    int changed;
} extractpdf_pdf_trim_plan;
```

Preflight:

```c
extractpdf_status extractpdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_pdf_trim_plan *plans,
    int *out_any_changed);
```

Security may initially call a shared-or-copied fail-closed helper, but do not refactor Phase 5 unrelated code.

- [ ] **Step 1: Extend tests first with argument and malformed cases**

Add helper requiring output reset:

```c
static int expect_trim_error(..., extractpdf_status expected)
{
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;
    extractpdf_status status = extractpdf_trim_pages(..., &output);
    return status == expected && output == NULL;
}
```

Lock at least:

```text
NULL document/trims/out_output
trim_count == 0
small struct_size
out-of-range page
negative page
NaN/infinity
zero/inverted rectangle
duplicate page index
request outside public MediaBox
malformed MediaBox/CropBox -> FORMAT
invalid inherited Rotate -> FORMAT
invalid page-local UserUnit -> FORMAT
non-PDF -> UNSUPPORTED
encrypted -> UNSUPPORTED
signed -> UNSUPPORTED
```

- [ ] **Step 2: Add an all-no-op test before implementation**

Query public MediaBox via existing API and pass that exact rectangle as trim request.

Require:

```text
OK
output != NULL
source remains unchanged
repeated all-no-op calls produce byte-identical canonical bytes
```

Raw non-materialization is added in Task 5.

- [ ] **Step 3: Run test to verify new RED**

Expected first failure should be a missing validation/no-op behavior in the shell, not a compile error.

- [ ] **Step 4: Implement trim-only plan building**

For each request:

```c
view = extractpdf_pdf_page_box_resolve(...);
validate request inside view.media_public;
public_to_pdf = fz_invert_matrix(view.pdf_to_public);
requested_media_pdf = normalize(transform(request, public_to_pdf));
validate requested_media_pdf inside view.media_pdf;
changed = request != view.media_public;

if (changed && view.has_explicit_crop) {
    visible_after = intersect(requested_media_pdf, view.crop_pdf);
    if (empty(visible_after))
        return EXTRACTPDF_ERROR_ARGUMENT;
}
```

Do not parse BleedBox/TrimBox/ArtBox.

- [ ] **Step 5: Implement source security + all-no-op path**

`extractpdf_trim_pages()` must:

```text
reset out_output
reject non-PDF/encrypted/signed
allocate plans safely
build complete source plan
if all no-op:
  serialize source once using existing deterministic serializer
else:
  return UNSUPPORTED for now
```

Changed trim remains RED until Task 5.

- [ ] **Step 6: Run static and sanitizer suites**

Expected:

```text
all validation/no-op assertions pass
all baseline 22 CTests pass
extractpdf.pdf_trim fails only at first valid changed trim
```

- [ ] **Step 7: Commit**

```bash
git add src/pdf_trim_internal.h src/pdf_trim_preflight.c src/pdf_trim.c \
  tests/test_pdf_trim.c tests/CMakeLists.txt tests/fixtures/trim-malformed-*.pdf
git commit -m "feat: preflight immutable MediaBox trims"
```

---

### Task 5: Implement isolated MediaBox-only writer and both page-frame modes

**Files:**
- Modify: `src/pdf_trim.c`
- Create: `tests/test_pdf_trim_internal.h`
- Create: `tests/test_pdf_trim_raw.c`
- Create: `tests/fixtures/trim-preserved-crop.pdf`
- Modify: `tests/test_pdf_trim.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- `extractpdf_trim_pages()` becomes functional for unrotated/default-UserUnit pages.
- Raw helper API:

```c
int trim_raw_expect_local_mediabox(
    const unsigned char *data, size_t size, int page_index,
    int expect_present, const float expected[4]);

int trim_raw_expect_preserved_cropbox(
    const unsigned char *before, size_t before_size,
    const unsigned char *after, size_t after_size,
    int page_index);

int trim_raw_expect_preserved_graph(
    const unsigned char *before, size_t before_size,
    const unsigned char *after, size_t after_size);
```

- [ ] **Step 1: Add preserved-CropBox fixture**

Create deterministic pages covering:

```text
Page A:
  inherited MediaBox [0 0 400 300]
  inherited CropBox [50 40 350 260]
  no local MediaBox/CropBox

Page B:
  inherited MediaBox [0 0 400 300]
  inherited CropBox [50 40 350 260]
```

Use Page A for physical-only trim that still contains CropBox, e.g. public MediaBox request corresponding to raw `[20 10 380 290]`.
Use Page B for trim clipping CropBox but retaining positive intersection, e.g. raw `[100 80 380 290]`.

The exact public request rectangles must be derived/locked from `extractpdf_page_box_bounds(...MEDIA...)` and the common mapping, not guessed from a `(0,0)` assumption.

- [ ] **Step 2: Add raw test helper**

Link only this test executable privately to MuPDF. Verify:

```text
changed inherited MediaBox becomes local
no-op inherited MediaBox remains inherited
raw CropBox object/value remains unchanged
Contents/Resources/Annots/root AcroForm/root Outlines remain semantically unchanged
```

Do not compare indirect object numbers as identity.

- [ ] **Step 3: Add public page-frame assertions first**

No-CropBox fallback page:

```text
source request uses source MediaBox public subrect
output MediaBox/CropBox/visible origin == (0,0)
output dimensions equal request width/height
text/image/link/annotation/widget/destination geometry follows re-anchored page transform
```

Preserved-CropBox physical-only page:

```text
MediaBox changes
visible page bounds unchanged
CropBox public observation unchanged
```

Preserved-CropBox clipped page:

```text
MediaBox changes
raw CropBox unchanged
visible = intersection(new MediaBox, CropBox)
visible public x0/y0 may be non-zero
object geometry is not individually rewritten
```

Run now; expected changed trim RED remains `UNSUPPORTED`.

- [ ] **Step 4: Implement private changed transform flow**

Mirror CropBox isolation without sharing writer/orchestration:

```text
canonical source serialization
fresh private context
open memory stream
pdf_disable_js
security recheck
allocate private plans/views
rebuild complete trim plan in private graph
resolve every target page before write
verify private plan classification/semantics
write only changed local /MediaBox arrays
serialize private PDF
cleanup all private state
```

Writer must be narrow:

```c
static void extractpdf_pdf_trim_put_mediabox(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *page_obj,
    fz_rect box)
{
    pdf_obj *array = NULL;
    fz_var(array);
    fz_try(ctx) {
        array = pdf_new_array(ctx, document, 4);
        pdf_array_push_real(ctx, array, box.x0);
        pdf_array_push_real(ctx, array, box.y0);
        pdf_array_push_real(ctx, array, box.x1);
        pdf_array_push_real(ctx, array, box.y1);
        pdf_dict_put(ctx, page_obj, PDF_NAME(MediaBox), array);
    }
    fz_always(ctx) { pdf_drop_obj(ctx, array); }
    fz_catch(ctx) { fz_rethrow(ctx); }
}
```

No other semantic dictionary key may be written.

- [ ] **Step 5: Require static + ASan/UBSan 23/23**

At this checkpoint unrotated/default-UserUnit fallback and preserved-CropBox cases must be GREEN.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_trim.c tests/test_pdf_trim.c tests/test_pdf_trim_raw.c \
  tests/test_pdf_trim_internal.h tests/fixtures/trim-preserved-crop.pdf tests/CMakeLists.txt
git commit -m "feat: apply isolated MediaBox trims"
```

Review gate: only `/MediaBox` writer, no graft, no per-object mutation, no default-box materialization.

---

### Task 6: Add Rotate/UserUnit and opaque Bleed/Trim/Art preservation

**Files:**
- Create: `tests/test_pdf_trim_transforms.c`
- Create: `tests/fixtures/trim-rotate-90.pdf`
- Create: `tests/fixtures/trim-userunit.pdf`
- Create: `tests/fixtures/trim-default-boxes.pdf`
- Modify: `tests/test_pdf_trim_internal.h`
- Modify: `tests/test_pdf_trim_raw.c`
- Modify: `tests/CMakeLists.txt`
- Production changes only if a real common-mapping defect appears: `src/pdf_page_box_common.c`, `src/pdf_trim_preflight.c`, or `src/pdf_trim.c`.

**Interfaces:**
- Reuses common `pdf_to_public`; no per-rotation formula API.

- [ ] **Step 1: Add transformed fixtures and tests first**

Rotate fixture: valid `/Rotate 90`, changed MediaBox trim.

UserUnit fixture: valid page-local `/UserUnit 2`, changed MediaBox trim.

For each:

```text
query current public MediaBox
construct a strict inset in public space
trim
verify output raw MediaBox equals inverse-mapped request
verify public MediaBox/visible behavior according to CropBox provenance
```

- [ ] **Step 2: Add opaque default-box preservation fixture**

Use pages with:

```text
one page: BleedBox/TrimBox/ArtBox all absent
one page: explicit BleedBox/TrimBox/ArtBox values, including values extending beyond future new MediaBox
```

Require after trim:

```text
absent keys remain absent
explicit raw arrays remain semantically unchanged
trim succeeds even if an effective production box becomes reduced/empty
```

Do not add trim-specific malformed-production-box rejection tests; spec explicitly excludes those keys from consumed-state validation.

- [ ] **Step 3: Run test-first RED/characterization**

If Task 5 already handles Rotate/UserUnit through common matrix correctly, these tests may PASS immediately. That is acceptable characterization, not a reason to edit production.

- [ ] **Step 4: Make only minimal production correction if tests expose a spec defect**

Allowed correction surface is restricted to the three trim/common mapping files above. Do not introduce rotation switch/case tables or write extra boxes.

- [ ] **Step 5: Run full static + sanitizer suites**

Expected 23/23 in both configurations.

- [ ] **Step 6: Commit tests and any required minimal correction**

```bash
git add tests/test_pdf_trim_transforms.c tests/test_pdf_trim_raw.c \
  tests/test_pdf_trim_internal.h tests/fixtures/trim-rotate-90.pdf \
  tests/fixtures/trim-userunit.pdf tests/fixtures/trim-default-boxes.pdf tests/CMakeLists.txt
git add src/pdf_page_box_common.c src/pdf_trim_preflight.c src/pdf_trim.c
git commit -m "test: lock transformed MediaBox trim semantics"
```

If production files did not change, do not stage them.

---

### Task 7: Lock batch determinism, failure atomicity, and final public preservation

**Files:**
- Modify: `tests/test_pdf_trim.c`
- Modify: `tests/test_pdf_trim_transforms.c`
- Modify: `tests/test_pdf_trim_raw.c` only if an existing helper needs one additional observation.
- Production changes only for a real contract defect: `src/pdf_trim.c`, `src/pdf_trim_preflight.c`.

- [ ] **Step 1: Add all-no-op two-page determinism**

Build two requests equal to each page's current public MediaBox.

Require:

```text
OK
no local MediaBox materialized merely because page was requested
repeated outputs have same size and byte-identical data
no assertion that canonical output equals original input bytes
```

- [ ] **Step 2: Add mixed no-op + changed batch**

Require:

```text
no-op page remains structurally untouched
changed page receives local MediaBox
both output pages reopen correctly
```

- [ ] **Step 3: Add two-changed-page deterministic batch**

Call identical changed batch twice on unchanged source:

```c
size_a == size_b
memcmp(data_a, data_b, size_a) == 0
```

- [ ] **Step 4: Add failure-atomic ordering case**

Batch:

```text
request 0 valid changed trim
request 1 invalid/disjoint-with-preserved-CropBox trim
```

Require:

```text
EXTRACTPDF_ERROR_ARGUMENT
output == NULL
source page 0 unchanged
source page 1 unchanged
```

- [ ] **Step 5: Add source-lifetime/public preservation checks**

On a successful changed output:

```text
source still exposes original MediaBox/CropBox/text/link/annotation/form/outline observations
close source
reopen/use output successfully afterward
objects outside new medium remain structurally enumerable where existing APIs enumerate by structure
```

- [ ] **Step 6: Run tests before production edits**

Expected PASS if Tasks 4-6 fully implement the spec. Any failure is a contract defect; do not weaken tests unless they contradict the committed spec.

- [ ] **Step 7: Make minimal correction only if required**

No public ABI changes and no new transform framework.

- [ ] **Step 8: Run full static + sanitizer 23/23 and commit**

```bash
git add tests/test_pdf_trim.c tests/test_pdf_trim_transforms.c tests/test_pdf_trim_raw.c
git add src/pdf_trim.c src/pdf_trim_preflight.c
git commit -m "test: lock MediaBox trim batch determinism"
```

If production files did not change, do not stage them.

---

### Task 8: Freeze exact head, run same-SHA cross-platform proof, review, and STOP

**Files:**
- No intended source/test changes.
- PR/issue metadata/comments only.

**Interfaces:**
- Consumes: final feature head from Task 7.
- Produces: frozen exact-SHA Linux + sanitizer + macOS + Windows proof and integration authorization gate.

- [ ] **Step 1: Freeze exact candidate SHA**

After this point any source/test/spec change invalidates proof and requires a fresh run. Do not amend/rebase/force-push the proven head.

- [ ] **Step 2: Audit net feature scope against baseline**

Expected paths are limited to:

```text
docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-design.md
docs/superpowers/plans/2026-08-29-extractpdf-mediabox-trim.md
include/extractpdf/extractpdf.h
CMakeLists.txt
src/pdf_page_box_common.h
src/pdf_page_box_common.c
src/pdf_crop_internal.h
src/pdf_crop_preflight.c
src/pdf_crop.c              # only if common-helper adaptation required
src/pdf_trim_internal.h
src/pdf_trim_preflight.c
src/pdf_trim.c
tests/CMakeLists.txt
tests/test_pdf_trim.c
tests/test_pdf_trim_raw.c
tests/test_pdf_trim_internal.h
tests/test_pdf_trim_transforms.c
tests/fixtures/trim-interactive.pdf
tests/fixtures/trim-preserved-crop.pdf
tests/fixtures/trim-rotate-90.pdf
tests/fixtures/trim-userunit.pdf
tests/fixtures/trim-default-boxes.pdf
tests/fixtures/trim-malformed-box.pdf
tests/fixtures/trim-malformed-rotate.pdf
tests/fixtures/trim-malformed-userunit.pdf
```

Reused fixtures and workflow files must not be modified.

- [ ] **Step 3: Perform forbidden-surface audit**

Require:

```text
pdf_page_box_common.c:
  no put/del/graft/serializer/runtime mutation

pdf_trim_preflight.c:
  no put/del/graft
  no Bleed/Trim/Art strict parsing

pdf_trim.c:
  no page graft
  no content rewrite
  no annotation/form/widget mutation
  no appearance regeneration
  no JS/event execution
  sole semantic dictionary write key = PDF_NAME(MediaBox)

crop regression:
  CropBox writer remains sole PDF_NAME(CropBox) write for crop path
  extractpdf.pdf_crop remains GREEN

public header:
  no MuPDF types
```

- [ ] **Step 4: Require fresh exact-head Linux PR GREEN**

Same candidate SHA:

```text
static configure/build/test -> 23/23
ASan/UBSan configure/build/test -> 23/23
```

- [ ] **Step 5: Trigger existing `full-ci` label on the draft PR**

Do not edit workflow YAML. Require new workflow head SHA equals frozen candidate.

Require:

```text
Linux static 23/23
Linux ASan/UBSan 23/23
macOS 23/23
Windows DLL configure/build/test 23/23
```

Windows log must explicitly show:

```text
extractpdf.dll
extractpdf_test_pdf_trim.exe
extractpdf.pdf_trim as test 23/23
100% tests passed out of 23
```

- [ ] **Step 6: Final Critical/Important review**

Review exact source/tests against all invariants:

```text
public trim inputs are current Fitz page-space MediaBox coordinates
MediaBox shrink-only
source immutable
all requests preflight before writes
full-document copy, not graft
private context re-resolves every page
no source MuPDF pointer crosses contexts
only changed pages receive local MediaBox
no-op inherited MediaBox remains inherited
no-CropBox fallback re-anchors output page frame
preserved CropBox stays raw-unchanged and may yield non-zero visible origin
physical-only trim is not mistaken for no-op
Rotate/UserUnit use common page transform, no manual rotation geometry
Bleed/Trim/Art absent/present raw state remains untouched and is not trim-validated
links/annotations/Widgets/AcroForm/outline/destinations structurally preserved
objects outside medium remain structurally present
encrypted/signed fail closed
output reset on failure
canonical deterministic outputs
CropBox #22 regression remains GREEN
new trim #23 adds coverage without weakening old tests
```

Fetch submitted reviews and inline threads; no unresolved Critical/Important blocker may remain.

- [ ] **Step 7: Record checkpoint and STOP**

Post frozen SHA, workflow IDs, 23/23 evidence, scope audit, CropBox regression result, and review conclusion to #51 and draft PR.

Do **not** mark ready, merge, close #51, update #48/#2 as integrated, delete branch, or begin poster split without explicit integration authorization.

---

### Task 9: Integrate only after explicit authorization

**Files:**
- No intended feature-tree changes.
- Git/PR/issue metadata only.

- [ ] **Step 1: Re-read exact frozen head/base/reviews/CI**

If feature head moved, repeat Task 8. If master moved, inspect the new base and do not silently rebase the proven feature.

- [ ] **Step 2: Prefer normal expected-head PR merge**

Use merge commit semantics with expected frozen head guard.

If draft->ready connector GraphQL remains broken, the already-approved fallback pattern is allowed only after this explicit integration authorization:

```text
create exact two-parent merge commit
  tree = frozen feature tree
  parent 1 = current verified master
  parent 2 = exact frozen feature SHA
update master with force=false
verify GitHub associates PR as merged
```

Never force-update master.

- [ ] **Step 3: Require integrated-master push proof**

On exact merge SHA require:

```text
Linux static 23/23
Linux ASan/UBSan 23/23
macOS 23/23
Windows DLL 23/23
```

Windows must explicitly build `extractpdf.dll` and `extractpdf_test_pdf_trim.exe`.

- [ ] **Step 4: Close and checkpoint only after integrated proof**

Only then:

```text
close #51 as completed
record PR/merge SHA/integrated workflow in #51
add checkpoint comments to #48 and #2
```

Do not auto-start poster split.
