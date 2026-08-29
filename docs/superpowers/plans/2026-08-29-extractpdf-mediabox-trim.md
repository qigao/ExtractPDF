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

- `src/pdf_page_box_common.h` — strict reusable page-box view and raw/public mapping declarations.
- `src/pdf_page_box_common.c` — MediaBox/CropBox inheritance resolution, CropBox provenance, Rotate/UserUnit validation, effective-visible intersection, PDF/public mapping.
- `src/pdf_trim_internal.h` — trim-only plan type and private declarations.
- `src/pdf_trim_preflight.c` — MediaBox-specific request validation, no-op classification, post-trim CropBox-intersection validation.
- `src/pdf_trim.c` — public orchestration, private full-document reopen, `/MediaBox` writer, deterministic publication.

### Existing production files modified

- `include/extractpdf/extractpdf.h` — approved `extractpdf_page_trim` and `extractpdf_trim_pages()` ABI only.
- `src/pdf_crop_internal.h` — retain crop-only plan declarations; consume common page-box view.
- `src/pdf_crop_preflight.c` — delegate strict page-box resolution to common helper without semantic changes.
- `src/pdf_crop.c` — include/name adjustment only if required by common extraction; no semantic change intended.
- `CMakeLists.txt` — add common and trim production sources.

### New tests/fixtures

- `tests/test_pdf_trim.c` — ABI, argument/security/no-op/batch/source-lifetime tests and main runner.
- `tests/test_pdf_trim_raw.c` — raw local MediaBox/CropBox/default-box/preservation checks.
- `tests/test_pdf_trim_internal.h` — test-only declarations.
- `tests/test_pdf_trim_transforms.c` — Rotate/UserUnit and preserved-CropBox coordinate cases.
- `tests/fixtures/trim-interactive.pdf` — deterministic two-page no-CropBox interactive fixture.
- `tests/fixtures/trim-preserved-crop.pdf` — inherited CropBox physical-only and clipped-visible cases.
- `tests/fixtures/trim-rotate-90.pdf`
- `tests/fixtures/trim-userunit.pdf`
- `tests/fixtures/trim-default-boxes.pdf`
- `tests/fixtures/trim-malformed-box.pdf`
- `tests/fixtures/trim-malformed-rotate.pdf`
- `tests/fixtures/trim-malformed-userunit.pdf`

### Existing test file modified

- `tests/CMakeLists.txt` — register exactly one new executable/CTest; private MuPDF link for raw helper; Windows DLL post-build copy as required.

---

### Task 1: Lock the MediaBox ABI compile RED and open the draft PR

**Files:**
- Create: `tests/test_pdf_trim.c`
- Create: `tests/fixtures/trim-interactive.pdf`
- Modify: `tests/CMakeLists.txt`
- Do not modify: public header, `src/`, root `CMakeLists.txt`, workflow files.

**Interfaces:**
- Consumes: integrated 22-test baseline.
- Produces: one failing `extractpdf_test_pdf_trim` compile target referencing the approved-but-absent ABI.

- [ ] **Step 1: Create the deterministic two-page no-CropBox fixture**

Construct `trim-interactive.pdf` with:

```text
Page 1 raw MediaBox [0 0 400 300]
Page 2 raw MediaBox [0 0 400 300]
No local/inherited CropBox.

Page 1:
  text marker TRIM-TEXT
  one image XObject occurrence
  URI link https://example.com/trim
  internal /XYZ link targeting page 2
  Square annotation Contents=(TRIM-ANNOT)
  Text Widget under a legal AcroForm hierarchy:
      parent field /T (trim)
      child Widget field /T (text)
      public full field name = trim.text
      value = TRIM-VALUE

Page 2:
  text marker TRIM-TARGET
  outline internal destination
```

Do **not** encode `/T (trim.text)` as one partial field name; the existing strict AcroForm parser rejects dotted partial names. Use Parent/Kids hierarchy to produce the public full name `trim.text`.

