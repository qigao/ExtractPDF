# ExtractPDF Immutable MediaBox Physical Trim V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable, batch, shrink-only MediaBox physical trim transform that preserves existing CropBox/interactive/document-root structures and extends the suite from 22 to 23 CTests.

**Architecture:** Reuse CropBox V1's proven full-document isolation and deterministic serializer, but keep MediaBox trim as a separate public primitive and writer. Extract only strict page-box resolution/mapping into a small private `pdf_page_box_common.[ch]` helper so CropBox and MediaBox share one proven coordinate model without creating a generic transform framework. Frame-changing classification is based on the effective visible intersection before/after trim, per the committed MediaBox frame correction.

**Tech Stack:** C11, MuPDF 1.28.2, CMake/CTest, GitHub Actions Linux + ASan/UBSan + macOS + Windows DLL.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-design.md`

**Normative correction/evidence:** `docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-frame-correction.md`

## Global Constraints

- Baseline master is `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`, tree `594499cfea3071f210b5b8781d73e942ba94a94d`.
- Public request coordinates are current ExtractPDF/Fitz page space; callers never supply raw PDF coordinates.
- MuPDF page matrix project invariant: PDF user space -> ExtractPDF/Fitz public page space; inverse maps public -> PDF.
- V1 is shrink-only against current public MediaBox.
- A real local/inherited CropBox remains structurally unchanged.
- Post-trim effective visible raw box is `requested_media_pdf` when no real CropBox exists, otherwise `intersection(requested_media_pdf, preserved_crop_pdf)`.
- If post-trim effective visible raw box equals source effective visible raw box, the transform is physical-only: page frame and public visible/object geometry stay unchanged even though MediaBox changes.
- If post-trim effective visible raw box differs, MuPDF re-anchors the page frame to that new effective visible box; deterministic unrotated/UserUnit=1 visible bounds restart at `(0,0)`.
- Do not require a clipped preserved-CropBox output visible rectangle to retain its old non-zero origin. Instead test that the output MediaBox can be negative/non-zero relative to the newly re-anchored visible frame.
- If no CropBox exists through inheritance, CropBox falls back to new MediaBox and output frame re-anchors to the new MediaBox.
- BleedBox/TrimBox/ArtBox are opaque preservation state: do not write, materialize, normalize, repair, or trim-specific validate them.
- Only changed pages receive page-local `/MediaBox` writes.
- No page graft, content-stream transformation, per-object geometry rewrite, annotation/form mutation, appearance regeneration, JavaScript, form events, validation, formatting, calculation, or activation.
- Source document remains immutable; output owns independent bytes and survives source close.
- Encrypted and already-signed PDF inputs fail closed with `EXTRACTPDF_ERROR_UNSUPPORTED`.
- All requests preflight before private writes; private canonical reparse must re-resolve/revalidate every target before first write.
- No MuPDF types in public headers; no mutable process-global/TLS PDF state; no MuPDF exception crosses the C ABI.
- No `.github/workflows/ci.yml` changes are authorized.
- Current suite is 22 CTests; this slice adds exactly one new CTest `extractpdf.pdf_trim`, yielding 23 total.
- Do not start poster split, flatten, optimize/gc, image recompression, or security rewrite work in this branch.

---

## File Structure

### New production files
- `src/pdf_page_box_common.h` — strict reusable page-box view and raw/public mapping declarations.
- `src/pdf_page_box_common.c` — MediaBox/CropBox inheritance resolution, CropBox provenance, Rotate/UserUnit validation, effective-visible intersection, PDF/public mapping.
- `src/pdf_trim_internal.h` — trim-only plan type and declarations.
- `src/pdf_trim_preflight.c` — MediaBox-specific request validation, no-op classification, post-trim visible intersection, frame-change classification.
- `src/pdf_trim.c` — public orchestration, private full-document reopen, `/MediaBox` writer, deterministic publication.

### Existing production files modified
- `include/extractpdf/extractpdf.h` — approved `extractpdf_page_trim` and `extractpdf_trim_pages()` ABI only.
- `src/pdf_crop_internal.h` — retain crop-only plan declarations; consume common page-box view.
- `src/pdf_crop_preflight.c` — delegate strict page-box resolution to common helper without semantic changes.
- `src/pdf_crop.c` — include/name adjustment only if required; no semantic change intended.
- `CMakeLists.txt` — add common and trim production sources.

### New tests/fixtures
- `tests/test_pdf_trim.c`
- `tests/test_pdf_trim_raw.c`
- `tests/test_pdf_trim_internal.h`
- `tests/test_pdf_trim_transforms.c`
- `tests/fixtures/trim-interactive.pdf`
- `tests/fixtures/trim-preserved-crop.pdf`
- `tests/fixtures/trim-rotate-90.pdf`
- `tests/fixtures/trim-userunit.pdf`
- `tests/fixtures/trim-default-boxes.pdf`
- `tests/fixtures/trim-malformed-box.pdf`
- `tests/fixtures/trim-malformed-rotate.pdf`
- `tests/fixtures/trim-malformed-userunit.pdf`

### Existing test file modified
- `tests/CMakeLists.txt` — register exactly one new executable/CTest, private MuPDF link for raw helper, Windows DLL copy as required.

---

### Task 1: Lock the MediaBox ABI compile RED and open the draft PR

**Files:** Create `tests/test_pdf_trim.c`, `tests/fixtures/trim-interactive.pdf`; modify `tests/CMakeLists.txt`. No public/header/src/root-CMake/workflow edits.

**Interfaces:** Produces one compile-RED target referencing absent approved ABI.

- [ ] Create deterministic two-page fixture:

```text
Page 1/2 raw MediaBox [0 0 400 300]
No real CropBox anywhere.
Page 1: TRIM-TEXT, one image, URI https://example.com/trim,
        internal /XYZ link -> page 2,
        Square Contents=(TRIM-ANNOT),
        Text Widget with legal hierarchy:
          parent /T(trim), child Widget field /T(text)
          public full field name trim.text, value TRIM-VALUE
