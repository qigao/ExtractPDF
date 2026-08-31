# Poster Split V1 Implementation Plan Correction

Plan: `docs/superpowers/plans/2026-08-30-quantapdf-poster-split.md`
Specs: `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-design.md` and `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-preflight-correction.md`
Baseline: `master@eca8179e59723e2e6dfd3b3acbdedc61c15bf350`

## Status

This correction is normative for implementation-plan execution. It was produced during the required plan self-review, before RED tests, ABI changes, or production implementation.

It does not change the approved public API or Poster Split behavior. It removes execution ambiguities and one invalid internal data-shape assumption in the first plan commit.

Where this file conflicts with `2026-08-30-quantapdf-poster-split.md`, this file wins.

## 1. Crossing annotation/link tile membership is an explicit index list

The original Task 4 sketch used:

```c
size_t first_tile_index;
size_t tile_count;
```

That representation is invalid for a rectangle that intersects multiple rows without spanning every column. Example with a 3-column grid: a rectangle can intersect row-major tile indices `{1, 4}`, which are not a contiguous numeric range.

Use this plan-owned representation instead:

```c
typedef struct quantapdf_pdf_poster_annot_plan {
    size_t source_annot_index;
    quantapdf_pdf_poster_annot_kind kind;
    quantapdf_rect source_public_rect;
    size_t *tile_indices;
    size_t tile_count;
} quantapdf_pdf_poster_annot_plan;
```

Rules:

- `tile_indices` is allocated from semantic plan memory, not a MuPDF context.
- entries are exact row-major tile indices with positive-area intersection.
- entries are strictly increasing because the classifier scans tiles row-major.
- ordinary annotation and Widget require `tile_count == 1`.
- a contained Link has `tile_count == 1`.
- a crossing Link has `tile_count >= 2` and may contain non-contiguous numeric indices.
- `quantapdf_pdf_poster_drop_plan()` frees every annotation `tile_indices` array.
- source/private plan equivalence compares the complete ordered tile-index list.

Replace the Task 4 rectangle helper with:

```c
quantapdf_status quantapdf_pdf_poster_collect_rect_tiles(
    const quantapdf_pdf_poster_split_plan *split,
    quantapdf_rect rect,
    size_t **out_tile_indices,
    size_t *out_tile_count,
    int *out_crosses);
```

The helper initializes outputs to `NULL/0/0`, allocates only after the positive intersections have been counted, and returns `NOMEM` on checked allocation failure. Task 6 uses `annotation_plan->tile_indices[k]` directly when choosing the original Link's first tile and later clone tiles.

## 2. Strict outline walker extraction is mandatory in Task 4

Do not duplicate the existing strict outline structural validator inside Poster Split and do not leave the extraction conditional.

Task 4 MUST create:

```text
src/pdf_outline_common.h
src/pdf_outline_common.c
```

and minimally refactor:

```text
src/pdf_outline.c
```

The private interface is:

```c
typedef quantapdf_status (*quantapdf_pdf_outline_visit_fn)(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *item,
    size_t preorder_index,
    void *user);

quantapdf_status quantapdf_pdf_outline_walk_strict(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_outline_visit_fn visit,
    void *user,
    size_t *out_count);
```

The extracted walker owns the existing iterative depth/cycle/Parent/Prev/Last checks and the `depth > 256 -> UNSUPPORTED` rule. `pdf_outline.c` uses it for the existing snapshot preflight; `pdf_poster_navigation.c` uses the same walker for destination/action classification.

Required gate immediately after extraction:

```text
existing quantapdf.pdf_outline test remains green with no public behavior change
```

The final feature scope therefore always includes `pdf_outline_common.[ch]` plus the minimal `pdf_outline.c` delegation diff.

## 3. Task 3 test-target modularization is exact

When Task 3 introduces the modular Poster Split test runner, replace the Task 1 one-source target with:

```cmake
add_executable(quantapdf_test_pdf_poster_split
  test_pdf_poster_split.c
  test_pdf_poster_split_geometry.c
  test_pdf_poster_split_main.c)
set_source_files_properties(test_pdf_poster_split.c PROPERTIES
  COMPILE_DEFINITIONS "main=quantapdf_pdf_poster_split_base_main")
target_link_libraries(quantapdf_test_pdf_poster_split PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_poster_split PRIVATE
  POSTER_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-basic.pdf"
  POSTER_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-rotate-90.pdf"
  POSTER_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-userunit.pdf"
  SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
```

Later tasks add modules with `target_sources(quantapdf_test_pdf_poster_split PRIVATE ...)` and add fixture compile definitions to the same target. Task 5 adds MuPDF privately for raw assertions:

```cmake
target_link_libraries(quantapdf_test_pdf_poster_split PRIVATE
  unofficial::libmupdf::libmupdf)
```

There remains exactly one CTest named `quantapdf.pdf_poster_split`.

## 4. Basic row-major proof uses clipped render equivalence, not text extraction clipping

The static basic fixture may keep visible quadrant text markers, but Task 5 MUST NOT assume structured/plain text extraction clips objects outside a tile solely because the output page box changed.

The normative row-major proof is:

```text
source page 1 clipped render(tile public 0) == output page 1 full render
source page 1 clipped render(tile public 1) == output page 2 full render
source page 1 clipped render(tile public 2) == output page 3 full render
source page 1 clipped render(tile public 3) == output page 4 full render
```

Page 0 and the final page may still use `POSTER-BEFORE` / `POSTER-AFTER` text markers to prove surrounding page order.

This avoids accidentally baking an unapproved content-extraction clipping contract into Poster Split.

## 5. Global `/Annots` subtype/action classification is explicit

For the document-wide action-safety scan during any real split:

- `/Annots` missing -> allowed.
- `/Annots` present but not array -> `FORMAT`.
- an array entry that cannot be treated as an indirect annotation dictionary -> `FORMAT`.
- missing or non-name `/Subtype` -> `FORMAT`, because V1 cannot prove whether Link-specific `/Dest` semantics apply.
- known `/Link` -> classify `/Dest`, `/A`, `/AA` under the Link registry.
- any non-Link annotation -> `/A` or `/AA` anywhere is `UNSUPPORTED`.
- an unknown but syntactically valid non-Link subtype on an unselected page is allowed only when it has no `/Dest`, `/A`, or `/AA`; on a selected split page it remains `UNSUPPORTED` because migration semantics are undefined.

This is stricter than immutable annotation enumeration by design.

## 6. Outline URI actions remain supported preservation state

Task 7 Outline action classification accepts:

```text
no destination/action
/Dest <supported local destination>
/A << /S /GoTo /D <supported local destination> >>
/A << /S /URI ... >>
```

URI action bytes/state are preserved; no local page remap occurs. Other action kinds and `/Next` chains remain `UNSUPPORTED`.

## 7. Local destination page operand must resolve to an actual page object

For local destination arrays inspected by the V1 registry:

- array element 0 must resolve to an indirect `/Page` object in the current document.
- `pdf_lookup_page_number(ctx, document, page_operand)` must return a valid source page index.
- an integer page operand or an object that is not a current page is `FORMAT` for local V1 navigation.

This prevents accidental acceptance of remote-destination integer page-number conventions as local GoTo semantics.

## 8. Private runtime locators resolve before page-tree splice

Task 5's private changed path has this mutation order after source/private plan equivalence:

```text
1. resolve and keep every original selected source Page object in private runtime state
2. resolve private annotation/action/destination owner objects required by semantic locators
3. create every tile Page indirect object, not yet inserted
4. build tile Annots and apply annotation/Widget/Link membership changes
5. apply supported destination page-reference rewrites while original source Pages still exist
6. splice selected Pages in descending original index order
7. delete each original Page from the page tree
8. serialize
```

No Task 6/7 helper may look up a selected original Page by its original numeric index after any page-tree splice has begun. Runtime `pdf_obj *` references are private-context-only and may be kept across the private mutation sequence; semantic source/private equivalence still uses no pointer identity.

This ordering supersedes any wording in the original plan that could be read as resolving owner objects after insertion/deletion starts.

## 9. `UserUnit` and `Rotate` materialization policy for tile Pages

For generated tile Pages:

- write `/Rotate` only when effective normalized Rotate is non-zero.
- write `/UserUnit` only when the source selected page has a valid page-local UserUnit whose numeric value is not exactly `1.0f`.
- do not invent inherited UserUnit semantics; UserUnit remains page-local per the integrated resolver contract.

The public result is authoritative; source absence vs explicit default-value presence is not promised for new tile Page dictionaries.

## 10. Final execution-unit rule

Implementation execution MUST read, in this order:

```text
1. docs/superpowers/specs/2026-08-30-quantapdf-poster-split-design.md
2. docs/superpowers/specs/2026-08-30-quantapdf-poster-split-preflight-correction.md
3. docs/superpowers/plans/2026-08-30-quantapdf-poster-split.md
4. docs/superpowers/plans/2026-08-30-quantapdf-poster-split-plan-correction.md
```

Task numbering and integration gates remain unchanged: Tasks 1-9 execute the feature and stop before integration; Task 10 requires explicit integration authorization.
