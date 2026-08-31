# QuantaPDF Immutable PDF Output Merge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `quantapdf_merge_outputs(...)` so immutable Phase 4 PDF outputs can be merged in exact caller order without crossing live document MuPDF-context ownership boundaries.

**Architecture:** Merge reparses every immutable `quantapdf_output` inside one fresh temporary `fz_context`, uses one destination `pdf_document` and one source-local `pdf_graft_map` per input, then serializes the destination exactly once. The deterministic PDF writer currently embedded in `quantapdf_export_pages(...)` becomes one private serializer shared by export and merge; the existing export graft/validation semantics remain unchanged.

**Tech Stack:** C11, MuPDF 1.28.2 PDF/Fitz APIs, CMake 3.20+, CTest, GitHub Actions, Linux ASan/UBSan, Windows DLL build, macOS static build.

**Spec:** `docs/superpowers/specs/2026-08-28-quantapdf-pdf-merge-outputs-design.md`

## Global Constraints

- Stack on #25 / PR #26 exact head `18a6c0596b68c1ca7b116624a09e27dcf0ac4a7f`; do not retarget during implementation unless the stack is explicitly integrated first.
- MuPDF remains pinned to 1.28.2 through the existing all-OS vcpkg model; do not change `vcpkg.json`, overlay ports, or CI dependency versions.
- Preserve the stable C ABI and expose no MuPDF types in `include/quantapdf/quantapdf.h`.
- Keep every existing `quantapdf_document` owning its own `fz_context`; merge must not consume live document handles or pass MuPDF objects across contexts.
- Public API is exactly:

```c
QUANTAPDF_API quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output);
```

- `input_count == 0`, `inputs == NULL`, or any `inputs[i] == NULL` returns `QUANTAPDF_ERROR_ARGUMENT`; a non-NULL output slot is reset to `NULL` before further work.
- One input is valid; duplicate input pointers are valid; exact input order defines whole-document append order.
- Total merged page count must not exceed `INT_MAX`; use overflow-safe arithmetic before grafting the source that would exceed the limit.
- Merge is strictly all-or-nothing: no partial output may be published on any failure.
- Inputs remain immutable and alive only for the duration of the call; the successful result is independent of all inputs.
- Do not expand the Phase 4 preservation boundary beyond `Contents`, `Resources`, page boxes, `Rotate`, and `UserUnit`; do not add links/annotations/widgets, metadata, outlines, forms, signatures, encryption, JavaScript, page labels, named destinations, or other document-root state.
- The deterministic writer policy is shared by selected-page export and merge and remains `pdf_default_write_options` with `reproducible = 1` and `dont_regenerate_id = 1`.
- No merge session, live-document merge array, cross-document page-selection descriptor, output list, filename policy, save-to-path API, or unrelated Page/Render/Text/Search/Image/Links refactor belongs in this slice.
- Development remains Linux-first. Because this slice adds public ABI, memory-stream parsing, a new temporary context, and a shared writer refactor, the final exact head must pass Linux normal tests + ASan/UBSan and then a `full-ci` run on Linux/macOS/Windows.
- Do not merge PRs without explicit user authorization.

---

## File Structure

The implementation is intentionally split by responsibility:

```text
include/quantapdf/quantapdf.h
    public merge declaration only

src/pdf_internal.h
    PDF-private shared helper declaration; includes MuPDF PDF types privately

src/pdf_output.c
    deterministic pdf_document -> immutable quantapdf_output serializer only

src/pdf_export.c
    existing single-source selected-page validation/grafting;
    serialization block replaced by the shared private serializer call

src/pdf_merge.c
    merge argument validation, temporary context, memory-source parsing,
    one graft map per source, page-total guard, atomic cleanup

CMakeLists.txt
    register pdf_output.c and pdf_merge.c

tests/test_pdf_merge.c
    deterministic public merge contract

tests/CMakeLists.txt
    register quantapdf.pdf_merge and Windows DLL copy target
```

Do not move `quantapdf_output_data(...)` or `quantapdf_drop_output(...)`; they remain where they are unless a concrete compile failure proves a minimal move is necessary.

---

### Task 1: Add the strict merge-contract RED

