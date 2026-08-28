# PDF Page Export + Immutable Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PDF-only `extractpdf_export_pages` primitive that emits caller-ordered/duplicated pages into immutable ExtractPDF-owned PDF bytes.

**Architecture:** Validate the entire request before composition, down-cast the generic MuPDF document to `pdf_document`, graft each requested page into a fresh destination PDF through one `pdf_graft_map`, serialize to an in-memory `fz_buffer`, then copy the final bytes into an opaque `extractpdf_output`. The public output contains only `malloc`-owned bytes and is independent of the source document and MuPDF context.

**Tech Stack:** C11, CMake/CTest, MuPDF 1.28.2 (`fe374accd98a43174a328fa7980d7675e06d5b0d`), GitHub Actions Linux normal + ASan/UBSan and cross-platform full-ci.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-export-pages-design.md`

## Global Constraints

- Base is integrated `master` `1ee52484c7561fb38ecf505d2de0ea83e8b6d9b1`; implementation branch is `feat/pdf-export-pages`.
- Public ABI remains stable C and exposes no MuPDF type.
- `extractpdf_output` is opaque, immutable, and fully source-document-independent.
- Page indices are zero-based, emitted exactly in caller order, and duplicates are valid.
- Validate the entire index list before the first graft; no partial public result.
- Non-PDF source documents return `EXTRACTPDF_ERROR_UNSUPPORTED`.
- V1 preserves only the MuPDF 1.28.2 `pdf_graft_mapped_page` surface: `Contents`, `Resources`, page boxes, `Rotate`, and `UserUnit`; it does not preserve `Annots`, links, annotations, widgets, or document-root interactive state.
- Writer options are `pdf_default_write_options` plus `reproducible = 1` and `dont_regenerate_id = 1`.
- No direct filename save API, callback sink, memory-open API, multi-document merge, or unrelated refactor.
- TDD is strict: production declarations/implementation are forbidden until the RED workflow has failed for the intended missing output API.

---

### Task 1: Deterministic RED contract

**Files:**
- Create: `tests/fixtures/composition-three-page.pdf`
- Create: `tests/fixtures/composition-non-pdf.txt`
- Create: `tests/test_pdf_export.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes existing public APIs: `extractpdf_open`, `extractpdf_close`, `extractpdf_page_count`, `extractpdf_load_page`, `extractpdf_page_bounds`, `extractpdf_extract_text`, `extractpdf_free`, `extractpdf_drop_page`.
- Produces the wished-for compile contract for `extractpdf_output`, `extractpdf_export_pages`, `extractpdf_output_data`, and `extractpdf_drop_output` without adding those declarations yet.

- [ ] **Step 1: Create the deterministic three-page PDF fixture**

Generate a minimal PDF whose page tree contains exactly three pages with these MediaBox/CropBox values and searchable Helvetica text:

```text
page 0: /MediaBox [0 0 200 200], /CropBox [0 0 200 200], text PAGE-A
page 1: /MediaBox [0 0 240 180], /CropBox [0 0 240 180], text PAGE-B
page 2: /MediaBox [0 0 300 150], /CropBox [0 0 300 150], text PAGE-C
```

Use direct page content streams such as:

```pdf
BT
/F1 18 Tf
20 80 Td
(PAGE-A) Tj
ET
```

with one shared `/Font << /F1 ... >>` resource. Keep the fixture free of annotations, outlines, forms, encryption, metadata dependencies, and signatures.

- [ ] **Step 2: Create a non-PDF fixture**

`tests/fixtures/composition-non-pdf.txt` must contain exactly:

```text
ExtractPDF composition non-PDF fixture.
```

The test first requires `extractpdf_open` to succeed for this `.txt` document before checking the composition-specific unsupported result.

- [ ] **Step 3: Write the RED test against the wished-for public ABI**

Create `tests/test_pdf_export.c` with these helpers and assertions. The file must include only `<extractpdf/extractpdf.h>` plus standard C headers; do not locally forward-declare the new API.

Core shape:

```c
#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int expect_page(extractpdf_document *document,
                       int index,
                       const char *needle,
                       float width,
                       float height)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;
    int ok = 0;

    if (extractpdf_load_page(document, index, &page) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_page_bounds(page, &bounds) != EXTRACTPDF_OK)
        goto done;
    if (bounds.x0 != 0.0f || bounds.y0 != 0.0f ||
        bounds.x1 != width || bounds.y1 != height)
        goto done;
    if (extractpdf_extract_text(page, &text, &text_size) != EXTRACTPDF_OK)
        goto done;
    if (text == NULL || strstr(text, needle) == NULL)
        goto done;
    ok = 1;

done:
    extractpdf_free(text);
    extractpdf_drop_page(page);
    return ok;
}
```

The main success path must:

```c
int indices[] = {2, 0, 2};
extractpdf_output *first = NULL;
extractpdf_output *second = NULL;
const unsigned char *first_data = NULL;
const unsigned char *second_data = NULL;
size_t first_size = 0;
size_t second_size = 0;

/* open source */
/* export twice with {2,0,2} */
/* require output_data success */
/* require size > 0 and bytes start with "%PDF-" */
/* require first_size == second_size and memcmp(...) == 0 */
/* close source before further use of first_data */
/* write first_data to COMPOSITION_OUTPUT_PDF */
/* reopen through extractpdf_open */
/* require page_count == 3 */
/* page 0 PAGE-C 300x150 */
/* page 1 PAGE-A 200x200 */
/* page 2 PAGE-C 300x150 */
```

The same executable must separately assert all of these contract failures:

```c
extractpdf_export_pages(NULL, indices, 3, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_pages(document, indices, 3, NULL) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_pages(document, NULL, 3, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_pages(document, indices, 0, &out) == EXTRACTPDF_ERROR_ARGUMENT;

int negative[] = {-1};
extractpdf_export_pages(document, negative, 1, &out) == EXTRACTPDF_ERROR_ARGUMENT;

int high[] = {3};
extractpdf_export_pages(document, high, 1, &out) == EXTRACTPDF_ERROR_ARGUMENT;

int mixed[] = {0, 3, 1};
extractpdf_export_pages(document, mixed, 3, &out) == EXTRACTPDF_ERROR_ARGUMENT;
```

After every failing call with an `out` slot, initialize `out` to a non-NULL sentinel first and require it to be reset to `NULL`.

For the output accessor, require reset semantics:

```c
const unsigned char *data = (const unsigned char *)(uintptr_t)1;
size_t size = 123;

extractpdf_output_data(NULL, &data, &size) == EXTRACTPDF_ERROR_ARGUMENT;
data == NULL;
size == 0;

extractpdf_output_data(first, NULL, &size) == EXTRACTPDF_ERROR_ARGUMENT;
size == 0;

extractpdf_output_data(first, &data, NULL) == EXTRACTPDF_ERROR_ARGUMENT;
data == NULL;
```

For the non-PDF source:

```c
extractpdf_document *text_document = NULL;
extractpdf_open(COMPOSITION_NON_PDF, NULL, &text_document) == EXTRACTPDF_OK;
extractpdf_export_pages(text_document, one_index, 1, &out) == EXTRACTPDF_ERROR_UNSUPPORTED;
out == NULL;
```

Call `extractpdf_drop_output(NULL)` once to lock NULL safety.

