# QuantaPDF Poster Split V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable Poster Split transform that expands selected PDF pages into deterministic row-major `columns × rows` tile pages while preserving supported content, annotations, Widgets, AcroForm relationships, and local navigation under the approved fail-closed V1 policy.

**Architecture:** Build on the integrated CropBox/MediaBox page-box resolver and full-document immutable rewrite substrate. Source and private-copy preflight produce semantically equivalent split plans; the private writer creates new `/Page` objects inside the same PDF graph, shares content/resource objects, migrates supported `/Annots`, remaps the approved destination registry, then removes the original page objects and serializes once. Never use `pdf_graft_mapped_page()` as the Poster Split preservation substrate.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, existing deterministic PDF serializer, Linux GCC/Clang sanitizers, macOS ARM64, Windows MSVC DLL CI.

**Spec:** `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-design.md`

**Normative correction:** `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-preflight-correction.md`

## Global Constraints

- Baseline is integrated `master@eca8179e59723e2e6dfd3b3acbdedc61c15bf350`, tree `7454c7bd2029bad2f6fe595c66b301b431c8815f`, with 23 CTests.
- Public Poster Split coordinates use the existing QuantaPDF/Fitz public page space; raw PDF coordinates are never a second row/column convention.
- Tile order is row-major: top-to-bottom, left-to-right in source public page space.
- Tile page `/MediaBox` and `/CropBox` are the tile raw rectangle; content streams are not translated or rewritten.
- Source `Rotate` and page-local `UserUnit` semantics are reproduced through the existing MuPDF page transform, not manual per-object geometry rules.
- Real page expansion uses a complete private copy of the source graph; no source `pdf_obj *`, `pdf_page *`, or source-context allocation crosses into the private context.
- `pdf_graft_mapped_page()` is forbidden as the Poster Split implementation substrate.
- New tile `/Page` dictionaries use an allowlist; do not copy the complete source page dictionary.
- `/Contents`, effective `/Resources`, local `/Group`, and local `/Tabs` may be shared inside the same private document.
- Selected-page effective `/BleedBox`, `/TrimBox`, or `/ArtBox` makes a real split `QUANTAPDF_ERROR_UNSUPPORTED`.
- A real split of tagged PDF (`Catalog /StructTreeRoot` or selected `/StructParents`) is `QUANTAPDF_ERROR_UNSUPPORTED`.
- Real split rejects catalog `/OpenAction`, `/AA`, `/PageLabels`, `/Threads`; page `/AA` anywhere; AcroForm `/XFA`; field/Widget actions; unsupported annotation/action graphs.
- Supported destination registry is exactly Link `/Dest`, Link `/A /GoTo /D`, Outline `/Dest`, Outline `/A /GoTo /D`, Names/Dests definitions, and legacy catalog `/Dests` definitions.
- A destination targeting a split page is supported only as explicit `/XYZ` with finite numeric x and y; tile ownership is decided after mapping that raw point through source `pdf_to_public` into the public grid.
- Destination rewrite changes only the target page reference; raw `/XYZ` x/y/zoom stay unchanged.
- URI links may be cloned across tile boundaries; ordinary annotations and Widgets may not be cloned.
- Ordinary annotation/Widget crossing a grid boundary is `QUANTAPDF_ERROR_UNSUPPORTED`.
- Supported contained ordinary annotations and Widgets keep the same indirect object and are moved to exactly one tile; Widget field/cardinality/value semantics remain unchanged.
- Encrypted and already-signed PDFs fail closed, including all-`1×1` calls; JavaScript is disabled in the private graph.
- An all-`1×1` batch is a semantic no-op: perform public/security/page-box validation, skip expansion-only restrictions, canonical serialize once, and return deterministic immutable bytes.
- Every failure resets `*out_output == NULL`; source document remains immutable.
- Final suite target is exactly 24 CTests, adding `quantapdf.pdf_poster_split` while preserving all existing 23 tests.
- No workflow YAML changes are authorized merely to obtain proof. Use the existing `full-ci` label mechanism for same-SHA Linux/macOS/Windows proof.
- Task 9 stops before integration. Task 10 is executable only after an explicit user integration authorization such as `go integrate`.

---

## File Structure Locked by This Plan

### Production

- `include/quantapdf/quantapdf.h` — public request struct and immutable Poster Split function only; no MuPDF types.
- `src/pdf_poster_internal.h` — Poster Split semantic plan structs and private interfaces.
- `src/pdf_poster_preflight.c` — public arguments, security, page-count/grid geometry, page-key policy, no-op classification, plan ownership/equality.
- `src/pdf_poster_annotations.c` — global `/Annots` action safety, selected-page Link/ordinary annotation/Widget migration classification, AcroForm provenance checks.
- `src/pdf_poster_navigation.c` — strict supported destination/action registry scan, named-destination traversal, public-grid destination tile selection, private destination rewrite.
- `src/pdf_poster.c` — full-document private-copy orchestration, tile `/Page` construction, page-tree splice, annotation migration, deterministic serialization.
- `src/pdf_outline_common.h` / `src/pdf_outline_common.c` — only if required by Task 4: extract the existing strict outline structural walk so `pdf_outline.c` and Poster Split share one authoritative validator; do not change public outline semantics.
- `src/pdf_outline.c` — only the minimal delegation needed if the strict walker is extracted.
- `CMakeLists.txt` — compile new Poster Split production/common sources.

### Tests

- `tests/test_pdf_poster_split.c` — ABI/error/no-op base contract.
- `tests/test_pdf_poster_split_internal.h` — test-module interfaces.
- `tests/test_pdf_poster_split_raw.c` — MuPDF-private semantic/raw graph assertions.
- `tests/test_pdf_poster_split_geometry.c` — basic tiling, page order, render equivalence, Rotate/UserUnit.
- `tests/test_pdf_poster_split_policy.c` — page/root/action/tagged/print-production fail-closed matrix.
- `tests/test_pdf_poster_split_interactive.c` — URI Links, crossing Link clones, contained annotation, contained Widget/AcroForm.
- `tests/test_pdf_poster_split_navigation.c` — internal Links, Outlines, Names/Dests, legacy `/Dests`, grid-edge/Rotate/UserUnit destination ownership.
- `tests/test_pdf_poster_split_batch.c` — multiple requests, request-order normalization, atomicity, determinism, lifetime.
- `tests/test_pdf_poster_split_main.c` — one CTest entry that runs all Poster Split modules.
- `tests/CMakeLists.txt` — target, fixtures, MuPDF-private test link, Windows DLL copy.

### Fixtures

Commit static deterministic PDF fixtures. They may be generated once while authoring the change, but the repository must contain the resulting PDF bytes; test execution must not depend on Python or a fixture generator.

