# ExtractPDF Contiguous PDF Page Range Export Design

Date: 2026-08-28  
Status: approved design  
Tracks: #21, umbrella #2  
Stacked base: #19 / PR #20 head `d7d3e2a3c0ead330d0f8c97dd2bab7cd695f0012`  
Branch: `feat/pdf-export-range`

## Goal

Add a thin public helper for exporting one forward contiguous page range to one immutable PDF output while keeping `extractpdf_export_pages(...)` as the only composition engine.

This slice supplies the public range primitive needed for page-range split workflows without introducing output arrays, filenames, a second graft path, or new output ownership rules.

## Architectural decision

V1 exposes one **single-output** range helper:

```c
EXTRACTPDF_API extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output);
```

The helper converts a contiguous range into the same explicit zero-based index sequence already consumed by:

```c
extractpdf_export_pages(document, indices, page_count, out_output);
```

It must not call MuPDF PDF composition APIs directly.

Conceptually:

```text
extractpdf_export_page_range(document, 1, 2, &out)
        |
        v
validate mapping-safe arguments
        |
        v
indices = [1, 2]
        |
        v
extractpdf_export_pages(document, indices, 2, &out)
        |
        v
existing PDF graft / serialization / immutable output engine
```

## Why `first_page + page_count`

Three public shapes were considered:

1. `first_page + page_count`;
2. `[start_page, end_page_exclusive)`;
3. a versioned `extractpdf_page_range` struct.

V1 chooses `first_page + page_count` because it maps directly to split semantics:

```text
single page      (3, 1)
three pages      (3, 3) -> [3, 4, 5]
whole document   (0, document_page_count)
```

There is no inclusive/exclusive endpoint convention for callers to remember. A struct would add ABI surface without any range options that need future extension.

## Public semantics

`first_page` is zero-based, matching `extractpdf_load_page` and `extractpdf_export_pages`.

`page_count` is the number of consecutive pages to export.

The range is always **forward and contiguous**:

```text
(first_page = 2, page_count = 3)
=> [2, 3, 4]
```

V1 does not express:

- descending ranges;
- stepped ranges;
- disjoint ranges;
- duplicated pages inside a range.

Those cases already belong to the more general `extractpdf_export_pages(...)` index-list primitive.

## Split model

V1 deliberately does not return an array/list of `extractpdf_output` objects.

Per-page split is expressed by repeated single-page calls:

```c
for (int page = 0; page < count; ++page)
    extractpdf_export_page_range(document, page, 1, &output);
```

Page-range split is the same primitive with a larger `page_count`.

This keeps:

- output ownership identical to #19 / PR #20;
- partial-success policy outside the core ABI;
- filename/output naming policy with the caller;
- one output per call;
- error reporting one operation at a time.

No `extractpdf_output_list`, output array, batch callback, or partial-result collection is introduced.

## Validation responsibilities

The range layer validates only what is required to map its public arguments safely into an `int[]` index sequence.

When `out_output != NULL`, the helper sets `*out_output = NULL` before further validation.

It returns `EXTRACTPDF_ERROR_ARGUMENT` for syntactically or arithmetically invalid requests:

- `out_output == NULL`;
- `document == NULL`;
- `first_page < 0`;
- `page_count == 0`;
- `page_count > INT_MAX`;
- `page_count > SIZE_MAX / sizeof(int)`;
- `first_page + page_count - 1` cannot be represented as an `int`.

The implementation must not compute `first_page + page_count - 1` before proving it cannot overflow. With `page_count <= INT_MAX`, the safe check is equivalent to:

```c
size_t offset = page_count - 1;
if ((size_t)first_page > (size_t)INT_MAX - offset)
    return EXTRACTPDF_ERROR_ARGUMENT;
```

Only after those checks may it allocate/fill the index array.

## Document-dependent page bounds

The helper does **not** create a second source-page-count validation path.