Page 2: TRIM-TARGET and outline internal destination.
```

Do not encode `/T(trim.text)` as one partial name; existing strict AcroForm rejects dotted partial names.

- [ ] Write initial absent-ABI test:

```c
extractpdf_page_trim trim = {0};
extractpdf_output *output = NULL;
trim.struct_size = sizeof(trim);
trim.page_index = 0;
trim.bounds = (extractpdf_rect){40,30,360,270};
if (extractpdf_trim_pages(document, &trim, 1, &output) != EXTRACTPDF_OK)
    return fail("valid trim failed");
```

- [ ] Register one new CTest:

```cmake
add_executable(extractpdf_test_pdf_trim test_pdf_trim.c)
target_link_libraries(extractpdf_test_pdf_trim PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_trim PRIVATE
  TRIM_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trim-interactive.pdf")
add_test(NAME extractpdf.pdf_trim COMMAND extractpdf_test_pdf_trim)
set_tests_properties(extractpdf.pdf_trim PROPERTIES TIMEOUT 60)
```

- [ ] Commit `test: lock MediaBox trim contract`.
- [ ] Create canonical **draft** PR `feat/mediabox-trim` -> `master`, tracking #51/#48/#2 and committed spec/correction.
- [ ] Require Linux compile RED attributable only to missing `extractpdf_page_trim` / `extractpdf_trim_pages`; old 22 targets must still build. Stop otherwise.

---

### Task 2: Add the public ABI shell and move RED to runtime

**Files:** modify header/root CMake; create `src/pdf_trim.c`.

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

- [ ] Add exact ABI beside CropBox ABI; no options/generic page-box rewrite API.
- [ ] Add shell:

```c
#include "internal.h"
extractpdf_status extractpdf_trim_pages(...)
{
    if (out_output == NULL) return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || trims == NULL || trim_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

- [ ] Add only `src/pdf_trim.c` to root CMake.
- [ ] Require workflow: 23 executables compile; old 22 CTests pass; only `extractpdf.pdf_trim` runtime REDs at changed trim.
- [ ] Commit `feat: add MediaBox trim ABI shell`.

---

### Task 3: Extract strict page-box common helper without changing CropBox behavior

**Files:** create `src/pdf_page_box_common.[ch]`; modify crop internal/preflight, root CMake, and `src/pdf_crop.c` only if include/name adjustment is necessary.

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
    fz_context *ctx, pdf_document *document, int page_index,
    extractpdf_pdf_page_box_view *out_view);
```

- [ ] Characterize `extractpdf.pdf_crop` before refactor; PASS required.
- [ ] Move only read-only consumed-state logic: page dictionary; Parent cycle/depth; nearest MediaBox; nearest CropBox + provenance; strict finite boxes; visible intersection; inherited Rotate; page-local UserUnit; `pdf_to_public`; media/visible public rects.
- [ ] Keep CropBox struct-size/duplicates/shrink-to-visible/no-op/security/writer/orchestration crop-specific.
- [ ] Adapt crop plan mapping with `inverse(view.pdf_to_public)` and preserve exact integrated behavior.
- [ ] Add common source to CMake; run static + sanitizer. Baseline 22 incl. crop PASS; trim remains runtime RED.
- [ ] Audit common helper: no put/del/graft/serializer/JS/form runtime.
- [ ] Commit `refactor: share strict PDF page-box resolution`.

Reject this task if any CropBox status/no-op/output/test/public-ABI behavior changes.

---

### Task 4: Lock strict trim preflight, security, no-op, and frame classification

**Files:** create `src/pdf_trim_internal.h`, `src/pdf_trim_preflight.c`, three malformed fixtures; modify `src/pdf_trim.c`, trim test, tests CMake.

**Interfaces:**

```c
typedef struct extractpdf_pdf_trim_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_media_pdf;
    fz_rect output_visible_pdf;
    int changed;
    int frame_changed;
} extractpdf_pdf_trim_plan;

