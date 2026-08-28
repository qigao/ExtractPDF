# PDF Contiguous Range Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `extractpdf_export_page_range(...)` as a thin contiguous-index mapping layer over the already-verified `extractpdf_export_pages(...)` composition engine.

**Architecture:** The new helper validates only mapping-safe arguments, expands `(first_page, page_count)` into a temporary `int[]`, and delegates to `extractpdf_export_pages(...)`. It owns no PDF-specific behavior and must not include `<mupdf/pdf.h>` or call any MuPDF PDF graft/write API.

**Tech Stack:** C11, CMake/CTest, existing ExtractPDF immutable output ABI, MuPDF 1.28.2 only through the already-existing export-pages engine.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-export-range-design.md`

## Global Constraints

- Stacked base is PR #20 exact head `d7d3e2a3c0ead330d0f8c97dd2bab7cd695f0012`; implementation branch is `feat/pdf-export-range`.
- Public ABI remains stable C and exposes no MuPDF type.
- `extractpdf_export_pages(...)` remains the only PDF composition/serialization engine.
- `src/pdf_range.c` must not include `<mupdf/pdf.h>` and must not call `pdf_specifics`, `pdf_create_document`, `pdf_new_graft_map`, `pdf_graft_page`, `pdf_graft_mapped_page`, `pdf_write_document`, `pdf_save_document`, or `fz_new_output_with_buffer`.
- Range syntax is zero-based `first_page + page_count`, forward and contiguous only.
- `page_count > 0`; no descending, stepped, disjoint, duplicate-in-range, string-range, or multi-output API.
- Mapping-safety checks happen before allocation: `first_page >= 0`, `page_count <= INT_MAX`, `page_count <= SIZE_MAX / sizeof(int)`, and `first_page + page_count - 1 <= INT_MAX` without overflow.
- Document-dependent page bounds and non-PDF handling remain delegated to `extractpdf_export_pages(...)`.
- Returned output ownership/lifetime is exactly the existing immutable `extractpdf_output` contract.
- TDD is strict: do not add the declaration or production helper until the new range test has failed at the intended missing-API boundary.

---

### Task 1: Range-only deterministic RED

**Files:**
- Create: `tests/test_pdf_range.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes existing APIs: `extractpdf_open`, `extractpdf_close`, `extractpdf_export_pages`, `extractpdf_output_data`, `extractpdf_drop_output`, `extractpdf_page_count`, `extractpdf_load_page`, `extractpdf_page_bounds`, `extractpdf_extract_text`, `extractpdf_free`, `extractpdf_drop_page`.
- Produces the wished-for compile contract only:

```c
extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output);
```

- [ ] **Step 1: Write `tests/test_pdf_range.c` against the absent API**

Use only `<extractpdf/extractpdf.h>` plus standard C headers. Do not locally declare the new function.

Required helpers:

```c
#include <extractpdf/extractpdf.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
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

static int expect_page(
    extractpdf_document *document,
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

Primary success proof:

```c
extractpdf_output *range_output = NULL;
extractpdf_output *index_output = NULL;
const unsigned char *range_data = NULL;
const unsigned char *index_data = NULL;
size_t range_size = 0;
size_t index_size = 0;
int indices[] = {1, 2};

/* open COMPOSITION_PDF */
/* export_page_range(document, 1, 2, &range_output) -> OK */
/* export_pages(document, indices, 2, &index_output) -> OK */
/* output_data for both -> non-NULL PDF bytes */
/* require equal sizes and memcmp(range_data,index_data,size)==0 */
/* write range bytes to RANGE_OUTPUT_PDF and reopen */
/* page_count == 2 */
/* page 0 PAGE-B, 240x180 */
/* page 1 PAGE-C, 300x150 */
```

Additional success checks:

```c
extractpdf_export_page_range(document, 2, 1, &out) == EXTRACTPDF_OK;
/* bytes equal export_pages({2}) */