- [ ] **Step 2: Write the initial compile-RED test**

Reference the absent ABI exactly:

```c
extractpdf_page_trim trim;
extractpdf_output *output = NULL;

memset(&trim, 0, sizeof(trim));
trim.struct_size = sizeof(trim);
trim.page_index = 0;
trim.bounds = (extractpdf_rect){40.0f, 30.0f, 360.0f, 270.0f};

if (extractpdf_trim_pages(document, &trim, 1, &output) != EXTRACTPDF_OK)
    return fail("valid trim failed");
```

- [ ] **Step 3: Register exactly one new CTest**

```cmake
add_executable(extractpdf_test_pdf_trim test_pdf_trim.c)
target_link_libraries(extractpdf_test_pdf_trim PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_trim PRIVATE
  TRIM_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trim-interactive.pdf")
add_test(NAME extractpdf.pdf_trim COMMAND extractpdf_test_pdf_trim)
set_tests_properties(extractpdf.pdf_trim PROPERTIES TIMEOUT 60)
```

- [ ] **Step 4: Commit the strict RED surface**

```bash
git add tests/test_pdf_trim.c tests/fixtures/trim-interactive.pdf tests/CMakeLists.txt
git commit -m "test: lock MediaBox trim contract"
```

- [ ] **Step 5: Create canonical draft PR**

`feat/mediabox-trim` -> `master`, title `feat: add immutable MediaBox physical trim transform`, body tracks #51/#48/#2 and links the committed spec. State compile-RED checkpoint and explicit integration requirement.

- [ ] **Step 6: Require attributable Linux compile RED**

Expected:

```text
existing 22 targets build
only extractpdf_test_pdf_trim fails for missing extractpdf_page_trim / extractpdf_trim_pages
```

Stop if any old target fails or RED is caused by fixture/CMake defects.

---

### Task 2: Add the public ABI shell and move RED to runtime

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Create: `src/pdf_trim.c`
- Modify: `CMakeLists.txt`

**Interfaces:**

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

- [ ] **Step 1: Add exact approved ABI** beside CropBox types/function.
- [ ] **Step 2: Add minimal shell**:

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

- [ ] **Step 3: Add only `src/pdf_trim.c` to root CMake.**
- [ ] **Step 4: Run PR workflow.** Expected 23 executables compile; old 22 CTests pass; only `extractpdf.pdf_trim` fails `valid trim failed`.
- [ ] **Step 5: Commit**:

```bash
git add include/extractpdf/extractpdf.h src/pdf_trim.c CMakeLists.txt
git commit -m "feat: add MediaBox trim ABI shell"
```

Public ABI review gate: no MuPDF type, flags, generic box-rewrite API, or unrelated ABI.

---

### Task 3: Extract strict page-box common helper without changing CropBox behavior

**Files:**
- Create: `src/pdf_page_box_common.h`
- Create: `src/pdf_page_box_common.c`
- Modify: `src/pdf_crop_internal.h`
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_crop.c` only if include/name adjustment is required
- Modify: `CMakeLists.txt`

**Interfaces:**

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

extractpdf_status extractpdf_pdf_page_box_resolve(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_pdf_page_box_view *out_view);
```

- [ ] **Step 1: Characterize existing CropBox** with `ctest --test-dir build -R '^extractpdf\.pdf_crop$' --output-on-failure`; expect PASS before refactor.
- [ ] **Step 2: Move only read-only consumed-state logic**: page dictionary; Parent cycle/depth; nearest MediaBox; nearest CropBox + presence provenance; strict four-number parsing; positive raw boxes; visible intersection; inherited Rotate; page-local UserUnit; `pdf_to_public`; `media_public`; `visible_public`.
- [ ] **Step 3: Keep crop policy crop-specific**: struct_size, duplicate page, shrink-to-visible, changed/no-op, security, writer/orchestration.
- [ ] **Step 4: Adapt crop plan mapping**:

```c
public_to_pdf = fz_invert_matrix(view.pdf_to_public);
requested_pdf = normalize(transform(crops[index].bounds, public_to_pdf));
validate requested_pdf inside view.visible_pdf;
changed = requested_public != view.visible_public;
```

- [ ] **Step 5: Add common source to CMake and run static + sanitizer suites.** Expected baseline 22 pass; trim stays runtime RED.
- [ ] **Step 6: Forbidden audit** common helper contains no `pdf_dict_put`, `pdf_dict_del`, graft, serializer, JS/form runtime.
- [ ] **Step 7: Commit**:

```bash
git add src/pdf_page_box_common.h src/pdf_page_box_common.c \
  src/pdf_crop_internal.h src/pdf_crop_preflight.c src/pdf_crop.c CMakeLists.txt
git commit -m "refactor: share strict PDF page-box resolution"
```

Reject this task if any CropBox status, no-op, output semantics, tests, or public ABI changed.

---

### Task 4: Lock strict trim preflight, security, and all-no-op behavior

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

```c
typedef struct extractpdf_pdf_trim_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_media_pdf;
    int changed;
} extractpdf_pdf_trim_plan;

extractpdf_status extractpdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_pdf_trim_plan *plans,
    int *out_any_changed);
```

- [ ] **Step 1: Add tests first** for null pointers, zero count, too-small struct, bad/duplicate index, NaN/inf, zero/inverted rect, request outside MediaBox, malformed consumed MediaBox/CropBox, invalid Rotate/UserUnit, non-PDF, encrypted, signed. Every failure initializes output to sentinel `(extractpdf_output *)(uintptr_t)1` and requires `output == NULL` afterward.
- [ ] **Step 2: Add all-no-op test** by querying exact public MediaBox and submitting it unchanged. Require OK, source unchanged, repeated canonical outputs byte-identical.
- [ ] **Step 3: Run test-first RED**; failure should now be missing validation/no-op behavior, not compile.
- [ ] **Step 4: Implement trim plan**:

```c
view = extractpdf_pdf_page_box_resolve(...);
validate request inside view.media_public;
requested_media_pdf = normalize(transform(request, inverse(view.pdf_to_public)));
validate requested_media_pdf inside view.media_pdf;
changed = request != view.media_public;
if (changed && view.has_explicit_crop)
    require positive intersection(requested_media_pdf, view.crop_pdf);
```

Do not inspect BleedBox/TrimBox/ArtBox.

- [ ] **Step 5: Implement fail-closed security and all-no-op orchestration**. Changed batch still returns `UNSUPPORTED`; all-no-op serializes source once with existing deterministic serializer.
- [ ] **Step 6: Run static + sanitizer suites**. Expected all validation/no-op assertions pass, baseline 22 pass, trim test fails only at first changed request.
- [ ] **Step 7: Commit** `feat: preflight immutable MediaBox trims`.

---

### Task 5: Implement isolated MediaBox-only writer and both page-frame modes

**Files:**
- Modify: `src/pdf_trim.c`
- Create: `tests/test_pdf_trim_internal.h`
- Create: `tests/test_pdf_trim_raw.c`
- Create: `tests/fixtures/trim-preserved-crop.pdf`
- Modify: `tests/test_pdf_trim.c`
- Modify: `tests/CMakeLists.txt`

**Test-only interfaces:**

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

- [ ] **Step 1: Add inherited-CropBox fixture** with MediaBox `[0 0 400 300]`, CropBox `[50 40 350 260]`, child pages without local boxes. One page is trimmed physically while still containing all CropBox; another clips CropBox but leaves positive intersection.
- [ ] **Step 2: Derive public trim requests from actual public MediaBox observations/mapping**, never assume its public origin is `(0,0)` when CropBox exists.
- [ ] **Step 3: Add raw helper** and privately link MuPDF only to trim test. Prove changed inherited MediaBox becomes local, no-op inherited MediaBox remains inherited, CropBox raw object/value unchanged, and Contents/Resources/Annots/root AcroForm/root Outlines are semantically preserved without object-number identity assumptions.
- [ ] **Step 4: Add public assertions before production**:

```text
no CropBox fallback:
  output MediaBox/CropBox/visible origin -> (0,0)
  dimensions == requested physical dimensions
  existing geometry follows re-anchored transform

preserved CropBox physical-only:
  MediaBox changes
  visible/CropBox observation unchanged

preserved CropBox clipped:
  raw CropBox unchanged
  visible == intersection(new MediaBox, CropBox)
  visible public x0/y0 may be non-zero
  no per-object rewrite
```

Expected still RED on changed trim.

- [ ] **Step 5: Implement private changed flow**: canonical source serialize -> fresh context -> open bytes -> disable JS -> security recheck -> rebuild all private plans -> resolve all target pages -> verify private semantics before first write -> local `/MediaBox` writes only -> deterministic serialize -> cleanup.
- [ ] **Step 6: Narrow writer** uses `pdf_new_array(ctx, private_document, 4)` and exactly one `pdf_dict_put(..., PDF_NAME(MediaBox), array)` semantic key. No journal/graft/runtime mutation.
- [ ] **Step 7: Require static + ASan/UBSan 23/23** at this checkpoint.
- [ ] **Step 8: Commit** `feat: apply isolated MediaBox trims`.

---

### Task 6: Lock Rotate/UserUnit and opaque Bleed/Trim/Art preservation

**Files:**
- Create: `tests/test_pdf_trim_transforms.c`
- Create: `tests/fixtures/trim-rotate-90.pdf`
- Create: `tests/fixtures/trim-userunit.pdf`
- Create: `tests/fixtures/trim-default-boxes.pdf`
- Modify: `tests/test_pdf_trim_internal.h`
- Modify: `tests/test_pdf_trim_raw.c`
- Modify: `tests/CMakeLists.txt`
- Production correction only if a real defect appears: `src/pdf_page_box_common.c`, `src/pdf_trim_preflight.c`, `src/pdf_trim.c`.

- [ ] **Step 1: Add Rotate 90 test first**. Query public MediaBox, construct a valid public inset, trim, verify raw MediaBox equals inverse-mapped request and public behavior follows CropBox provenance.
- [ ] **Step 2: Add page-local UserUnit 2 test** with same methodology.
- [ ] **Step 3: Add default-box fixture**:

```text
page A: BleedBox/TrimBox/ArtBox absent
page B: all explicit, with at least one raw box extending beyond future new MediaBox
```

Require absent keys remain absent; explicit raw arrays unchanged; trim succeeds even when a production box's effective intersection becomes reduced or empty.

- [ ] **Step 4: Run characterization**. If mapping already passes, do not edit production. If not, fix only common/trim mapping; never add rotation switch tables or extra box writes.
- [ ] **Step 5: Run full static + sanitizer 23/23.**
- [ ] **Step 6: Commit** `test: lock transformed MediaBox trim semantics`; stage production only if it actually changed.

---

### Task 7: Lock batch determinism, failure atomicity, and lifetime preservation

**Files:**
- Modify: `tests/test_pdf_trim.c`
- Modify: `tests/test_pdf_trim_transforms.c`
- Modify: `tests/test_pdf_trim_raw.c` only if needed for one extra observation.
- Production correction only for real contract defect: `src/pdf_trim.c`, `src/pdf_trim_preflight.c`.

- [ ] **Step 1: Two-page all-no-op batch**: both current public MediaBoxes; require no local materialization and repeated canonical outputs byte-identical; do not compare to original file bytes.
- [ ] **Step 2: Mixed no-op + changed**: no-op page structurally untouched, changed page local MediaBox.
- [ ] **Step 3: Two changed pages**: identical batch twice -> same size + `memcmp == 0`.
- [ ] **Step 4: Failure atomicity**: request 0 valid changed; request 1 invalid because it leaves empty preserved-CropBox intersection. Require `ARGUMENT`, output NULL, both source pages unchanged.
- [ ] **Step 5: Source/output lifetime**: successful transform leaves source public observations unchanged; close source; output remains reopenable/fully usable; structurally enumerated objects outside new medium remain present.
- [ ] **Step 6: Run tests before production edits**. Expected PASS if implementation already matches spec; failures are real contract defects unless test contradicts committed spec.
- [ ] **Step 7: Minimal correction only if required**, no public ABI/general framework.
- [ ] **Step 8: Run static + sanitizer 23/23 and commit** `test: lock MediaBox trim batch determinism`.