extractpdf_status extractpdf_pdf_trim_build_plan(
    fz_context *ctx, pdf_document *document,
    const extractpdf_page_trim *trims, size_t trim_count,
    extractpdf_pdf_trim_plan *plans, int *out_any_changed);
```

- [ ] Test-first argument/error matrix: nulls, zero count, small struct, bad/duplicate index, NaN/inf, zero/inverted, outside MediaBox, malformed consumed MediaBox/CropBox, invalid Rotate/UserUnit, non-PDF, encrypted, signed. Reuse existing non-PDF/encrypted/signed fixtures read-only; do not modify them. Initialize output sentinel and require NULL after all failures.
- [ ] `trim-malformed-box.pdf` must isolate both consumed-box failures without ambiguity: one target page with malformed MediaBox and another target page with valid MediaBox but malformed real CropBox; each selected independently.
- [ ] Add all-no-op test by querying exact current public MediaBox. Require OK, source unchanged, repeated canonical outputs byte-identical.
- [ ] Run new tests before production; expect validation/no-op RED.
- [ ] Build each plan:

```c
view = extractpdf_pdf_page_box_resolve(...);
validate request inside view.media_public;
requested_media_pdf = normalize(transform(request, inverse(view.pdf_to_public)));
validate requested_media_pdf inside view.media_pdf;
changed = request != view.media_public;

if (view.has_explicit_crop)
    output_visible_pdf = intersection(requested_media_pdf, view.crop_pdf);
else
    output_visible_pdf = requested_media_pdf;

require positive output_visible_pdf for changed request;
frame_changed = !raw_rect_equal(output_visible_pdf, view.visible_pdf);
```

A changed MediaBox with `frame_changed == 0` is a valid physical-only trim, not a no-op.

- [ ] Implement fail-closed security + all-no-op canonical serialization; changed batch still `UNSUPPORTED`.
- [ ] Static + sanitizer: all new validation/no-op assertions pass, old 22 pass, trim fails only at first changed request.
- [ ] Commit `feat: preflight immutable MediaBox trims`.

---

### Task 5: Implement isolated MediaBox-only writer and both frame modes

**Files:** modify `src/pdf_trim.c`; create trim raw helper/header, `trim-preserved-crop.pdf`; modify trim test/CMake.

**Raw helper interfaces:**

```c
int trim_raw_expect_local_mediabox(
    const unsigned char *data, size_t size, int page_index,
    int expect_present, const float expected[4]);
