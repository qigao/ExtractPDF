# ExtractPDF PDF Page Annotation Enumeration V1 Implementation Plan

> Use strict RED -> GREEN -> exact-head verification. Do not add production behavior before the RED failure is captured.

**Goal:** Add a PDF-only immutable ordinary-annotation snapshot with tolerant `/Annots` collection handling, strict surviving-item materialization, stable relative order, snapshot-local indices, independent lifetime, and atomic failure publication.

**Base:** integrated master `a83639752e629225b34b052d537a5d2e61220711`; branch `feat/pdf-annotations`; issue #35; roadmap #2.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotations-design.md`

## Locked constraints

- Reuse `extractpdf_page`; no second PDF page handle.
- PDF-only V1; non-PDF page -> `UNSUPPORTED`.
- Missing/non-array `/Annots` -> successful empty snapshot.
- Non-dictionary members ignored.
- Link/Popup/Widget filtered out.
- Missing/non-name/unrecognized subtype -> `UNKNOWN`.
- Surviving `/Rect` must be exactly four finite numbers.
- Missing `/F` -> 0; present `/F` must be uint32-representable integer.
- Missing `/Contents` -> absent; present `/Contents` must be a PDF string.
- Preserve original `/Annots` relative order among survivors.
- Snapshot indices are never persistent identities or mutation handles.
- Snapshot retains no MuPDF/PDF/page/document pointer.
- `*out_annotations` is reset to NULL before later validation/work and published only after complete success.
- Enumeration itself does not call annotation synchronization/resynthesis/update/mutation APIs.
- Use `pdf_page_transform()` CTM **directly** to map PDF rectangle coordinates to Fitz page space; do not invert it.

## File scope

RED:

```text
tests/fixtures/annotations-mixed.pdf
tests/fixtures/annotations-nonarray.pdf
tests/fixtures/annotations-filtered-only.pdf
tests/fixtures/annotations-late-malformed.pdf
tests/test_pdf_annotations.c
tests/CMakeLists.txt
```

GREEN production:

```text
include/extractpdf/extractpdf.h
src/pdf_annotations.c
CMakeLists.txt
```

No changes are expected in page, links, outline, metadata, render, text, image, composition, or output implementation.

---

## Task 1 — Strict RED

- [x] Create deterministic fixtures.
- [x] Add `tests/test_pdf_annotations.c` referencing the wished-for annotation ABI.
- [x] Register `extractpdf.pdf_annotations` in CMake, including Windows DLL-copy target list.
- [x] Verify exact RED failure before production code.

### Required RED fixture behavior

`annotations-mixed.pdf` logical order:

```text
Text-A
scalar 17
Link
FutureThing
Widget
Popup
Highlight-B
```

Expected survivors after GREEN:

```text
0 TEXT       Rect [10 20 30 40]       -> Fitz [10 160 30 180], flags=4,  contents="alpha"
1 UNKNOWN    Rect [50 60 70 80]       -> Fitz [50 120 70 140], flags=64, contents="unknown"
2 HIGHLIGHT  Rect [90 100 120 130]    -> Fitz [90 70 120 100], flags=0,  contents="bravo"
```

`annotations-nonarray.pdf`: `/Annots 17` -> `OK + non-NULL count 0`.

`annotations-filtered-only.pdf`: scalar + Link + Widget + Popup -> `OK + non-NULL count 0`.

`annotations-late-malformed.pdf`: valid Text followed by Highlight `/Contents 123` -> whole extraction `FORMAT + NULL`, repeatably.

Reuse existing `one-page.pdf` for missing `/Annots` empty case.

### RED acceptance

The library and every pre-existing target must build. Only the new annotation test target may fail, and it must fail because `extractpdf_annotation_page`, enum/info types, constants, and annotation APIs do not yet exist. Runtime/fixture errors are not valid RED.

Record exact RED SHA/run in PR #36 and issue #35.

---

## Task 2 — Minimal GREEN

### Public ABI

Add exactly:

```c
typedef struct extractpdf_annotation_page extractpdf_annotation_page;
typedef enum extractpdf_annotation_type { ... } extractpdf_annotation_type;
typedef struct extractpdf_annotation_info {
    size_t struct_size;
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
} extractpdf_annotation_info;

extractpdf_status extractpdf_extract_annotations(
    extractpdf_page *, extractpdf_annotation_page **);
extractpdf_status extractpdf_annotation_count(
    const extractpdf_annotation_page *, size_t *);
extractpdf_status extractpdf_annotation_get_info(
    const extractpdf_annotation_page *, size_t, extractpdf_annotation_info *);
extractpdf_status extractpdf_annotation_contents(
    const extractpdf_annotation_page *, size_t, const char **, size_t *);
void extractpdf_drop_annotation_page(extractpdf_annotation_page *);
```

Do not expose MuPDF enum values, PDF object numbers, `/NM`, or mutation identity.

### Private snapshot

`src/pdf_annotations.c` owns records equivalent to:

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

No back-pointer to source page/document is allowed.

### Pass 1: tolerant survivor count

From the existing `fz_page`, use `pdf_page_from_fz_page()`. NULL -> `UNSUPPORTED`.

Read `pdf_page->obj` `/Annots` raw. `pdf_array_len()` naturally gives zero for missing/non-array. For each element in array order:

```text
not dictionary -> skip
Subtype Link    -> skip
Subtype Popup   -> skip
Subtype Widget  -> skip
otherwise       -> survivor; known subtype maps explicitly, else UNKNOWN
```

Count survivors with size/allocation overflow guards.

### Pass 2: strict materialization

Obtain `pdf_page_transform(ctx, pdf_page, NULL, &page_ctm)` once. The returned CTM maps PDF page user space to Fitz page space and is applied directly.

For each survivor:

1. Locate `/Rect`; require array length 4 and numeric finite members.
2. Normalize PDF endpoints, apply `fz_transform_rect(raw_rect, page_ctm)`, verify finite result, normalize output endpoints.
3. `/F`: missing -> 0; present -> require integer in `[0, UINT32_MAX]`.
4. `/Contents`: missing -> absent; present -> require string, decode with `pdf_to_text_string()`, deep-copy into snapshot string arena with terminating NUL.
5. Any malformed survivor fails the whole extraction.

Use existing `extractpdf_status_from_mupdf()` for MuPDF exceptions. Allocation/size overflow -> `NOMEM`.

### Atomic publication and accessors

Extraction starts:

```c
if (out_annotations == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
*out_annotations = NULL;
```

Publish only after pass 2 completes and the materialized count equals the allocated survivor count.

`annotation_count`: reset output count to zero when possible before validation.

`annotation_get_info`: validate `struct_size` through `flags`; then neutral-reset type/bounds/flags before handle/index validation; accept larger structs.

`annotation_contents`: reset pointer/size to NULL/0 before validation; absent contents -> `OK + NULL + 0`; present contents -> borrowed snapshot-owned UTF-8.

`drop(NULL)` is safe.

### GREEN acceptance

On the exact GREEN SHA:

```text
Linux static build                PASS
all static CTests                 PASS
Linux ASan/UBSan build            PASS
all sanitizer CTests              PASS
```

Do not weaken tests to get GREEN.

---

## Task 3 — Final exact-head proof

Before final full CI:

- [ ] Correct any documentation/API implementation mismatch without changing locked semantics.
- [ ] Confirm diff scope contains only spec, plan, four fixtures, one annotation test, test/root CMake, public header, and `src/pdf_annotations.c`.
- [ ] Review explicitly against: malformed/tolerance, empty snapshot, order, identity, error atomicity.

Trigger the repository's `full-ci` PR label on the **final unchanged head**. Require:

```text
Linux static + ASan/UBSan  PASS
macOS build/test            PASS
Windows DLL build/test      PASS
```

Windows must execute `extractpdf.pdf_annotations`, proving export/link/runtime behavior through the shared-library build.

After all jobs pass, update PR #36 and issue #35 with:

- RED exact SHA + workflow/run and expected failure;
- GREEN/final exact SHA + Linux workflow/run;
- same-head full-ci workflow/run;
- exact diff scope;
- fresh review result for the five locked boundaries.

Keep annotation mutation and forms/widgets deferred to separate architecture work.