- `tests/fixtures/poster-basic.pdf` — three pages; middle page 400×300 with quadrant-distinguishing vector/text content and no interactive state.
- `tests/fixtures/poster-rotate-90.pdf` — 400×300 visible page with Rotate 90 and quadrant markers.
- `tests/fixtures/poster-userunit.pdf` — page-local UserUnit 2 with quadrant markers.
- `tests/fixtures/poster-interactive.pdf` — split page plus control page; contained URI Link, crossing URI Link, contained ordinary annotation, contained Widget, AcroForm field/value, unselected-page internal Link, Outline `/XYZ`, Names/Dests `/XYZ`, and an image/text payload.
- `tests/fixtures/poster-legacy-dests.pdf` — legacy catalog `/Dests` definition targeting split page.
- `tests/fixtures/poster-crossing-annotation.pdf` — valid ordinary annotation crossing one public grid edge.
- `tests/fixtures/poster-crossing-widget.pdf` — valid Widget crossing one public grid edge.
- `tests/fixtures/poster-production-box.pdf` — selected page with explicit/effective BleedBox/TrimBox/ArtBox.
- `tests/fixtures/poster-tagged.pdf` — catalog `/StructTreeRoot` plus selected `/StructParents`.
- `tests/fixtures/poster-root-policy.pdf` — isolated variants or pages exposing `/OpenAction`, catalog `/AA`, `/PageLabels`, `/Threads`, or page `/AA` as needed by the policy module.
- `tests/fixtures/poster-unselected-actions.pdf` — unselected-page non-Link annotation action and page `/AA` cases.
- `tests/fixtures/poster-form-actions.pdf` — AcroForm `/XFA`, field `/A`/`/AA`, Widget `/A`/`/AA` variants.
- `tests/fixtures/poster-malformed-annots.pdf` — malformed unselected `/Annots` needed by the global scan.
- `tests/fixtures/poster-malformed-destination.pdf` — malformed supported registry destination forms.

Re-use existing `annotation-mutation-signed.pdf`, `encrypted-one-page.pdf`, `composition-non-pdf.txt`, and the existing malformed box/Rotate/UserUnit fixtures where their exact state already matches the required contract.

---

### Task 1: Strict Poster Split Compile RED and Draft PR

**Files:**
- Create: `tests/test_pdf_poster_split.c`
- Create: `tests/fixtures/poster-basic.pdf`
- Modify: `tests/CMakeLists.txt`
- Do not modify: public header, `src/`, root `CMakeLists.txt`, workflows.

**Interfaces:**
- Consumes: current public API at `master@eca8179e...`.
- Produces: one new CTest target whose only build failure is the intentionally absent approved Poster Split ABI.

- [ ] **Step 1: Add the static basic fixture**

Create a valid three-page PDF with these semantic page payloads:

```text
page 0: 200×200, text marker "POSTER-BEFORE"
page 1: 400×300, no CropBox, Rotate 0, UserUnit absent
        top-left     marker "POSTER-00"
        top-right    marker "POSTER-01"
        bottom-left  marker "POSTER-10"
        bottom-right marker "POSTER-11"
        each quadrant also contains a distinct filled rectangle so render comparison is not text-only
page 2: 200×200, text marker "POSTER-AFTER"
```

Do not include `/Annots`, `/AcroForm`, `/Outlines`, `/Names`, Bleed/Trim/Art, tagged structure, actions, page metadata, or unknown page keys in this basic fixture.

- [ ] **Step 2: Add the intentionally uncompilable public test**

`tests/test_pdf_poster_split.c` starts with the same `CHECK`/sentinel style as crop/trim tests and contains this valid-call contract:

```c
#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    quantapdf_page_poster_split split;

    CHECK(quantapdf_open(POSTER_BASIC_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);

    split.struct_size = sizeof(split);
    split.page_index = 1;
    split.columns = 2;
    split.rows = 2;

    CHECK(quantapdf_poster_split_pages(document, &split, 1, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);

    quantapdf_drop_output(output);
    quantapdf_close(document);
    return 0;
}
```

- [ ] **Step 3: Register only the new test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(quantapdf_test_pdf_poster_split test_pdf_poster_split.c)
target_link_libraries(quantapdf_test_pdf_poster_split PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_poster_split PRIVATE
  POSTER_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-basic.pdf")
add_test(NAME quantapdf.pdf_poster_split COMMAND quantapdf_test_pdf_poster_split)
set_tests_properties(quantapdf.pdf_poster_split PROPERTIES TIMEOUT 60)

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET quantapdf_test_pdf_poster_split POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:quantapdf>
      $<TARGET_FILE_DIR:quantapdf_test_pdf_poster_split>
    VERBATIM)
endif()
```

- [ ] **Step 4: Commit strict RED**

```bash
git add tests/test_pdf_poster_split.c tests/fixtures/poster-basic.pdf tests/CMakeLists.txt
git commit -m "test: add Poster Split compile contract"
```

- [ ] **Step 5: Open a draft PR before production exists**

Create a draft PR from `feat/poster-split` to `master`, track `#53`, link both committed specs, and explicitly state that the current head is expected RED.

- [ ] **Step 6: Run exact-head Linux CI and record attributable RED**

Expected outcome:

```text
existing 23 test executables compile as before
new target fails to compile only because:
  quantapdf_page_poster_split is unknown
  quantapdf_poster_split_pages is undeclared
```

If any pre-existing target fails, stop and debug that regression before continuing.

---

### Task 2: Public ABI Shell and Runtime RED

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_poster.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_poster_split.c`

**Interfaces:**
- Produces public ABI exactly:

```c
typedef struct quantapdf_page_poster_split {
    size_t struct_size;
    int page_index;
    size_t columns;
    size_t rows;
} quantapdf_page_poster_split;

QUANTAPDF_API quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output);
```

- [ ] **Step 1: Add the approved request type beside crop/trim request types**

Use the exact field order above. No flags/options enum is added in V1.

- [ ] **Step 2: Add the approved function declaration beside other immutable PDF transforms**

Keep MuPDF types out of the public header.

- [ ] **Step 3: Add the minimal shell**

`src/pdf_poster.c` initially contains only public argument reset/basic validation and returns `UNSUPPORTED` for a syntactically valid call:

```c
#include "pdf_internal.h"

quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        splits == NULL || split_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    return QUANTAPDF_ERROR_UNSUPPORTED;
}
```

- [ ] **Step 4: Add `src/pdf_poster.c` to the library target**

Place it beside `pdf_crop.c` / `pdf_trim.c` in root `CMakeLists.txt`.

- [ ] **Step 5: Verify runtime RED**

Expected exact behavior:

```text
all 24 executables build
CTest #1-#23 pass
CTest #24 quantapdf.pdf_poster_split fails at the valid 2×2 call because shell returns UNSUPPORTED
```

- [ ] **Step 6: Commit the ABI shell**

```bash
git add include/quantapdf/quantapdf.h src/pdf_poster.c CMakeLists.txt
git commit -m "feat: add Poster Split ABI shell"
```

---

### Task 3: Request Geometry, Security, No-op, and Semantic Plan

**Files:**
- Create: `src/pdf_poster_internal.h`
- Create: `src/pdf_poster_preflight.c`
- Modify: `src/pdf_poster.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_pdf_poster_split.c`
- Create: `tests/test_pdf_poster_split_internal.h`
- Create: `tests/test_pdf_poster_split_geometry.c`
- Create: `tests/test_pdf_poster_split_main.c`
- Create: `tests/fixtures/poster-rotate-90.pdf`
- Create: `tests/fixtures/poster-userunit.pdf`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `quantapdf_pdf_page_box_resolve()` from `pdf_page_box_common.h`.
- Produces:

```c
typedef struct quantapdf_pdf_poster_tile_plan {
    size_t row;
    size_t column;
    size_t tile_index;
    quantapdf_rect public_rect;
    fz_rect pdf_rect;
} quantapdf_pdf_poster_tile_plan;

typedef struct quantapdf_pdf_poster_split_plan {
    int page_index;
    size_t columns;
    size_t rows;
    size_t tile_count;
    quantapdf_pdf_page_box_view page;
    quantapdf_pdf_poster_tile_plan *tiles;
    int changed;
} quantapdf_pdf_poster_split_plan;

typedef struct quantapdf_pdf_poster_plan {
    quantapdf_pdf_poster_split_plan *splits;
    size_t split_count;
    int source_page_count;
    int output_page_count;
    int any_changed;
} quantapdf_pdf_poster_plan;

quantapdf_status quantapdf_pdf_poster_check_security(
    fz_context *ctx,
    pdf_document *document);

quantapdf_status quantapdf_pdf_poster_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    int expansion_policy,
    quantapdf_pdf_poster_plan **out_plan);

int quantapdf_pdf_poster_plan_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right);

void quantapdf_pdf_poster_drop_plan(quantapdf_pdf_poster_plan *plan);
```

`expansion_policy=0` performs common request/page-box geometry only; later tasks add expansion-only policy under `expansion_policy=1` without changing the no-op contract.

- [ ] **Step 1: Expand the base test with deterministic argument/reset coverage**

Add helpers analogous to trim:

```c
static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static quantapdf_page_poster_split make_split(
    int page_index, size_t columns, size_t rows)
{
    quantapdf_page_poster_split split;
    split.struct_size = sizeof(split);
    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    return split;
}
```

Test all of:

```text
NULL out_output
NULL document
NULL splits
split_count == 0
short struct_size
page_index < 0
page_index == page_count
duplicate page_index
columns == 0
rows == 0
columns*rows overflow
final page-count overflow path using a dimension value that overflows before allocation
```

Every failure must reset the sentinel to `NULL` when an output pointer is supplied.

- [ ] **Step 2: Add security and strict page-box cases**

Re-use existing non-PDF, encrypted, signed, malformed box, malformed Rotate, and malformed UserUnit fixtures. Expected status is the same rewrite-layer boundary used by crop/trim:

```text
non-PDF -> UNSUPPORTED
encrypted -> UNSUPPORTED
signed -> UNSUPPORTED
malformed required page-box/Rotate/UserUnit -> FORMAT
```

- [ ] **Step 3: Add `1×1` deterministic no-op tests**

For `poster-basic.pdf`, call the API twice with `{page_index=1, columns=1, rows=1}` and assert:

```text
both calls OK
both outputs non-NULL
output bytes identical
source page count still 3
source page 1 bounds unchanged
```

This test must become GREEN in this task even though real `2×2` remains RED.

- [ ] **Step 4: Implement geometry plan ownership**

The plan builder must:

1. validate minimum `struct_size = offsetof(..., rows) + sizeof(size_t)`;
2. reject zero dimensions and duplicate page indices;
3. normalize request order by ascending original `page_index` into plan-owned storage without modifying caller memory;
4. resolve strict page box view through `quantapdf_pdf_page_box_resolve`;
5. calculate `tile_count = columns * rows` with overflow checks;
6. calculate final output page count with `INT_MAX` guard;
7. allocate tile plan storage with `SIZE_MAX / sizeof(...)` checks;
8. compute public edges using direct indexed division, not accumulated tile widths;
9. cast each computed edge to `float`, then require strictly increasing adjacent edges;
10. map each public tile through `fz_invert_matrix(page.pdf_to_public)` and normalize the resulting raw rectangle;
11. require finite positive tile raw rectangles and source-visible containment;
12. set `changed = tile_count > 1`, `any_changed` across the batch.

Use a double intermediate for edge computation, then store the resulting public coordinate as `float`:

```c
double fraction = (double)index / (double)count;
float edge = (float)((double)start + ((double)end - (double)start) * fraction);
```

First/last stored edges must be assigned exactly from `visible_public.x0/x1` and `y0/y1`, not recomputed.

- [ ] **Step 5: Implement Poster Split security check**

Match the existing transform policy: reject trailer `/Encrypt` and already-signed signature fields without executing form actions. Keep this helper Poster-specific; do not refactor crop/trim security in this task.

- [ ] **Step 6: Implement all-no-op path in `src/pdf_poster.c`**

Flow:

```c
check security
build plan with expansion_policy=0
if (!plan->any_changed)
    return quantapdf_serialize_pdf(document->ctx, source_pdf, out_output);
return QUANTAPDF_ERROR_UNSUPPORTED;
```

A real split remains intentionally RED after this task.

- [ ] **Step 7: Modularize the single CTest**

Rename base `main` at compile time and add `test_pdf_poster_split_main.c`:

```c
int quantapdf_pdf_poster_split_base_main(void);
int poster_run_geometry_tests(void);

int main(void)
{
    if (quantapdf_pdf_poster_split_base_main() != 0)
        return 1;
    return poster_run_geometry_tests();
}
```

`test_pdf_poster_split_geometry.c` should initially characterize plan-visible public geometry only through the final output once the writer arrives; for now keep Rotate/UserUnit changed calls as explicit expected RED checks at the end of the module so the first failure remains the basic changed path.

- [ ] **Step 8: Run Linux static + sanitizer tests**

Expected:

```text
#1-#23 green
Poster Split validation/no-op checks green
#24 still fails only at first valid changed 2×2 call
```

- [ ] **Step 9: Commit**

```bash
git add src/pdf_poster_internal.h src/pdf_poster_preflight.c src/pdf_poster.c \
  CMakeLists.txt tests/test_pdf_poster_split.c tests/test_pdf_poster_split_internal.h \
  tests/test_pdf_poster_split_geometry.c tests/test_pdf_poster_split_main.c \
  tests/fixtures/poster-rotate-90.pdf tests/fixtures/poster-userunit.pdf tests/CMakeLists.txt