- [ ] **Step 4: Wire only the new RED test into CTest**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_pdf_export test_pdf_export.c)
target_link_libraries(extractpdf_test_pdf_export PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_export PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  COMPOSITION_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-export-output.pdf")
add_test(NAME extractpdf.pdf_export COMMAND extractpdf_test_pdf_export)
set_tests_properties(extractpdf.pdf_export PROPERTIES TIMEOUT 30)
```

Add `extractpdf_test_pdf_export` to the existing Windows shared-library copy loop. Do not modify production CMake or headers in this task.

- [ ] **Step 5: Commit the RED-only change**

```bash
git add tests/CMakeLists.txt tests/test_pdf_export.c \
  tests/fixtures/composition-three-page.pdf \
  tests/fixtures/composition-non-pdf.txt
git commit -m "test: define PDF page export RED contract"
```

- [ ] **Step 6: Open/update a draft PR and verify the RED workflow**

Expected Linux result: all pre-existing library/test targets build, while `extractpdf_test_pdf_export` fails to compile because `extractpdf_output` and the three new functions are absent from the public header. This is the required RED boundary. A typo, fixture parse failure, or failure in an existing target is not an acceptable RED.

---

### Task 2: Minimal PDF graft + immutable output GREEN

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Modify: `src/internal.h`
- Create: `src/pdf_export.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_export.c`

**Interfaces:**
- Consumes MuPDF 1.28.2 private APIs: `pdf_specifics`, `pdf_create_document`, `pdf_new_graft_map`, `pdf_graft_mapped_page`, `pdf_write_document`, `fz_new_buffer`, `fz_new_output_with_buffer`, `fz_close_output`, `fz_buffer_storage`.
- Produces:

```c
typedef struct extractpdf_output extractpdf_output;

extractpdf_status extractpdf_export_pages(
    extractpdf_document *document,
    const int *page_indices,
    size_t page_count,
    extractpdf_output **out_output);

extractpdf_status extractpdf_output_data(
    const extractpdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

void extractpdf_drop_output(extractpdf_output *output);
```

- [ ] **Step 1: Add only the approved public ABI**

In `include/extractpdf/extractpdf.h`, add the opaque forward declaration near the other handles:

```c
typedef struct extractpdf_output extractpdf_output;
```

Add the three function declarations near document/output lifecycle APIs. Do not expose a public output struct or MuPDF type.

- [ ] **Step 2: Add the private immutable output representation**

In `src/internal.h`:

```c
struct extractpdf_output {
    unsigned char *data;
    size_t size;
};
```

No document/context pointer is allowed in this struct.

- [ ] **Step 3: Implement validation and output accessors first**

Create `src/pdf_export.c` with private PDF API inclusion:

```c
#include "internal.h"
#include <mupdf/pdf.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
```

Accessor/drop implementation:

```c
extractpdf_status extractpdf_output_data(
    const extractpdf_output *output,
    const unsigned char **out_data,
    size_t *out_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (output == NULL || out_data == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_data = output->data;
    *out_size = output->size;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_output(extractpdf_output *output)
{
    if (output == NULL)
        return;
    free(output->data);
    free(output);
}
```

At the start of `extractpdf_export_pages`:

```c
if (out_output == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
*out_output = NULL;

if (document == NULL || page_indices == NULL || page_count == 0 ||
    page_count > (size_t)INT_MAX)
    return EXTRACTPDF_ERROR_ARGUMENT;
```

Get source page count through `fz_count_pages` inside the MuPDF exception boundary, then validate every index before creating/grafting destination pages.

- [ ] **Step 4: Implement the minimal composition path**

The core success path must follow this ownership structure:

```c
pdf_document *source_pdf = NULL;
pdf_document *destination = NULL;
pdf_graft_map *graft = NULL;
fz_buffer *buffer = NULL;
fz_output *memory_output = NULL;
extractpdf_output *result = NULL;
unsigned char *buffer_data = NULL;
size_t buffer_size = 0;
pdf_write_options options = pdf_default_write_options;
int caught_code = FZ_ERROR_NONE;
```

Inside `fz_try(document->ctx)` after full index validation:

```c
source_pdf = pdf_specifics(document->ctx, document->doc);
if (source_pdf == NULL) {
    /* leave MuPDF try normally and return unsupported outside */
}

destination = pdf_create_document(document->ctx);
graft = pdf_new_graft_map(document->ctx, destination);

for (size_t i = 0; i < page_count; ++i)
    pdf_graft_mapped_page(document->ctx, graft, -1, source_pdf, page_indices[i]);

buffer = fz_new_buffer(document->ctx, 0);
memory_output = fz_new_output_with_buffer(document->ctx, buffer);

options.reproducible = 1;
options.dont_regenerate_id = 1;
pdf_write_document(document->ctx, destination, memory_output, &options);
fz_close_output(document->ctx, memory_output);

buffer_size = fz_buffer_storage(document->ctx, buffer, &buffer_data);
```

Then copy to normal C memory before returning:

```c
result = (extractpdf_output *)calloc(1, sizeof(*result));
if (result == NULL)
    return EXTRACTPDF_ERROR_NOMEM;

result->data = (unsigned char *)malloc(buffer_size);
if (result->data == NULL) {
    free(result);
    return EXTRACTPDF_ERROR_NOMEM;
}
memcpy(result->data, buffer_data, buffer_size);
result->size = buffer_size;
```

The real implementation must keep C allocation and MuPDF cleanup exception-safe: destination, graft map, memory output, and buffer are always dropped; no MuPDF-owned pointer survives in `result`. If `pdf_specifics` returns NULL, return `EXTRACTPDF_ERROR_UNSUPPORTED` with `*out_output == NULL` and without creating the destination.

Use the existing `extractpdf_status_from_mupdf(fz_caught(...))` mapping for caught MuPDF failures.

- [ ] **Step 5: Register the implementation source**

In root `CMakeLists.txt`, add exactly:

```cmake
  src/pdf_export.c
```

to the `extractpdf` source list. No unrelated source changes.

- [ ] **Step 6: Run/inspect the GREEN build and tests**

Expected Linux result:

```text
strict -Wall -Wextra -Wpedantic -Werror build: PASS
all existing CTests: PASS
extractpdf.pdf_export: PASS
ASan/UBSan build: PASS
all sanitizer CTests including extractpdf.pdf_export: PASS
```

If byte determinism fails, investigate writer state/ID generation; do not weaken the test. If the non-PDF fixture does not open, verify the pinned build's TXT handler and fix only the fixture/contract mismatch before proceeding.

- [ ] **Step 7: Commit the minimal GREEN**

```bash
git add include/extractpdf/extractpdf.h src/internal.h src/pdf_export.c CMakeLists.txt
git commit -m "feat: export selected PDF pages"
```

---

### Task 3: Exact-head integration evidence and bookkeeping

**Files:**
- Update PR #20 (or the draft PR created from this branch) body/evidence.
- Update issue #19 with RED/GREEN evidence.
- Update umbrella #2 only after the feature checkpoint is green.

**Interfaces:**
- Consumes exact branch head and workflow evidence.
- Produces a reviewable Phase 4 first-slice PR with immutable evidence and no merge unless explicitly requested.

- [ ] **Step 1: Verify exact-head Linux workflow**

Record exact SHA and workflow number. Require Linux normal + ASan/UBSan success on that same SHA. Do not cite an older head after any implementation change.

- [ ] **Step 2: Trigger architecture-critical full-ci**

Because this primitive establishes the Phase 4 output ABI and private MuPDF PDF linkage on Windows DLL builds, add the repository's existing `full-ci` label to the draft PR and require the same exact head to pass:

```text
Linux static + CTest + ASan/UBSan
macOS native configure/build/test
Windows DLL configure/build/test
```

- [ ] **Step 3: Scope-review the final diff**

Compare against `master` and require production changes to remain limited to:

```text
include/extractpdf/extractpdf.h
src/internal.h
src/pdf_export.c
CMakeLists.txt
```

plus the approved spec/plan and focused tests/fixtures. Any change to Page/Render/Text/Search/Image/Links production files is a blocker unless separately justified.

- [ ] **Step 4: Update issue/PR evidence**

Record:

```text
RED exact SHA + intended missing-symbol/type failure
GREEN exact SHA + Linux normal/sanitizer workflow
full-ci exact SHA + Linux/macOS/Windows job results
```

Keep #19 open only for integration bookkeeping if the PR remains stacked/draft. Do not merge or close the umbrella #2 without an explicit integration decision.