**Files:**
- Create: `tests/test_pdf_merge.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `quantapdf_open`, `quantapdf_export_pages`, `quantapdf_output_data`, `quantapdf_drop_output`, `quantapdf_page_count`, `quantapdf_load_page`, `quantapdf_page_bounds`, `quantapdf_extract_text`.
- Produces: one deterministic CTest named `quantapdf.pdf_merge` that fully specifies the new public merge ABI before production code exists.

- [ ] **Step 1: Create `tests/test_pdf_merge.c` with the public contract**

Use the existing fixtures only. The test must create immutable inputs through the public export API, close the original documents before merge, and validate repeated determinism, single-input semantics, duplicate whole-document semantics, lifetime independence, and argument/reset behavior.

Use this structure:

```c
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

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

static quantapdf_document *open_output(
    const quantapdf_output *output,
    const char *path)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    quantapdf_document *document = NULL;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size >= 5);
    CHECK(memcmp(data, "%PDF-", 5) == 0);
    CHECK(write_bytes(path, data, size));
    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    return document;
}

static void expect_page(
    quantapdf_document *document,
    int page_index,
    const char *needle,
    int check_geometry,
    float width,
    float height)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    if (check_geometry) {
        CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
        CHECK(bounds.x0 == 0.0f);
        CHECK(bounds.y0 == 0.0f);
        CHECK(bounds.x1 == width);
        CHECK(bounds.y1 == height);
    }
    CHECK(quantapdf_extract_text(page, &text, &text_size) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(strstr(text, needle) != NULL);

    quantapdf_free(text);
    quantapdf_drop_page(page);
}