git commit -m "feat: add Poster Split geometry preflight"
```

---

### Task 4: Expansion Policy, Global Action Safety, and Migration Classification

**Files:**
- Modify: `src/pdf_poster_internal.h`
- Modify: `src/pdf_poster_preflight.c`
- Create: `src/pdf_poster_annotations.c`
- Create: `src/pdf_poster_navigation.c`
- Create if extraction is required: `src/pdf_outline_common.h`, `src/pdf_outline_common.c`
- Modify if common walker is extracted: `src/pdf_outline.c`
- Modify: `CMakeLists.txt`
- Create: `tests/test_pdf_poster_split_policy.c`
- Create: `tests/fixtures/poster-crossing-annotation.pdf`
- Create: `tests/fixtures/poster-crossing-widget.pdf`
- Create: `tests/fixtures/poster-production-box.pdf`
- Create: `tests/fixtures/poster-tagged.pdf`
- Create: `tests/fixtures/poster-root-policy.pdf`
- Create: `tests/fixtures/poster-unselected-actions.pdf`
- Create: `tests/fixtures/poster-form-actions.pdf`
- Create: `tests/fixtures/poster-malformed-annots.pdf`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces expansion-only semantic classifications before any writer mutation.
- Extend each split plan with migration entries that contain semantic locators, not source pointers:

```c
typedef enum quantapdf_pdf_poster_annot_kind {
    QUANTAPDF_PDF_POSTER_ANNOT_LINK,
    QUANTAPDF_PDF_POSTER_ANNOT_WIDGET,
    QUANTAPDF_PDF_POSTER_ANNOT_ORDINARY
} quantapdf_pdf_poster_annot_kind;

typedef struct quantapdf_pdf_poster_annot_plan {
    size_t source_annot_index;
    quantapdf_pdf_poster_annot_kind kind;
    quantapdf_rect source_public_rect;
    size_t first_tile_index;
    size_t tile_count;
} quantapdf_pdf_poster_annot_plan;
```

For Links, `first_tile_index/tile_count` describe the positive-area tile intersection sequence; ordinary annotation/Widget require `tile_count == 1`.

- [ ] **Step 1: Add policy-only RED tests before broadening successful output**

Each real `2×1`/`2×2` request must return the exact approved class:

```text
selected effective Bleed/Trim/Art -> UNSUPPORTED
catalog StructTreeRoot -> UNSUPPORTED
selected StructParents -> UNSUPPORTED
catalog OpenAction/AA/PageLabels/Threads -> UNSUPPORTED
page AA on selected or unselected page -> UNSUPPORTED
non-Link annotation A/AA anywhere -> UNSUPPORTED
Link AA or action Next anywhere -> UNSUPPORTED
unsupported Link action kind anywhere -> UNSUPPORTED
AcroForm XFA -> UNSUPPORTED
field/Widget A or AA anywhere -> UNSUPPORTED
malformed unselected Annots needed by global scan -> FORMAT
unknown selected-page key outside the allowlist -> UNSUPPORTED
crossing ordinary annotation -> UNSUPPORTED
crossing Widget -> UNSUPPORTED
Popup/relation graph -> UNSUPPORTED
```

For the same fixtures, pure `1×1` no-op must not apply expansion-only restrictions, but must still enforce security and page-box validation.

- [ ] **Step 2: Lock the selected-page local-key allowlist**

For a real split, iterate local keys on the selected `/Page`. Accept only:

```text
Type
Parent
MediaBox
CropBox
Resources
Contents
Rotate
UserUnit
Group
Tabs
Annots
```

The page-box/common resolver handles inherited MediaBox/CropBox/Rotate/Resources semantics. Explicit/effective Bleed/Trim/Art are rejected separately even when inherited. Any other local key on the selected page is `UNSUPPORTED`, including all identity-sensitive keys named by the spec.

- [ ] **Step 3: Implement document-root expansion gates**

Before selected-page migration classification, reject real expansion when catalog contains any of:

```text
OpenAction
AA
PageLabels
Threads
StructTreeRoot
```

Do not delete or normalize those entries.

- [ ] **Step 4: Implement document-wide `/Annots` action scan**

For every page under any real expansion:

```text
missing Annots -> allowed
present Annots but not array -> FORMAT
entry required for classification not indirect dictionary -> FORMAT
Link -> accept only Dest, local GoTo/D, or URI; reject AA, Next, other action kinds
non-Link -> reject A or AA anywhere
```

Selected split pages then receive stricter migration classification: valid subtype, finite positive Rect, public-space mapping, containment/crossing rules.

Use `quantapdf_pdf_annotation_classify()` for ordinary subtype mapping where possible, but do not inherit immutable enumeration's tolerant-ignore semantics for objects that Poster Split must move.

- [ ] **Step 5: Implement public-grid rectangle membership helper**

One helper is authoritative for Link/annotation/Widget placement:

```c
quantapdf_status quantapdf_pdf_poster_rect_tiles(
    const quantapdf_pdf_poster_split_plan *split,
    quantapdf_rect rect,
    size_t *out_first_tile,
    size_t *out_tile_count,
    int *out_crosses);
```

Rules:

```text
boundary-only contact does not count as crossing
ordinary annotation/widget must have exactly one containing tile
Link may have every positive-area intersection in row-major order
rect completely outside source visible split domain -> UNSUPPORTED for selected-page migration
```

- [ ] **Step 6: Use the existing strict form model/provenance rather than inventing Widget identity**

For any document with AcroForm during real expansion:

```c
quantapdf_pdf_form_build(
    ctx,
    document,
    1,
    &model,
    &provenance);
```

Use provenance `live_field.widgets[]` to prove every selected-page Widget object belongs to exactly one field and has expected page membership. Reject XFA and field/Widget action containers before accepting migration. Record migration by deterministic field index/widget index/page annotation ordinal; do not store source pointers in the semantic plan.

- [ ] **Step 7: Extract/reuse strict outline structure validation if needed**

If Poster navigation cannot reuse current static outline preflight without duplication, extract only the structural walk into `pdf_outline_common.[ch]` with an interface equivalent to:

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

`pdf_outline.c` must delegate to the extracted walker and keep existing test #17 behavior unchanged. Do not expose this helper publicly.

- [ ] **Step 8: Add conservative navigation classification placeholder state, not a writer**

`pdf_poster_navigation.c` in this task may classify Link/Outline/local destination containers and set plan facts such as `has_split_target_navigation`; it must not mutate them yet. Any destination targeting a split page that cannot be semantically captured for Task 7 is `UNSUPPORTED` now, never silently preserved.

- [ ] **Step 9: Verify policy GREEN / changed writer still RED**

Expected:

```text
all policy/error fixtures return exact documented statuses
all old 23 tests green
all no-op tests green
basic valid 2×2 still fails only because no tile writer exists
```

- [ ] **Step 10: Commit**

```bash
git add src/pdf_poster_internal.h src/pdf_poster_preflight.c src/pdf_poster_annotations.c \
  src/pdf_poster_navigation.c CMakeLists.txt tests/test_pdf_poster_split_policy.c \
  tests/fixtures/poster-crossing-annotation.pdf tests/fixtures/poster-crossing-widget.pdf \
  tests/fixtures/poster-production-box.pdf tests/fixtures/poster-tagged.pdf \
  tests/fixtures/poster-root-policy.pdf tests/fixtures/poster-unselected-actions.pdf \
  tests/fixtures/poster-form-actions.pdf tests/fixtures/poster-malformed-annots.pdf tests/CMakeLists.txt