extractpdf_export_page_range(document, 0, 3, &out) == EXTRACTPDF_OK;
/* bytes equal export_pages({0,1,2}) */
```

Required failure checks, each using a non-NULL sentinel output when an output slot is supplied and requiring reset to NULL:

```c
extractpdf_export_page_range(NULL, 0, 1, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_page_range(document, 0, 1, NULL) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_page_range(document, -1, 1, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_page_range(document, 0, 0, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_page_range(document, 2, 2, &out) == EXTRACTPDF_ERROR_ARGUMENT;
extractpdf_export_page_range(document, INT_MAX, 2, &out) == EXTRACTPDF_ERROR_ARGUMENT;
```

When `SIZE_MAX > INT_MAX`, also require:

```c
extractpdf_export_page_range(
    document, 0, (size_t)INT_MAX + 1u, &out) == EXTRACTPDF_ERROR_ARGUMENT;
```

Non-PDF proof:

```c
extractpdf_open(COMPOSITION_NON_PDF, NULL, &text_document) == EXTRACTPDF_OK;
extractpdf_export_page_range(text_document, 0, 1, &out) == EXTRACTPDF_ERROR_UNSUPPORTED;
out == NULL;
```

- [ ] **Step 2: Wire only the new range test into CTest**

Append before the Windows copy loop:

```cmake
add_executable(extractpdf_test_pdf_range test_pdf_range.c)
target_link_libraries(extractpdf_test_pdf_range PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_range PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  RANGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-range-output.pdf")
add_test(NAME extractpdf.pdf_range COMMAND extractpdf_test_pdf_range)
set_tests_properties(extractpdf.pdf_range PROPERTIES TIMEOUT 30)
```

Add `extractpdf_test_pdf_range` to the existing Windows shared-library copy loop. Do not modify the public header, root CMake, `src/internal.h`, `src/pdf_export.c`, or any other production file in RED.

- [ ] **Step 3: Verify the stacked feature diff before opening the PR**

Compare the branch against `d7d3e2a3c0ead330d0f8c97dd2bab7cd695f0012`. At the RED boundary the only non-doc feature files must be `tests/test_pdf_range.c` and `tests/CMakeLists.txt`.

- [ ] **Step 4: Open a draft stacked PR**

Base: `feat/pdf-export-pages`  
Head: `feat/pdf-export-range`

The PR body must state that the current head is intentionally RED-only and that production code is forbidden until the missing range API is observed in CI.

- [ ] **Step 5: Verify the RED workflow**

Expected Linux result:

```text
pinned dependencies/configure                 PASS
libextractpdf + all existing test executables PASS
extractpdf_test_pdf_range                     FAIL compile
```

The failure must be caused by the absent `extractpdf_export_page_range` declaration/function. Any failure in #20 code/tests, fixture parsing, CMake syntax, or unrelated targets is not an acceptable RED.

---

### Task 2: Thin mapping GREEN

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Create: `src/pdf_range.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_range.c`

**Interfaces:**
- Consumes:

```c
extractpdf_status extractpdf_export_pages(
    extractpdf_document *document,
    const int *page_indices,
    size_t page_count,
    extractpdf_output **out_output);
```

- Produces:

```c
extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output);
```

- [ ] **Step 1: Add only the approved public declaration**

Place the new declaration next to `extractpdf_export_pages(...)` in `include/extractpdf/extractpdf.h`.

- [ ] **Step 2: Implement `src/pdf_range.c` without MuPDF PDF APIs**

Exact implementation shape:

```c
#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output)
{
    int *indices;
    extractpdf_status status;
    size_t i;
    size_t offset;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || first_page < 0 || page_count == 0 ||
        page_count > (size_t)INT_MAX ||
        page_count > SIZE_MAX / sizeof(*indices))
        return EXTRACTPDF_ERROR_ARGUMENT;

    offset = page_count - 1;
    if ((size_t)first_page > (size_t)INT_MAX - offset)
        return EXTRACTPDF_ERROR_ARGUMENT;

    indices = (int *)malloc(page_count * sizeof(*indices));
    if (indices == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    for (i = 0; i < page_count; ++i)
        indices[i] = first_page + (int)i;

    status = extractpdf_export_pages(document, indices, page_count, out_output);
    free(indices);
    return status;
}
```

Do not add `<mupdf/pdf.h>` or any PDF/MuPDF composition call.

- [ ] **Step 3: Register only `src/pdf_range.c` in root CMake**

Add it alongside `src/pdf_export.c` in the existing `add_library(extractpdf ...)` source list. No other build-system change is needed.

- [ ] **Step 4: Verify exact-head Linux GREEN**

Require on the new branch head:

```text
strict static build                  PASS
all normal CTests                    PASS
extractpdf.pdf_export                PASS
extractpdf.pdf_range                 PASS
ASan/UBSan build                     PASS
all sanitizer CTests                 PASS
```

The range test must prove `(1,2) -> PAGE-B/PAGE-C`, `(2,1)`, `(0,3)`, byte equality against equivalent explicit-index exports, overflow validation, out-of-range delegation, output reset, and non-PDF unsupported behavior.

- [ ] **Step 5: Scope review**

Compare against stacked base `d7d3e2a3c0ead330d0f8c97dd2bab7cd695f0012`. Production feature delta must be limited to:

```text
include/extractpdf/extractpdf.h
src/pdf_range.c
CMakeLists.txt
```

`src/pdf_export.c`, `src/internal.h`, and all previous feature implementations must be byte-identical to the stacked base.

- [ ] **Step 6: Keep the PR draft and update #21 / #2**

Record RED and GREEN exact-head workflow evidence. Do not merge #20 or the range PR without a separate explicit integration decision.