static void expect_same_bytes(
    const quantapdf_output *left,
    const quantapdf_output *right)
{
    const unsigned char *left_data = NULL;
    const unsigned char *right_data = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(quantapdf_output_data(left, &left_data, &left_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(right, &right_data, &right_size) == QUANTAPDF_OK);
    CHECK(left_size == right_size);
    CHECK(memcmp(left_data, right_data, left_size) == 0);
}

int main(void)
{
    int sentinel = 0;
    quantapdf_document *composition = NULL;
    quantapdf_document *text = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_output *output_a = NULL;
    quantapdf_output *output_b = NULL;
    quantapdf_output *merged_ab_1 = NULL;
    quantapdf_output *merged_ab_2 = NULL;
    quantapdf_output *merged_single = NULL;
    quantapdf_output *merged_duplicate = NULL;
    quantapdf_output *reset_output = (quantapdf_output *)&sentinel;
    int composition_indices[] = {2, 0};
    int text_indices[] = {0};
    const quantapdf_output *empty_inputs[] = {NULL};
    int page_count = 0;

    (void)remove(MERGE_OUTPUT_PDF);

    CHECK(quantapdf_merge_outputs(NULL, 1, &reset_output) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(reset_output == NULL);

    reset_output = (quantapdf_output *)&sentinel;
    CHECK(quantapdf_merge_outputs(empty_inputs, 0, &reset_output) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(reset_output == NULL);

    CHECK(quantapdf_merge_outputs(empty_inputs, 1, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(COMPOSITION_PDF, NULL, &composition) == QUANTAPDF_OK);
    CHECK(quantapdf_open(TEXT_PDF, NULL, &text) == QUANTAPDF_OK);

    CHECK(quantapdf_export_pages(
        composition,
        composition_indices,
        sizeof(composition_indices) / sizeof(composition_indices[0]),
        &output_a) == QUANTAPDF_OK);
    CHECK(output_a != NULL);

    CHECK(quantapdf_export_pages(
        text,
        text_indices,
        sizeof(text_indices) / sizeof(text_indices[0]),
        &output_b) == QUANTAPDF_OK);
    CHECK(output_b != NULL);

    quantapdf_close(composition);
    quantapdf_close(text);
    composition = NULL;
    text = NULL;

    {
        const quantapdf_output *invalid_inputs[] = {output_a, NULL};
        reset_output = (quantapdf_output *)&sentinel;
        CHECK(quantapdf_merge_outputs(
            invalid_inputs,
            sizeof(invalid_inputs) / sizeof(invalid_inputs[0]),
            &reset_output) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(reset_output == NULL);
    }

    {
        const quantapdf_output *inputs[] = {output_a, output_b};
        CHECK(quantapdf_merge_outputs(inputs, 2, &merged_ab_1) == QUANTAPDF_OK);
        CHECK(quantapdf_merge_outputs(inputs, 2, &merged_ab_2) == QUANTAPDF_OK);
        CHECK(merged_ab_1 != NULL);
        CHECK(merged_ab_2 != NULL);
        expect_same_bytes(merged_ab_1, merged_ab_2);
    }

    {
        const quantapdf_output *inputs[] = {output_a};
        CHECK(quantapdf_merge_outputs(inputs, 1, &merged_single) == QUANTAPDF_OK);
        CHECK(merged_single != NULL);
    }

    {
        const quantapdf_output *inputs[] = {output_b, output_a, output_b};
        CHECK(quantapdf_merge_outputs(inputs, 3, &merged_duplicate) == QUANTAPDF_OK);
        CHECK(merged_duplicate != NULL);
    }

    /* Successful merge outputs must be independent of every input. */
    quantapdf_drop_output(output_a);
    quantapdf_drop_output(output_b);
    output_a = NULL;
    output_b = NULL;

    reopened = open_output(merged_ab_1, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 3);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 2, "Hello Caf", 0, 0.0f, 0.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_single, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 2);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_duplicate, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 4);
    expect_page(reopened, 0, "Hello Caf", 0, 0.0f, 0.0f);
    expect_page(reopened, 1, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 2, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 3, "Hello Caf", 0, 0.0f, 0.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    quantapdf_drop_output(merged_ab_1);
    quantapdf_drop_output(merged_ab_2);
    quantapdf_drop_output(merged_single);
    quantapdf_drop_output(merged_duplicate);
    (void)remove(MERGE_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
```

Use `"Hello Caf"` rather than embedding a source-code accent in this new test; the existing UTF-8 text test already proves the complete `Hello Café` byte sequence, while this merge test only needs a stable distinguishing substring from the second source.

- [ ] **Step 2: Register the RED target in `tests/CMakeLists.txt`**

Add immediately after `quantapdf.pdf_delete`:

```cmake
add_executable(quantapdf_test_pdf_merge test_pdf_merge.c)
target_link_libraries(quantapdf_test_pdf_merge PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_merge PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  MERGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-merge-output.pdf")
add_test(NAME quantapdf.pdf_merge COMMAND quantapdf_test_pdf_merge)
set_tests_properties(quantapdf.pdf_merge PROPERTIES TIMEOUT 30)
```

Also append `quantapdf_test_pdf_merge` to the existing `if(WIN32 AND BUILD_SHARED_LIBS)` test-target list so the DLL is copied beside the new executable.

- [ ] **Step 3: Commit the test-only RED**

```bash
git add tests/test_pdf_merge.c tests/CMakeLists.txt
git commit -m "test: define immutable PDF merge contract"
```

Do not add the public declaration or production implementation in this commit.

- [ ] **Step 4: Open a draft stacked PR to trigger exact-head CI**

Create the PR with:

```text
base: test/pdf-delete-contract
head: feat/pdf-merge-outputs
draft: true
tracks: #27 / #2
```

The PR body must state that this exact head is the intentional RED and that no production merge API exists yet. Record the PR number returned by GitHub; all later plan steps refer to that same draft Merge PR.

- [ ] **Step 5: Verify the RED is the intended missing-API boundary**

Use the PR-triggered workflow on the exact RED SHA. The expected Linux failure is only the new `quantapdf_test_pdf_merge` target because `quantapdf_merge_outputs` is not declared/defined. Existing library sources and pre-existing test targets must continue to build.

Expected diagnostic shape is the same class as the earlier range RED:

```text
implicit declaration of function 'quantapdf_merge_outputs'
undefined reference to 'quantapdf_merge_outputs'
```

If any pre-existing target fails, or the new target fails for fixture/test-code mistakes, fix the RED test before touching production code and re-run until the failure boundary is exact.

---

### Task 2: Extract the shared deterministic serializer without changing export behavior

**Files:**
- Create: `src/pdf_internal.h`
- Create: `src/pdf_output.c`
- Modify: `src/pdf_export.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: private `quantapdf_output` layout from `src/internal.h`; MuPDF `fz_context` and `pdf_document` types.
- Produces: private helper

```c
quantapdf_status quantapdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_output **out_output);
```

used by both export and the later merge task.

This is a behavior-preserving refactor. The existing `quantapdf.pdf_export` repeated-byte test is the characterization gate. The new merge target remains intentionally RED after this task because the public merge symbol still does not exist.

- [ ] **Step 1: Add a focused PDF-private header**

Create `src/pdf_internal.h`:

```c
#ifndef QUANTAPDF_PDF_INTERNAL_H
#define QUANTAPDF_PDF_INTERNAL_H

#include "internal.h"

#include <mupdf/pdf.h>

quantapdf_status quantapdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_output **out_output);

#endif
```

Do not put MuPDF PDF types in `include/quantapdf/quantapdf.h` or broaden `src/internal.h` with `<mupdf/pdf.h>` for unrelated source files.

- [ ] **Step 2: Move only deterministic serialization into `src/pdf_output.c`**

Create `src/pdf_output.c`:

```c
#include "pdf_internal.h"

#include <stdlib.h>
#include <string.h>

static void quantapdf_drop_pdf_serialization_state(
    fz_context *ctx,
    fz_buffer *buffer,
    fz_output *memory_output)
{
    if (memory_output != NULL)
        fz_drop_output(ctx, memory_output);
    if (buffer != NULL)
        fz_drop_buffer(ctx, buffer);
}

quantapdf_status quantapdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_output **out_output)
{
    pdf_write_options options = pdf_default_write_options;
    fz_buffer *buffer = NULL;
    fz_output *memory_output = NULL;
    quantapdf_output *result = NULL;
    unsigned char *buffer_data = NULL;
    size_t buffer_size = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (ctx == NULL || document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    options.reproducible = 1;
    options.dont_regenerate_id = 1;

    fz_var(buffer);
    fz_var(memory_output);
    fz_var(buffer_data);
    fz_var(buffer_size);
    fz_var(caught_code);

    fz_try(ctx)
    {
        buffer = fz_new_buffer(ctx, 0);
        memory_output = fz_new_output_with_buffer(ctx, buffer);
        pdf_write_document(ctx, document, memory_output, &options);
        fz_close_output(ctx, memory_output);
        buffer_size = fz_buffer_storage(ctx, buffer, &buffer_data);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status status = quantapdf_status_from_mupdf(caught_code);
        quantapdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return status;
    }

    if (buffer_data == NULL || buffer_size == 0) {
        quantapdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return QUANTAPDF_ERROR_MUPDF;
    }

    result = (quantapdf_output *)calloc(1, sizeof(*result));
    if (result == NULL) {
        quantapdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return QUANTAPDF_ERROR_NOMEM;
    }

    result->data = (unsigned char *)malloc(buffer_size);
    if (result->data == NULL) {
        quantapdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        free(result);
        return QUANTAPDF_ERROR_NOMEM;
    }

    memcpy(result->data, buffer_data, buffer_size);
    result->size = buffer_size;
    quantapdf_drop_pdf_serialization_state(ctx, buffer, memory_output);

    *out_output = result;
    return QUANTAPDF_OK;
}
```

The helper owns only buffer/output serialization state plus the newly allocated output snapshot. It must never drop the supplied `pdf_document` or context.

- [ ] **Step 3: Refactor `src/pdf_export.c` to call the helper**

Change its include to:

```c
#include "pdf_internal.h"

#include <limits.h>
```

Remove only the old serialization locals and allocation/copy block:

```text
fz_buffer *buffer
fz_output *memory_output
quantapdf_output *result
buffer_data
buffer_size
pdf_write_options options
calloc/malloc/memcpy serialization code
```

Reduce the export cleanup helper to destination/graft ownership only:

```c
static void quantapdf_drop_pdf_export_state(
    fz_context *ctx,
    pdf_document *destination,
    pdf_graft_map *graft)
{
    if (graft != NULL)
        pdf_drop_graft_map(ctx, graft);
    if (destination != NULL)
        pdf_drop_document(ctx, destination);
}
```

Keep the existing argument/PDF/page-count/all-indices-prevalidated logic exactly as-is. Keep the existing destination/graft loop exactly as-is. After grafting succeeds, replace the old writer block with:

```c
{
    quantapdf_status status = quantapdf_serialize_pdf(
        ctx, destination, out_output);
    quantapdf_drop_pdf_export_state(ctx, destination, graft);
    return status;
}
```

On graft failure, continue mapping the caught MuPDF error and clean up destination/graft before returning. Do not alter caller-order, duplicate-index, unsupported-format, or bounds semantics.

- [ ] **Step 4: Register `src/pdf_output.c` in the library**

In root `CMakeLists.txt`, add `src/pdf_output.c` beside the other PDF composition files. Do not add `src/pdf_merge.c` yet.

The tail should conceptually be:

```cmake
  src/links.c
  src/pdf_export.c
  src/pdf_output.c
  src/pdf_range.c)
```

- [ ] **Step 5: Build and run the export characterization gate**

With the same dependency configuration as CI, build only the existing export target so the intentionally missing merge symbol does not block this refactor check:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --target quantapdf_test_pdf_export --parallel 2
ctest --test-dir build -R '^quantapdf\.pdf_export$' --output-on-failure
```

Expected: `quantapdf.pdf_export` passes, including its repeated-export byte identity and source-independent output lifetime checks.

Also build the merge target once and confirm it is still the same intentional RED because the merge API remains absent:

```bash
cmake --build build --target quantapdf_test_pdf_merge --parallel 2
```

Expected: failure only at the missing `quantapdf_merge_outputs` declaration/symbol boundary.

- [ ] **Step 6: Commit the serializer refactor**

```bash
git add src/pdf_internal.h src/pdf_output.c src/pdf_export.c CMakeLists.txt
git commit -m "refactor: share deterministic PDF serializer"
```

Do not claim merge GREEN after this commit.

---

### Task 3: Implement immutable-output merge and turn the RED GREEN

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_merge.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `quantapdf_serialize_pdf(...)` from Task 2 and the private immutable bytes in `struct quantapdf_output`.
- Produces: public `quantapdf_merge_outputs(...)` with exact-order whole-document append, strict atomicity, duplicate-input support, single-input support, `INT_MAX` page-total guard, and input-independent result lifetime.

- [ ] **Step 1: Add the public declaration only at the established composition surface**

In `include/quantapdf/quantapdf.h`, place the declaration after `quantapdf_export_page_range(...)` and before `quantapdf_output_data(...)`:

```c
QUANTAPDF_API quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output);
```

No new public structs, enums, MuPDF types, or filename parameters are added.

- [ ] **Step 2: Implement one source-local merge helper in `src/pdf_merge.c`**

Create the file with private PDF includes and a local silent-log callback matching the library's existing no-stderr behavior:

```c
#include "pdf_internal.h"

#include <limits.h>
#include <stddef.h>

static void quantapdf_merge_discard_log(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static quantapdf_status quantapdf_merge_one_output(
    fz_context *ctx,
    pdf_document *destination,
    const quantapdf_output *input,
    int *total_page_count)
{
    fz_stream *stream = NULL;
    pdf_document *source = NULL;
    pdf_graft_map *graft = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    int source_page_count = 0;
    int new_total = *total_page_count;
    int caught_code = FZ_ERROR_NONE;
    int page;

    fz_var(stream);
    fz_var(source);
    fz_var(graft);
    fz_var(status);
    fz_var(source_page_count);
    fz_var(new_total);
    fz_var(caught_code);

    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, input->data, input->size);
        source = pdf_open_document_with_stream(ctx, stream);
        source_page_count = pdf_count_pages(ctx, source);

        if (source_page_count < 0) {
            status = QUANTAPDF_ERROR_MUPDF;
        } else if (source_page_count > INT_MAX - *total_page_count) {
            status = QUANTAPDF_ERROR_ARGUMENT;
        } else {
            new_total = *total_page_count + source_page_count;
            graft = pdf_new_graft_map(ctx, destination);
            for (page = 0; page < source_page_count; ++page)
                pdf_graft_mapped_page(ctx, graft, -1, source, page);
        }
    }
    fz_always(ctx)
    {
        if (graft != NULL)
            pdf_drop_graft_map(ctx, graft);
        if (source != NULL)
            pdf_drop_document(ctx, source);
        if (stream != NULL)
            fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    if (status != QUANTAPDF_OK)
        return status;

    *total_page_count = new_total;
    return QUANTAPDF_OK;
}
```

The total is updated only after the complete source graft succeeds. If page 2 of a source throws, the helper returns an error without advancing the running total; the caller then discards the entire private destination, preserving operation atomicity.

- [ ] **Step 3: Implement the public all-or-nothing merge function**

Continue `src/pdf_merge.c` with:

```c
quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output)
{
    fz_context *ctx = NULL;
    pdf_document *destination = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    int total_page_count = 0;
    int caught_code = FZ_ERROR_NONE;
    size_t i;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (inputs == NULL || input_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    for (i = 0; i < input_count; ++i) {
        if (inputs[i] == NULL)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    fz_set_error_callback(ctx, quantapdf_merge_discard_log, NULL);
    fz_set_warning_callback(ctx, quantapdf_merge_discard_log, NULL);

    fz_var(destination);
    fz_var(caught_code);

    fz_try(ctx)
    {
        destination = pdf_create_document(ctx);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        status = quantapdf_status_from_mupdf(caught_code);
        fz_drop_context(ctx);
        return status;
    }

    for (i = 0; i < input_count; ++i) {
        status = quantapdf_merge_one_output(
            ctx, destination, inputs[i], &total_page_count);
        if (status != QUANTAPDF_OK) {
            pdf_drop_document(ctx, destination);
            fz_drop_context(ctx);
            return status;
        }
    }

    status = quantapdf_serialize_pdf(ctx, destination, out_output);
    pdf_drop_document(ctx, destination);
    fz_drop_context(ctx);
    return status;
}
```

Do not add a one-input fast path or special duplicate handling. The generic loop must naturally satisfy both contracts.

- [ ] **Step 4: Register `src/pdf_merge.c`**

Update root `CMakeLists.txt` so the PDF section is:

```cmake
  src/pdf_export.c
  src/pdf_output.c
  src/pdf_range.c
  src/pdf_merge.c)
```

The exact order is not ABI-significant; keep the grouping readable and do not touch unrelated sources.

- [ ] **Step 5: Build the merge target and verify the original RED turns GREEN**

Run:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --target quantapdf_test_pdf_merge --parallel 2
ctest --test-dir build -R '^quantapdf\.pdf_merge$' --output-on-failure
```

Expected: `quantapdf.pdf_merge` passes all contract cases from Task 1.

- [ ] **Step 6: Run all normal Linux tests**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: every existing test remains green, including:

```text
quantapdf.pdf_export
quantapdf.pdf_range
quantapdf.pdf_order
quantapdf.pdf_delete
quantapdf.pdf_merge
```

The export test's byte-determinism assertion is the regression proof that serializer extraction did not drift the old writer path.

- [ ] **Step 7: Run Linux ASan/UBSan from a fresh sanitizer build directory**

```bash
cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: all sanitizer CTests pass with no sanitizer diagnostics.

- [ ] **Step 8: Commit the GREEN implementation**

```bash
git add \
  include/quantapdf/quantapdf.h \
  src/pdf_merge.c \
  CMakeLists.txt
git commit -m "feat: merge immutable PDF outputs"
```

Do not fold the RED or serializer-refactor commits into this commit during normal execution; the three-step history is useful evidence:

```text
test RED
refactor shared serializer
feature GREEN
```

---

### Task 4: Exact-head CI, cross-platform checkpoint, and roadmap evidence

**Files:**
- No production/test source changes expected.
- Update the draft Merge PR created in Task 1, issue #27, and umbrella #2 only after exact-head verification succeeds.

**Interfaces:**
- Consumes: the exact GREEN head from Task 3.
- Produces: auditable Linux GREEN + Linux/macOS/Windows architecture-checkpoint evidence, with #27 and #2 updated but all stacked PRs still unmerged.

- [ ] **Step 1: Verify the PR stack and exact feature diff before trusting CI**

Capture the implementation head once Task 3 is committed:

```bash
GREEN_SHA=$(git rev-parse HEAD)
printf '%s\n' "$GREEN_SHA"
```

Then confirm the draft Merge PR created in Task 1 remains:

```text
base ref: test/pdf-delete-contract
base sha: 18a6c0596b68c1ca7b116624a09e27dcf0ac4a7f
head ref: feat/pdf-merge-outputs
head sha: exactly the GREEN_SHA printed above
draft: true
```

The feature diff should contain only:

```text
docs/superpowers/specs/2026-08-28-quantapdf-pdf-merge-outputs-design.md
docs/superpowers/plans/2026-08-28-quantapdf-pdf-merge-outputs.md
include/quantapdf/quantapdf.h
src/pdf_internal.h
src/pdf_output.c
src/pdf_export.c
src/pdf_merge.c
CMakeLists.txt
tests/test_pdf_merge.c
tests/CMakeLists.txt
```

Any unrelated file is a scope blocker and must be removed before the final verification run.

- [ ] **Step 2: Verify the exact GREEN SHA's normal PR workflow**

Read the workflow run attached to `GREEN_SHA`. Require Linux success for every step already defined in `.github/workflows/ci.yml`:

```text
Configure static build
Build static library and tests
Test static build
Configure sanitizer build
Build sanitizer configuration
Test sanitizer configuration
```

macOS/Windows may be skipped on this normal pull-request synchronization run; do not call the architecture checkpoint complete yet.

- [ ] **Step 3: Update the draft PR body with RED/GREEN evidence before triggering full-ci**

Record:

```text
RED exact SHA + workflow number/run id + intended missing-symbol failure
GREEN exact SHA + workflow number/run id + Linux normal/sanitizer success
public ABI
one-temp-context / one-graft-map-per-source architecture
shared serializer requirement
preservation boundary
no live-document/context crossing
```

Keep the PR draft and unmerged.

- [ ] **Step 4: Apply the existing `full-ci` label to the draft PR**

The existing workflow is already configured so a pull-request `labeled` event with label `full-ci` runs macOS and Windows in addition to Linux. Do not edit `.github/workflows/ci.yml` for this feature.

Require the triggered run to use the same exact `GREEN_SHA` verified in Step 2.

- [ ] **Step 5: Verify every full-ci platform on the same exact head**

Require:

```text
Linux:
  static configure/build/tests ✅
  ASan/UBSan configure/build/tests ✅

macOS:
  configure/build/tests ✅

Windows:
  DLL configure/build/tests ✅
  quantapdf_test_pdf_merge links and runs against the shared library ✅
```

If Windows fails because the new public function is not exported, verify the declaration carries `QUANTAPDF_API` before considering any broader fix. If a platform reveals a real implementation defect, use systematic debugging and add the smallest regression test/fix; then repeat exact-head Linux and full-ci verification on the new SHA.

- [ ] **Step 6: Update issue #27 with completion evidence**

Keep #27 open only for stacked integration bookkeeping. Record:

```text
implementation PR number returned when Task 1 opened the draft Merge PR
exact GREEN SHA
RED workflow evidence
Linux GREEN workflow evidence
same-head full-ci evidence
final production/file scope
preservation boundary
atomicity and lifetime guarantees
```

- [ ] **Step 7: Update umbrella #2 only after full-ci is green**

Read the concrete numeric PR number returned when Task 1 opened the draft Merge PR. Replace the open `Merge pages/documents` checklist line with a checked line containing that exact number together with `#27`. Do not use a symbolic marker in the persisted issue body.

Add one concise Phase 4 merge-proof paragraph with `GREEN_SHA` and the same-head full-ci workflow evidence. Leave `Save/write with explicit ownership and error handling` open.

- [ ] **Step 8: Final completion gate**

Freshly re-read:

1. PR state/head/base: still draft, open, unmerged, and head SHA exactly equals `GREEN_SHA`;
2. exact-head full-ci jobs: Linux/macOS/Windows all successful;
3. compare against PR #26 base: only the scoped spec/plan/API/PDF implementation/test/CMake files above;
4. issue #27 and umbrella #2: evidence matches `GREEN_SHA` and no unverified claim is present.

Only then report the Merge slice implementation complete. Do not merge #20/#22/#24/#26 or the Merge PR without explicit authorization.