# include pdf_outline_common.* / pdf_outline.c only if the strict walker extraction was actually required
git commit -m "feat: add Poster Split expansion safety preflight"
```

---

### Task 5: Core Same-Document Tile Page Writer

**Files:**
- Modify: `src/pdf_poster.c`
- Modify: `src/pdf_poster_internal.h`
- Modify: `src/pdf_poster_preflight.c`
- Modify: `tests/test_pdf_poster_split_geometry.c`
- Create: `tests/test_pdf_poster_split_raw.c`
- Modify: `tests/test_pdf_poster_split_internal.h`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces a private runtime map whose `pdf_obj *` values exist only in the private context:

```c
typedef struct quantapdf_pdf_poster_private_split {
    pdf_obj *source_page;
    pdf_obj **tile_pages;
    size_t tile_count;
} quantapdf_pdf_poster_private_split;
```

This runtime map is never part of source/private semantic plan equivalence.

- [ ] **Step 1: Turn geometry tests into real GREEN requirements**

For `poster-basic.pdf`, split page 1 as 2×2 and verify after saving/reopening output:

```text
page count = 6
page order markers:
  0 POSTER-BEFORE
  1 POSTER-00
  2 POSTER-01
  3 POSTER-10
  4 POSTER-11
  5 POSTER-AFTER
pages 1..4 visible bounds = 200×150
```

Also add caller request-order normalization using two selected pages only after Task 8; keep this task focused on one source-page expansion.

- [ ] **Step 2: Add pixel render equivalence**

Render source page 1 four times at 72 DPI with `clip_enabled=1` and each source tile public rectangle. Render each output tile page at 72 DPI with no clip. Compare:

```text
width
height
stride
components
sample size
sample bytes with memcmp
```

Use the simple vector fixture so antialiasing/render state is identical between clipped source and tile output.

- [ ] **Step 3: Add Rotate 90 and UserUnit 2 output characterizations**

For each fixture:

```text
split in source public grid
row-major marker order stays visual, not raw-axis order
output tile public bounds match expected MuPDF page-transform result
render-equivalence check passes for at least one tile
```

Do not add manual rotate switch/case geometry in production.

- [ ] **Step 4: Implement changed-path private copy orchestration**

Mirror Crop/Trim's proven substrate:

```text
serialize source once
new private fz_context
suppress private warnings/errors
open seed bytes
pdf_disable_js
recheck Poster security
rebuild plan with expansion_policy=1
compare source/private semantic plans
only then allocate/write private tile objects
serialize final private document
```

On any mismatch return `QUANTAPDF_ERROR_FORMAT` before page-graph mutation.

- [ ] **Step 5: Construct every tile page object before page-tree splicing**

For each changed split plan, in private context:

```c
pdf_obj *page_dict = pdf_new_dict(ctx, document, 10);
pdf_dict_put(ctx, page_dict, PDF_NAME(Type), PDF_NAME(Page));
pdf_dict_put_rect(ctx, page_dict, PDF_NAME(MediaBox), tile->pdf_rect);
pdf_dict_put_rect(ctx, page_dict, PDF_NAME(CropBox), tile->pdf_rect);
```

Then share approved state:

```text
Contents: local source page value, if present
Resources: effective inherited source value, if present
Group: local source value, if present
Tabs: local source value, if present
Rotate: materialize effective non-zero Rotate
UserUnit: materialize non-default page-local UserUnit
```

Use same-document references with `pdf_dict_put`; do not graft/deep-copy content/resource streams.

Make the tile dictionary indirect with `pdf_add_object(ctx, document, page_dict)` and retain the returned indirect reference in the private runtime map. Do not insert it yet.

- [ ] **Step 6: Reject unexpected interactive state until Tasks 6-7 own it**

In this task's changed writer, if the accepted semantic plan contains selected-page annotations or destination rewrite entries, return `QUANTAPDF_ERROR_UNSUPPORTED` before the first private write. This temporary guard is removed by Tasks 6 and 7. `poster-basic`, Rotate, and UserUnit fixtures contain none of that state and must proceed.

- [ ] **Step 7: Splice pages deterministically**

After all private tile objects exist, process changed split plans in descending original `page_index`. For each source page at its current index:

```text
insert tile 0 at source index
insert tile 1 at source index + 1
...
insert tile N-1 at source index + N-1
delete original source page now shifted to source index + N
```

Use `pdf_insert_page()` and `pdf_delete_page()`. Descending original indices prevent an earlier splice from invalidating a later original page index.

- [ ] **Step 8: Add raw graph checks**

`test_pdf_poster_split_raw.c` opens output bytes privately with MuPDF and asserts:

```text
tile local MediaBox/CropBox match expected raw rectangles
all tile Contents references resolve to the same source payload semantics
all tile Resources references resolve to the same resource dictionary semantics
no tile contains Bleed/Trim/Art/AA/StructParents or unknown copied page keys
Rotate/UserUnit raw values are present only according to the writer policy
```

Do not compare source/output object numbers; only within one output may tests use indirect identity to prove tile sharing.

- [ ] **Step 9: Run Linux static + ASan/UBSan**

Expected: all 24 CTests pass for the currently supported noninteractive cases; policy tests remain green.

- [ ] **Step 10: Commit**

```bash
git add src/pdf_poster.c src/pdf_poster_internal.h src/pdf_poster_preflight.c \
  tests/test_pdf_poster_split_geometry.c tests/test_pdf_poster_split_raw.c \
  tests/test_pdf_poster_split_internal.h tests/CMakeLists.txt
git commit -m "feat: add Poster Split tile page writer"
```

---

### Task 6: URI Links, Ordinary Annotations, Widgets, and AcroForm Migration

**Files:**
- Modify: `src/pdf_poster.c`
- Modify: `src/pdf_poster_annotations.c`
- Modify: `src/pdf_poster_internal.h`
- Create: `tests/test_pdf_poster_split_interactive.c`
- Create: `tests/fixtures/poster-interactive.pdf`
- Modify: `tests/test_pdf_poster_split_raw.c`
- Modify: `tests/test_pdf_poster_split_internal.h`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: private tile page refs from Task 5 and semantic annotation plans from Task 4.
- Produces private migration helper:

```c
quantapdf_status quantapdf_pdf_poster_apply_annotations(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_plan *plan,
    quantapdf_pdf_poster_private_split *private_splits);