int trim_raw_expect_preserved_cropbox(...);
int trim_raw_expect_preserved_graph(...);
```

- [ ] Fixture: inherited MediaBox `[0 0 400 300]`, inherited CropBox `[50 40 350 260]`, child pages no local boxes. Include:
  - physical-only trim whose `requested_media_pdf ∩ crop_pdf == source visible_pdf`;
  - clipping trim whose intersection is positive but differs from source visible.
- [ ] Derive public requests from actual source public MediaBox observations/mapping; never assume source MediaBox origin `(0,0)` when CropBox exists.
- [ ] Raw helper proves changed inherited MediaBox becomes local; no-op remains inherited; raw CropBox unchanged; core graph semantically preserved without indirect-object-number identity assumptions.
- [ ] Public tests first:

```text
no real CropBox:
  changed trim -> new effective visible = new MediaBox
  output frame re-anchors
  deterministic unrotated/UserUnit=1 visible bounds [0,0,w,h]

real CropBox physical-only:
  MediaBox changes
  output_visible_pdf == source_visible_pdf
  frame unchanged
  visible/object public geometry unchanged

real CropBox clipping:
  raw CropBox unchanged
  output_visible_pdf = new MediaBox ∩ CropBox
  output_visible_pdf != source_visible_pdf
  page frame re-anchors to new effective visible intersection
  deterministic unrotated/UserUnit=1 visible bounds restart at [0,0,w,h]
  output MediaBox public rect may be negative/non-zero relative to that new visible frame
  object PDF geometry unchanged; public geometry follows new page transform