After safe expansion, it delegates the index list to `extractpdf_export_pages(...)`, which already validates every requested index against the source PDF page count before grafting any page.

Therefore a PDF request such as:

```text
source has 3 pages
first_page = 2
page_count = 2
indices = [2, 3]
```

is rejected by the existing engine with `EXTRACTPDF_ERROR_ARGUMENT` and no partial output.

This preserves one authoritative PDF page-boundary contract.

## Non-PDF precedence

The helper performs its format-independent argument/arithmetic checks first.

For an otherwise mapping-valid request on a successfully opened non-PDF document, delegation reaches `extractpdf_export_pages(...)`, which returns `EXTRACTPDF_ERROR_UNSUPPORTED`.

Document-dependent page-range validity is intentionally not defined for non-PDF composition because the composition operation itself is unsupported. This matches the existing engine's PDF-specific boundary rather than adding a competing generic range-count check.

## Allocation and ownership

The helper allocates only one temporary `int` array:

```text
int indices[page_count]   // dynamic allocation
```

The array contains:

```c
indices[i] = first_page + (int)i;
```

After calling `extractpdf_export_pages(...)`, the helper frees the temporary array on both success and failure.

The range helper never owns or retains:

- `extractpdf_document`;
- `extractpdf_output` after returning it;
- MuPDF context/document/output/buffer objects.

The returned `extractpdf_output` has exactly the ownership/lifetime contract already defined by #19 / PR #20: immutable ExtractPDF-owned PDF bytes, independent of the source document after the export call succeeds.

`EXTRACTPDF_ERROR_NOMEM` is returned if the temporary index-array allocation fails.

## One composition engine rule

The implementation for this slice must not include `<mupdf/pdf.h>` and must not call any MuPDF composition primitive.

In particular, the range helper must not call:

```text
pdf_specifics
pdf_create_document
pdf_new_graft_map
pdf_graft_page
pdf_graft_mapped_page
pdf_write_document
pdf_save_document
fz_new_output_with_buffer
```

All PDF-specific behavior remains in the existing `extractpdf_export_pages(...)` implementation.

The recommended responsibility split is:

```text
src/pdf_range.c
    range argument validation
    overflow-safe contiguous index expansion
    delegate to extractpdf_export_pages
    free temporary indices

src/pdf_export.c
    unchanged composition engine from #19 / PR #20
```

No existing Page/Render/Text/Search/Image/Links implementation changes are allowed.

## Deterministic TDD fixture

Reuse `tests/fixtures/composition-three-page.pdf` from #19 / PR #20:

```text
page 0: PAGE-A, 200 x 200 pt
page 1: PAGE-B, 240 x 180 pt
page 2: PAGE-C, 300 x 150 pt
```

No new PDF fixture is needed.

## Primary range proof

The main case is:

```text
extractpdf_export_page_range(document, 1, 2, &range_output)
```

Expected reopened result:

```text
page_count = 2
page 0: PAGE-B, 240 x 180 pt
page 1: PAGE-C, 300 x 150 pt
```

This proves start/count mapping and order.

## Engine-equivalence proof

The same test also calls:

```c
int indices[] = {1, 2};
extractpdf_export_pages(document, indices, 2, &index_output);
```

Under the same pinned build, bytes returned by `range_output` and `index_output` must be byte-for-byte identical.

This equality is valuable architecture evidence: the public range helper is only an index-mapping layer and reaches the exact existing deterministic serialization engine rather than a second implementation.

The equality contract is test-local to the same pinned build, matching the existing #19 deterministic-output scope; it does not create a cross-version or cross-platform byte-identity promise.

## Additional success cases

The deterministic test covers:

```text
(2, 1) -> [PAGE-C]
(0, 3) -> [PAGE-A, PAGE-B, PAGE-C]
```

The single-page case proves per-page split semantics. The whole-range case proves that a contiguous range can represent the complete document without special casing.