```

- [ ] **Step 1: Build the interactive fixture without split-target destination dependence for this task's first GREEN**

The split page contains:

```text
contained URI Link in tile 0
crossing URI Link spanning tiles 0 and 1
contained Square annotation in tile 2 with Contents="POSTER-ANNOT"
contained Text Widget in tile 3
AcroForm field tree:
  parent /T (poster)
  child field /T (text)
  public full name poster.text
  /V (POSTER-VALUE)
```

Use legal Parent/Kids field naming; do not encode `poster.text` as one dotted partial `/T` name.

The fixture may already contain Outline/named/internal navigation entries required by Task 7, but Task 6 tests should run a variant with those entries absent or target only unsplit pages so annotation migration can be proven independently.

- [ ] **Step 2: Add contained URI Link test**

After 2×2 split:

```text
URI bytes identical
exactly one tile exposes the link
public hotspot equals source hotspot translated by tile page frame
raw action/URI dictionary semantics preserved
```

- [ ] **Step 3: Add crossing URI Link test**

A source public hotspot `[150,40,250,90]` crossing the x=200 edge must become:

```text
tile 0 hotspot [150,40,200,90] in source space -> [150,40,200,90] raw-mapped / tile-local observation
tile 1 hotspot [200,40,250,90] in source space -> [0,40,50,90] public tile-local observation
```

The first positive-area tile row-major keeps the original Link object; later tiles use new indirect Link dictionaries. URI/action state remains semantically identical.

- [ ] **Step 4: Add contained ordinary annotation test**

Verify Square annotation survives on exactly one tile with:

```text
type/flags/Contents unchanged
raw Rect unchanged
public Rect tile-local through new page frame
appearance-related raw keys preserved when fixture provides them
/P updated only if it existed and pointed at source page
```

Crossing ordinary annotation remains `UNSUPPORTED` from Task 4.

- [ ] **Step 5: Add contained Widget/AcroForm test**

Public observations after reopen:

```text
field count unchanged
field full name poster.text unchanged
field value POSTER-VALUE unchanged
Widget count unchanged
Widget now reports correct tile page index
Widget bounds are tile-local
no extra Widget was created
```

Raw checks prove the same field tree still references one Widget and `/V` is unchanged. Crossing Widget remains `UNSUPPORTED`.

- [ ] **Step 6: Implement tile `/Annots` arrays in source annotation order**

Before source page removal, for each selected page:

1. locate source `/Annots` by deterministic source annotation ordinal from the private rebuilt plan;
2. create one new array per tile only when needed;
3. ordinary annotation/Widget: push the same indirect object to exactly one tile array;
4. contained Link: push same indirect object to one tile array;
5. crossing Link: use original object for first row-major intersection; for each later intersection:

```c
pdf_obj *clone_dict = pdf_copy_dict(ctx, source_link);
pdf_obj *clone_ref = pdf_add_object(ctx, document, clone_dict);
```

Referenced action/URI objects remain shared unless Task 7 later rewrites their destination object.

- [ ] **Step 7: Rewrite crossing Link Rect only**

For every Link instance, map the tile-intersection public rect through the source page's `public_to_pdf` matrix, normalize, and write `/Rect`. The original crossing Link's `/Rect` is changed to its first-tile clipped rect; clones receive their own clipped rect.

No other ordinary annotation/Widget raw Rect is translated.

- [ ] **Step 8: Update page membership safely**

For moved original annotation/Widget/Link:

```text
if /P existed and resolved to source page -> set /P to tile page ref
if /P missing and spec permits missing -> leave missing
if /P exists but points elsewhere -> FORMAT for selected-page migration
```

Every Link clone receives `/P` to its tile page.

Put the completed tile `/Annots` arrays onto tile page dictionaries before insertion. Do not retain the source `/Annots` array on any tile.

- [ ] **Step 9: Remove Task 5's temporary selected-annotation guard**

Changed writer now accepts the migration classes implemented by this task. Keep split-target destination rewrite guarded until Task 7.

- [ ] **Step 10: Run Linux static + sanitizer proof**

Expected: 24/24 static and 24/24 ASan/UBSan.

- [ ] **Step 11: Commit**

```bash
git add src/pdf_poster.c src/pdf_poster_annotations.c src/pdf_poster_internal.h \
  tests/test_pdf_poster_split_interactive.c tests/fixtures/poster-interactive.pdf \
  tests/test_pdf_poster_split_raw.c tests/test_pdf_poster_split_internal.h tests/CMakeLists.txt
git commit -m "feat: migrate Poster Split annotations and widgets"
```

---

### Task 7: Document-wide Internal Destination Remap

**Files:**
- Modify: `src/pdf_poster_navigation.c`
- Modify: `src/pdf_poster.c`
- Modify: `src/pdf_poster_internal.h`
- Create: `tests/test_pdf_poster_split_navigation.c`
- Create: `tests/fixtures/poster-legacy-dests.pdf`
- Create: `tests/fixtures/poster-malformed-destination.pdf`
- Modify: `tests/fixtures/poster-interactive.pdf` if navigation objects were intentionally withheld in Task 6
- Modify: `tests/test_pdf_poster_split_raw.c`
- Modify: `tests/test_pdf_poster_split_internal.h`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces deterministic navigation locators and rewrite API:

```c
typedef enum quantapdf_pdf_poster_dest_owner_kind {
    QUANTAPDF_PDF_POSTER_DEST_LINK,
    QUANTAPDF_PDF_POSTER_DEST_OUTLINE,
    QUANTAPDF_PDF_POSTER_DEST_NAME_TREE,
    QUANTAPDF_PDF_POSTER_DEST_LEGACY_DICT
} quantapdf_pdf_poster_dest_owner_kind;

typedef struct quantapdf_pdf_poster_dest_plan {
    quantapdf_pdf_poster_dest_owner_kind owner_kind;
    int owner_page_index;
    size_t owner_ordinal;
    int source_target_page_index;
    quantapdf_point target_public;
    size_t split_plan_index;
    size_t tile_index;
} quantapdf_pdf_poster_dest_plan;

quantapdf_status quantapdf_pdf_poster_apply_navigation(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_plan *plan,
    quantapdf_pdf_poster_private_split *private_splits);