---

### Task 8: Freeze exact head, same-SHA cross-platform proof, review, and STOP

**Files:**
- No intended source/test changes.
- PR/issue metadata/comments only.

- [ ] **Step 1: Freeze exact candidate SHA**. Any file change invalidates proof; no amend/rebase/force-push.
- [ ] **Step 2: Audit net paths**. Expected only spec/plan, trim/common production, crop adaptation, trim tests/fixtures, CMake/header. Reused fixtures and workflow YAML untouched.
- [ ] **Step 3: Forbidden audit**:

```text
pdf_page_box_common.c: no put/del/graft/serializer/runtime mutation
pdf_trim_preflight.c: no writes/graft; no Bleed/Trim/Art parsing
pdf_trim.c: sole semantic key write PDF_NAME(MediaBox); no graft/content/annot/form/widget/appearance/JS/event mutation
crop path: CropBox regression unchanged and sole crop write remains PDF_NAME(CropBox)
public header: no MuPDF type
```

- [ ] **Step 4: Fresh exact-head Linux PR proof**: static 23/23 + ASan/UBSan 23/23.
- [ ] **Step 5: Add existing `full-ci` label to draft PR only**; workflow YAML unchanged. Require same frozen head SHA and Linux static/sanitizer 23/23, macOS 23/23, Windows DLL 23/23. Windows logs must show `extractpdf.dll`, `extractpdf_test_pdf_trim.exe`, `extractpdf.pdf_trim` as test 23/23, and 100% of 23 tests.
- [ ] **Step 6: Final Critical/Important review** against: current-Fitz MediaBox inputs; shrink-only; source immutable; all preflight before writes; full-document isolation; private re-resolution; no source MuPDF pointer crossing; no-op inheritance preservation; fallback re-anchor; preserved-CropBox non-zero visible origin; physical-only trim != no-op; Rotate/UserUnit via common matrix; opaque Bleed/Trim/Art; structural preservation; fail-closed security; output reset; deterministic output; CropBox #22 regression; new #23 coverage.
- [ ] **Step 7: Fetch reviews/threads**; require no unresolved Critical/Important blocker.
- [ ] **Step 8: Post checkpoint to #51 + draft PR and STOP**. Do not mark ready, merge, close #51, checkpoint #48/#2 as integrated, delete branch, or start poster split without explicit integration authorization.

---

### Task 9: Integrate only after explicit authorization

**Files:**
- No intended feature-tree changes.

- [ ] **Step 1: Re-read frozen head, current master, reviews, and exact-head CI**. If feature moved, repeat Task 8; if master moved, inspect rather than silently rebasing.
- [ ] **Step 2: Prefer normal expected-head merge commit**.
- [ ] **Step 3: If draft->ready connector GraphQL is still broken**, use only the already-approved fallback after explicit integration authorization:

```text
create two-parent merge commit
  tree = frozen feature tree
  parent1 = current verified master
  parent2 = frozen feature SHA
update master force=false
verify PR is associated/merged
```

Never force-update master.

- [ ] **Step 4: Require integrated-master push proof on exact merge SHA**: Linux static 23/23 + ASan/UBSan 23/23 + macOS 23/23 + Windows DLL 23/23, including `extractpdf_test_pdf_trim.exe`.
- [ ] **Step 5: Only after proof** close #51 as completed and checkpoint #48/#2.
- [ ] **Step 6: Do not auto-start poster split.**