At least the primary `(1, 2)` result is reopened through the existing filename-based `extractpdf_open` test path to verify page count, searchable text, and geometry. Additional cases may use byte equality against equivalent `extractpdf_export_pages` calls to avoid redundant temporary-file machinery.

## Failure cases

Required contract tests include:

1. `document == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`, output reset;
2. `out_output == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`;
3. `first_page < 0` -> `EXTRACTPDF_ERROR_ARGUMENT`, output reset;
4. `page_count == 0` -> `EXTRACTPDF_ERROR_ARGUMENT`, output reset;
5. `(2, 2)` against the 3-page PDF -> `EXTRACTPDF_ERROR_ARGUMENT`, output reset;
6. `first_page = INT_MAX, page_count = 2` -> arithmetic-overflow `EXTRACTPDF_ERROR_ARGUMENT` before allocation/delegation;
7. mapping-valid range on `composition-non-pdf.txt` -> `EXTRACTPDF_ERROR_UNSUPPORTED`, output reset;
8. successful returned output remains governed by the existing `extractpdf_output_data` / `extractpdf_drop_output` lifecycle.

A platform-specific test for `page_count > INT_MAX` is only added where `SIZE_MAX > INT_MAX`; the implementation guard is required on all platforms.

## TDD boundary

The first RED change is range-test-only:

- create `tests/test_pdf_range.c`;
- wire one new CTest target;
- reuse the two existing composition fixtures;
- do not add the public declaration or production helper yet.

The expected RED is compile failure in only the new test because `extractpdf_export_page_range` is absent from the stacked #20 public header. Existing #20 production code and tests must continue to build.

The minimal GREEN then adds only:

- one public function declaration;
- focused `src/pdf_range.c`;
- root CMake source registration.

`src/internal.h` and `src/pdf_export.c` should remain unchanged unless a concrete compiler/test failure proves a minimal shared change is required. No speculative refactor is permitted.

## Stacked PR strategy

This slice is stacked because #20 is still draft/unmerged.

```text
master
  |
  +-- feat/pdf-export-pages   PR #20
          |
          +-- feat/pdf-export-range   #21 / new stacked PR
```

The range PR base is `feat/pdf-export-pages`, not `master`, until #20 is integrated.

Its true feature diff must therefore remain narrow: range spec/test/API/helper/CMake only.

If #20 is later merged first, the range PR may be retargeted to `master` only after verifying that the stacked branch is a descendant or reconciling history without changing the range feature tree.

## CI policy

Development remains Linux-first:

1. RED exact-head workflow must fail only at the missing range API boundary;
2. GREEN exact-head Linux strict build + all normal CTests must pass;
3. Linux ASan/UBSan build + CTests must pass;
4. macOS/Windows may remain deferred to a later Phase 4 checkpoint because #20 already established the architecture-critical output/PDF-private-linkage checkpoint;
5. an earlier range full-ci run is allowed if stack retargeting or Windows shared-library export behavior changes.

No success claim may reuse a workflow from an earlier head.

## Files and responsibilities

Expected range-slice files:

```text
include/extractpdf/extractpdf.h
    add one public range declaration

src/pdf_range.c
    validate mapping-safe range arguments
    expand contiguous indices
    delegate to extractpdf_export_pages
    free temporary array

CMakeLists.txt
    register src/pdf_range.c

tests/test_pdf_range.c
    range semantics, engine equivalence, errors

tests/CMakeLists.txt
    register CTest / Windows DLL copy target
```

Existing composition fixtures are reused unchanged.

## Non-goals

This slice does not add:

- multiple outputs in one call;
- output arrays/lists;
- automatic filename generation;
- direct file saving;
- descending or stepped range syntax;
- string page-range parsing;
- multi-document merge;
- page deletion or source mutation;
- a second MuPDF composition engine;
- new output ownership semantics;
- memory-open API;
- Phase 5 link/annotation/form preservation;
- new concurrency guarantees.