```

Named-destination locators additionally store a copied name/string key in plan-owned bytes; they never store source `pdf_obj *`.

- [ ] **Step 1: Add unselected-page Link -> split-page target test**

The Link stays on the unselected page but its target must reopen as:

```text
correct output tile page index
correct tile-local public target point
```

This explicitly proves the correction's document-wide scan.

- [ ] **Step 2: Add direct Link and GoTo action `/XYZ` tests**

Cover both:

```text
Link /Dest [page /XYZ x y zoom]
Link /A << /S /GoTo /D [page /XYZ x y zoom] >>
```

For split targets, assert raw x/y/zoom unchanged and only page ref changed.

- [ ] **Step 3: Add strict Outline destination tests**

Cover Outline `/Dest` and `/A /GoTo /D` with hierarchy/title preservation. Unsupported Outline action kind or `/Next` action chain returns `UNSUPPORTED`. Malformed outline structure remains governed by the existing strict outline validation and returns `FORMAT`/`UNSUPPORTED` exactly as that validator defines.

- [ ] **Step 4: Add Names/Dests and legacy `/Dests` tests**

Support both destination definition shapes:

```text
name-tree value is destination array
name-tree value is dictionary containing /D destination array
legacy /Dests dictionary value is destination array or dictionary /D
```

Referring Link/Outline name/string remains unchanged. Rewrite the shared definition once.

- [ ] **Step 5: Add grid-edge, Rotate 90, and UserUnit destination ownership tests**

For every split-target `/XYZ`:

```text
raw x/y -> source pdf_to_public -> public point
public x[]/y[] arrays choose row/column
final row/column includes final public edge
```

At least one Rotate 90 boundary point must distinguish the correct visual tile from a naive normalized-raw-axis rule. Add a UserUnit 2 case proving no manual scale shortcut exists.

- [ ] **Step 6: Implement strict destination operand parsing**

For a destination that resolves to a split target page, require:

```text
array length sufficient for /XYZ x y zoom
operand 1 is name XYZ
x numeric finite and non-null
y numeric finite and non-null
zoom null or numeric; preserve as-is
```

Other destination kinds targeting split pages are `UNSUPPORTED`; malformed arrays/types are `FORMAT`. Destinations targeting unsplit pages are preserved without normalization after structural classification.

Resolve page identity through the actual page object (`pdf_lookup_page_number`) while original pages still exist.

- [ ] **Step 7: Implement named-destination strict traversal**

For catalog `Names/Dests`, walk the name tree iteratively with:

```text
max structural depth 256
Kids arrays must be arrays of indirect dictionaries
Names arrays must have even key/value count
keys must be name-tree-compatible strings
no repeated/cyclic indirect node
```

Copy each relevant key into plan-owned memory. For legacy catalog `/Dests`, require dictionary shape and classify every definition needed by the supported registry. Malformed structures needed for remap are `FORMAT`.

Do not normalize or rewrite unrelated `Names` subtrees.

- [ ] **Step 8: Implement public-grid tile selection**

Never choose a destination tile by normalized `tile_pdf` row/column. Use:

```c
fz_point raw = { x, y };
fz_point public_point = fz_transform_point(raw, split->page.pdf_to_public);
```

Then scan the same public x/y edges represented by `tile.public_rect`. Use half-open intervals, with final right/bottom edge inclusive. Point outside `visible_public` is `UNSUPPORTED`.

- [ ] **Step 9: Rewrite only target page references in private graph**

After all tile page indirect refs exist and before source pages are removed, resolve each private locator and mutate the destination array's first element to the selected tile page ref. Preserve x/y/zoom operands byte/number semantics.

When multiple references use the same named destination definition, mutate the definition object once; do not replace referring names/strings.

- [ ] **Step 10: Remove the temporary split-target-navigation guard**

Changed writer now supports the full V1 destination registry and rejects every action graph outside it before mutation.

- [ ] **Step 11: Add raw destination assertions**

`test_pdf_poster_split_raw.c` must prove:

```text
raw XYZ x/y/zoom unchanged
new first destination operand points to expected tile Page object
Outline hierarchy/title unchanged
named reference string/name unchanged
shared named definition remains one logical definition
```

- [ ] **Step 12: Run Linux static + sanitizer proof**

Expected: 24/24 static and 24/24 ASan/UBSan.

- [ ] **Step 13: Commit**

```bash
git add src/pdf_poster_navigation.c src/pdf_poster.c src/pdf_poster_internal.h \
  tests/test_pdf_poster_split_navigation.c tests/fixtures/poster-legacy-dests.pdf \
  tests/fixtures/poster-malformed-destination.pdf tests/fixtures/poster-interactive.pdf \
  tests/test_pdf_poster_split_raw.c tests/test_pdf_poster_split_internal.h tests/CMakeLists.txt
git commit -m "feat: remap Poster Split destinations"
```

---

### Task 8: Batch Atomicity, Determinism, Lifetime, and Final Policy Characterization

**Files:**
- Create: `tests/test_pdf_poster_split_batch.c`
- Modify: `tests/test_pdf_poster_split_policy.c`
- Modify: `tests/test_pdf_poster_split_main.c`
- Modify: `tests/test_pdf_poster_split_internal.h`
- Modify: `tests/CMakeLists.txt`
- Production files: change only if a new failing characterization exposes a spec violation; every such production change must be the smallest fix attributable to the new RED.

**Interfaces:**
- No new public API.
- Freezes the semantic contract required before exact-head review.

- [ ] **Step 1: Add two-page different-grid batch**

Use a deterministic fixture or basic fixture variant with two eligible pages:

```text
page A -> 2×1
page B -> 1×2
```

Assert output page order is original-document order with each source page expanded row-major at its original location.

- [ ] **Step 2: Prove request-order normalization**

Call once with requests `[A, B]` and once `[B, A]`. Assert output byte size and bytes are identical.

- [ ] **Step 3: Prove duplicate rejection is order-independent**

Two entries for the same page, even identical grids, return `ARGUMENT` and reset output.

- [ ] **Step 4: Prove failure atomicity**

Batch:

```text
request 0: valid real split
request 1: later page with crossing Widget or unsupported policy state
```

Expected:

```text
status = UNSUPPORTED (or exact later fixture status)
output == NULL
source page count and all source observations unchanged
```

Repeat with a late malformed source state expected `FORMAT`.

- [ ] **Step 5: Prove repeated changed-output determinism**

Same valid multi-page request twice -> byte-identical outputs.

- [ ] **Step 6: Prove mixed `1×1` + real split semantics**

A no-op request on one page plus a real split on another:

```text
no-op page stays one page with original graph
real split expands normally
expansion-only document-wide restrictions still apply because the batch changes page count
```

- [ ] **Step 7: Prove source immutability after success**

Capture before/after source observations:

```text
page count
page bounds
text marker
URI/internal links
ordinary annotation
form field/value/widget
outline target
```

The live source document must remain identical after successful Poster Split call.

- [ ] **Step 8: Prove output lifetime**

Get output, close source document, save/reopen output, then re-run page/text/link/annotation/form/outline observations successfully.

- [ ] **Step 9: Complete final malformed/policy matrix**

Ensure explicit tests exist for every `FORMAT`/`UNSUPPORTED` item named in both specs, especially:

```text
unselected malformed Annots -> FORMAT during real split
unselected page AA -> UNSUPPORTED
unselected non-Link annotation action -> UNSUPPORTED
AcroForm XFA -> UNSUPPORTED
field/Widget action anywhere -> UNSUPPORTED
unsupported Outline action -> UNSUPPORTED
split-target non-XYZ -> UNSUPPORTED
split-target XYZ null/missing x or y -> UNSUPPORTED
split-target point outside visible domain -> UNSUPPORTED
all-1×1 bypasses expansion-only policy but not security/page-box validation
```

- [ ] **Step 10: Run full Linux gate on the exact current head**

Require:

```text
static CTest 24/24
ASan/UBSan CTest 24/24
```

If Task 8 required no production change, record that the final characterizations were already satisfied by Tasks 3-7.

- [ ] **Step 11: Commit test-only finalization or minimal attributable fix**

Preferred final Task 8 commit:

```bash
git add tests/test_pdf_poster_split_batch.c tests/test_pdf_poster_split_policy.c \
  tests/test_pdf_poster_split_main.c tests/test_pdf_poster_split_internal.h tests/CMakeLists.txt