```

Do not assert clipped visible public origin remains non-zero.

- [ ] Implement changed flow: source canonical serialize -> fresh private context -> open -> disable JS -> security recheck -> rebuild complete private plans -> resolve every target -> compare private plan semantics including `output_visible_pdf` and `frame_changed` before first write -> local MediaBox writes only -> deterministic serialize.
- [ ] Writer uses `pdf_new_array(ctx, private_document, 4)` and exactly one semantic `pdf_dict_put(... PDF_NAME(MediaBox) ...)` key.
- [ ] Static + ASan/UBSan 23/23.
- [ ] Commit `feat: apply isolated MediaBox trims`.

---

### Task 6: Lock Rotate/UserUnit and opaque Bleed/Trim/Art preservation

**Files:** create transformed/default-box tests/fixtures; modify trim raw/header/CMake. Production only for a real defect in common/trim mapping files.

- [ ] Rotate 90: query source public MediaBox, create valid inset, trim, verify raw MediaBox via inverse source mapping and post-trim frame behavior via effective intersection.
- [ ] UserUnit 2: same methodology.
- [ ] Default-box fixture:

```text
page A: Bleed/Trim/Art absent
page B: explicit values, at least one extends beyond future new MediaBox
```

Require absent keys remain absent; explicit raw values semantically unchanged; trim succeeds even if effective production box becomes reduced/empty. Do not add trim-specific malformed-production-box rejection.
- [ ] Run characterization first. If already PASS, no production edit. If RED, correction only in common/trim mapping; no rotation switch tables/extra writes.
- [ ] Static + sanitizer 23/23.
- [ ] Commit `test: lock transformed MediaBox trim semantics`, staging production only if changed.

---

### Task 7: Lock batch determinism, failure atomicity, and lifetime preservation

**Files:** modify trim public/transforms/raw tests; production only for real defect in trim orchestration/preflight.

- [ ] Two-page all-no-op: no local materialization; repeated canonical outputs byte-identical; never require original input byte equality.
- [ ] Mixed no-op + changed: untouched no-op page, local MediaBox on changed page.
- [ ] Two changed pages: identical batch twice -> equal size + `memcmp == 0`.
- [ ] Failure atomicity: request 0 valid changed; request 1 leaves empty post-trim effective visible intersection -> `ARGUMENT`, output NULL, source unchanged.
- [ ] Source/output lifetime: source original observations remain; close source; output remains fully usable; structurally enumerable objects outside medium remain present.
- [ ] Run tests before production changes; expected PASS if implementation matches spec/correction. Treat failures as contract defects.
- [ ] Minimal correction only if required; no ABI/general framework.
- [ ] Static + sanitizer 23/23; commit `test: lock MediaBox trim batch determinism`.

---

### Task 8: Freeze exact head, same-SHA cross-platform proof, review, and STOP

**Files:** no intended source/test changes; metadata/comments only.

- [ ] Freeze exact candidate SHA; any file change invalidates proof; no amend/rebase/force-push.
- [ ] Audit net paths: only MediaBox spec + frame correction + plan, approved header/CMake, page-box common + crop adaptation, trim production/tests/fixtures. Reused fixtures/workflow YAML untouched.
- [ ] Forbidden audit:

```text
pdf_page_box_common.c: no put/del/graft/serializer/runtime mutation
pdf_trim_preflight.c: no writes/graft; no Bleed/Trim/Art parsing
pdf_trim.c: sole semantic key write PDF_NAME(MediaBox); no graft/content/annot/form/widget/appearance/JS/event mutation
crop path: CropBox regression unchanged; sole crop semantic key PDF_NAME(CropBox)
public header: no MuPDF types
```

- [ ] Fresh exact-head Linux PR: static 23/23 + ASan/UBSan 23/23.
- [ ] Add existing `full-ci` label to draft PR only; workflow unchanged. Require same frozen SHA, Linux static/sanitizer 23/23, macOS 23/23, Windows DLL 23/23. Windows log explicitly shows `extractpdf.dll`, `extractpdf_test_pdf_trim.exe`, `extractpdf.pdf_trim` as test 23/23, 100% of 23 tests.
- [ ] Final Critical/Important review verifies:

```text
current-source-Fitz MediaBox input
shrink-only MediaBox
source immutable / private reparse before writes
no source MuPDF pointer crosses contexts
only changed local MediaBox writes
no-op inherited MediaBox stays inherited
no-CropBox fallback re-anchors frame
physical-only trim => effective visible intersection unchanged => frame unchanged
clipping trim => effective visible intersection changed => frame re-anchored
clipped visible public origin is not incorrectly required to preserve old non-zero origin
output MediaBox may be negative/non-zero relative to visible frame
Rotate/UserUnit use common matrix
Bleed/Trim/Art opaque preservation
interactive/document-root structural preservation
encrypted/signed fail closed
output reset and deterministic outputs
CropBox #22 regression GREEN
trim #23 adds coverage without weakening prior tests
```

- [ ] Fetch reviews/threads; no unresolved Critical/Important blocker.
- [ ] Post exact SHA/workflow IDs/23-of-23/scope/review checkpoint to #51 + draft PR and STOP. Do not mark ready, merge, close #51, checkpoint #48/#2 as integrated, delete branch, or start poster split without explicit integration authorization.

---

### Task 9: Integrate only after explicit authorization

**Files:** no intended feature-tree changes.

- [ ] Re-read frozen feature head, current master, reviews, exact-head CI. If feature moved repeat Task 8; if master moved inspect, do not silently rebase.
- [ ] Prefer normal expected-head merge commit.
- [ ] If draft->ready connector GraphQL is still broken, only after explicit integration authorization use proven fallback:

```text
create two-parent merge commit
  tree = frozen feature tree
  parent1 = current verified master
  parent2 = frozen feature SHA
update master force=false
verify GitHub associates PR as merged
```

Never force-update master.
- [ ] Require integrated-master exact merge SHA proof: Linux static 23/23, ASan/UBSan 23/23, macOS 23/23, Windows DLL 23/23 including `extractpdf_test_pdf_trim.exe`.
- [ ] Only after integrated proof close #51 completed and checkpoint #48/#2.
- [ ] Do not auto-start poster split.