git commit -m "test: complete Poster Split batch characterization"
```

If a production fix was required, include only the specifically implicated Poster Split source file(s) and state the failing characterization in the commit/PR checkpoint.

---

### Task 9: Freeze Exact Head, Same-SHA Full CI, Final Review, and STOP

**Files:**
- No production/test changes expected.
- PR # created in Task 1: update body/checkpoint only.
- Issue #53: add proof checkpoint only.
- Do not edit workflow YAML.

**Interfaces:**
- Produces one immutable candidate SHA and evidence package.
- Does not integrate.

- [ ] **Step 1: Freeze the current feature head**

Read `feat/poster-split` after Task 8. Record exact SHA and tree. From this point, any feature-branch content change invalidates this task and requires a new freeze/proof cycle.

- [ ] **Step 2: Audit changed paths against baseline**

Compare exact baseline `eca8179e59723e2e6dfd3b3acbdedc61c15bf350` to frozen candidate. Expected scope families only:

```text
Poster Split specs/plan
public header
root CMake
Poster Split production files
optional strict outline-common extraction
Poster Split tests/fixtures/test CMake
```

No `.github/workflows/*` change is allowed.

- [ ] **Step 3: Perform forbidden-surface audit**

Search exact frozen production source and verify:

```text
no pdf_graft_mapped_page / pdf_graft_page in Poster Split
no content-stream rewrite/filter/vectorize API
no form recalculation/format/validate/action execution
no appearance regeneration
no tagged-PDF rewrite
no Widget cloning
ordinary annotation cloning absent
Link cloning only in explicitly crossing-Link path
page dictionary writes limited to allowlisted/generated tile keys
source/private pointer identity never compared
```

- [ ] **Step 4: Verify exact-head Linux proof remains green**

Require a workflow on the frozen SHA with static 24/24 and ASan/UBSan 24/24.

- [ ] **Step 5: Trigger same-SHA cross-platform `full-ci` without workflow edits**

Use the existing `full-ci` PR label event. If the label is already present and adding it creates no event, remove then re-add the same label; do not modify the workflow file.

- [ ] **Step 6: Require full-ci on the exact frozen feature SHA**

Evidence must show:

```text
Linux static 24/24
Linux ASan/UBSan 24/24
macOS ARM64 24/24
Windows DLL build 24/24
```

Windows log must explicitly show `quantapdf.dll`, `quantapdf_test_pdf_crop.exe`, `quantapdf_test_pdf_trim.exe`, and `quantapdf_test_pdf_poster_split.exe` built, with Poster Split as test 24/24 and 100% of 24 tests passing.

- [ ] **Step 7: Fresh review state**

Check submitted reviews, inline review threads, requested reviewers, mergeability, PR head SHA, and current master. Any unresolved Critical/Important review issue blocks the gate.

- [ ] **Step 8: Final architecture review**

Explicitly answer no Critical/Important blocker for:

```text
public Fitz-space grid semantics
Rotate/UserUnit destination ownership
same-document page construction
page-key allowlist
content/resource sharing
crossing Link clipping/cloning
ordinary annotation containment-only move
Widget no-clone semantics and AcroForm preservation
document-wide action safety
destination registry and named-destination rewrite
page order and request normalization
no-op/security distinction
batch atomicity/determinism/lifetime
```

- [ ] **Step 9: Update PR and #53 checkpoint**

Record frozen SHA, exact Linux run, exact full-ci run, scope audit, forbidden audit, review result, and explicit STOP.

- [ ] **Step 10: STOP**

Do not mark ready, merge, close #53, update #48/#2 as integrated, delete the feature branch, or start Flatten/Bake. Wait for explicit integration authorization.

---

### Task 10: Integrate Only After Explicit Authorization

**Precondition:** User explicitly authorizes integration after Task 9, for example `go integrate`.

**Files:**
- No feature content changes expected.
- PR/issue/roadmap metadata only after integrated-master proof.

- [ ] **Step 1: Fresh pre-merge no-drift verification**

Require:

```text
PR head == frozen Task 9 SHA
same-SHA full-ci still success
no new blocking review/thread
master head known exactly
```

If feature head moved, stop and redo Task 9. If master moved, inspect merge preview/mergeability and re-prove as needed; do not silently rebase or force feature history.

- [ ] **Step 2: Attempt normal ready + expected-head merge**

Prefer normal PR merge with expected frozen head SHA and merge method `merge`. If the known draft→ready connector GraphQL bug persists and GitHub REST rejects the draft, use only the previously approved exact Git-data two-parent fallback:

```text
parent 1 = exact current master
parent 2 = exact frozen Poster Split feature
merge tree = exact frozen feature tree, only when feature is directly based on current master / clean integration is proven
master ref update = force=false
```

If that exact clean fallback cannot be proven, stop instead of forcing integration.

- [ ] **Step 3: Obtain exact integrated master SHA**

Confirm GitHub recognizes the PR as merged and master points to the intended merge commit/tree.

- [ ] **Step 4: Require fresh master push workflow on exact merge SHA**

Require log-level evidence:

```text
Linux static 24/24
Linux ASan/UBSan 24/24
macOS ARM64 24/24
Windows DLL 24/24
Windows built quantapdf.dll and quantapdf_test_pdf_poster_split.exe
```

- [ ] **Step 5: Only after integrated-master proof, close and checkpoint**

```text
close #53 as completed
comment integrated proof on PR/#53
append Phase 6 Poster Split checkpoint to #48 and #2
```

Do not start Flatten/Bake automatically; integration authorization for Poster Split does not authorize the next feature.
