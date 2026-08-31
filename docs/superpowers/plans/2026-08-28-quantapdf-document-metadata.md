# QuantaPDF Typed PDF Document Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add typed access to the eight standard PDF Info dictionary fields with exact empty-vs-absent, PDF-only, raw-date, malformed-value, and copied UTF-8 ownership semantics.

**Architecture:** Reuse the existing opaque `quantapdf_document`. The public ABI adds one explicit metadata-field enum and one getter. `src/pdf_metadata.c` privately down-casts with `pdf_specifics()`, scans dictionaries by key presence so explicit PDF null values are not confused with missing keys, validates `/Info` and selected-value types, decodes PDF text strings, and publishes only an QuantaPDF-owned UTF-8 copy.

**Tech Stack:** C11, MuPDF 1.28.2 pinned through the existing vcpkg overlay, CMake/CTest, Linux ASan/UBSan, Windows DLL CI, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-28-quantapdf-document-metadata-design.md`

## Global Constraints

- Base is integrated `master` exact SHA `86cd429b03ab06396b9a511477b806517fe63130`.
- Work on `feat/document-metadata`; child issue is #31; umbrella is #2.
- Public scope is only `/Title`, `/Author`, `/Subject`, `/Keywords`, `/Creator`, `/Producer`, `/CreationDate`, `/ModDate` from the PDF Info dictionary.
- No XMP merge, custom Info keys, format/version, encryption, permissions, page labels, setters, generic metadata string keys, or new public PDF document handle.
- Missing `/Info` or missing selected key -> `QUANTAPDF_OK`, `NULL`, `0`.
- Present empty PDF string -> `QUANTAPDF_OK`, allocated `""`, `0`.
- Present `/Info` that is not a dictionary -> `QUANTAPDF_ERROR_FORMAT`.
- Present selected value that is not a PDF string, including explicit PDF null or numeric values -> `QUANTAPDF_ERROR_FORMAT`.
- Dates are returned as raw decoded PDF text; no parsing or normalization.
- Returned strings are NUL-terminated QuantaPDF-owned `malloc` memory; `out_size` excludes NUL; caller uses `quantapdf_free()`; successful results outlive the document.
- Reset supplied outputs before validation/fallible work; publish no partial result.
- Keep MuPDF types private. Do not modify `src/document.c`, `src/internal.h`, Phase 4 composition/output files, or existing Page/Render/Text/Search/Image/Links implementations.
- Do not add `quantapdf_pdf_document`, a metadata snapshot, or a generic private PDF-require helper in this first read slice.
- Preserve a real RED commit before any public declaration or production implementation.
- Final acceptance requires the exact GREEN head to pass Linux static/all CTests, Linux ASan/UBSan/all CTests, macOS configure/build/test, and Windows DLL configure/build/test.

---

## File Structure

**Create**
- `tests/fixtures/metadata-info.pdf` — deterministic one-page PDF Info fixture.
- `tests/test_pdf_metadata.c` — public contract test.
- `src/pdf_metadata.c` — focused PDF Info reader.

**Modify**
- `tests/CMakeLists.txt` — metadata CTest and Windows DLL-copy registration.
- `include/quantapdf/quantapdf.h` — one enum and one getter during GREEN.
- `CMakeLists.txt` — register `src/pdf_metadata.c` during GREEN.

**Reuse unchanged**
- `src/pdf_internal.h` — private `<mupdf/pdf.h>` boundary.
- `src/text.c` — `malloc` + NUL + `quantapdf_free()` ownership precedent.
- `tests/fixtures/composition-non-pdf.txt` — non-PDF `UNSUPPORTED` fixture.

---

### Task 1: Strict RED for typed PDF metadata

**Files:**
- Create: `tests/fixtures/metadata-info.pdf`
- Create: `tests/test_pdf_metadata.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `quantapdf_open`, `quantapdf_close`, `quantapdf_free`.
- Produces: failing references to `quantapdf_metadata_field` and `quantapdf_document_metadata(...)` only; no production API exists yet.

- [ ] **Step 1: Create the deterministic binary PDF fixture**

Run once and check in the resulting file:

```bash
python - <<'PY'
from pathlib import Path

pdf = (
    b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
    b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
    b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
    b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>\nendobj\n"
    b"4 0 obj\n<<\n"
    b"/Title <FEFF0050006800610073006500200035002000430061006600E9>\n"
    b"/Author (QuantaPDF Test)\n"
    b"/Subject ()\n"
    b"/Creator (Metadata Fixture)\n"
    b"/Producer 42\n"
    b"/CreationDate (D:20260828123456+09'00')\n"
    b"/ModDate (D:20260828124500+09'00')\n"
    b">>\nendobj\n"
    b"xref\n0 5\n"
    b"0000000000 65535 f \n"
    b"0000000015 00000 n \n"
    b"0000000064 00000 n \n"
    b"0000000121 00000 n \n"
    b"0000000192 00000 n \n"
    b"trailer\n<< /Size 5 /Root 1 0 R /Info 4 0 R >>\n"
    b"startxref\n429\n%%EOF\n"
)

Path("tests/fixtures/metadata-info.pdf").write_bytes(pdf)
assert len(pdf) == 604
PY
```

Locked values:

```text
Title        = UTF-16BE text for "Phase 5 Café"
Author       = "QuantaPDF Test"
Subject      = present empty string
Keywords     = absent
Creator      = "Metadata Fixture"
Producer     = integer 42
CreationDate = "D:20260828123456+09'00'"
ModDate      = "D:20260828124500+09'00'"
```

- [ ] **Step 2: Write the failing public contract test**

Create `tests/test_pdf_metadata.c`:

```c
#include <quantapdf/quantapdf.h>

#include <stdint.h>
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

static void expect_value(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    const char *expected,
    size_t expected_size)
{
    char *value = (char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_document_metadata(document, field, &value, &size) ==
          QUANTAPDF_OK);
    CHECK(value != NULL);
    CHECK(size == expected_size);
    CHECK(memcmp(value, expected, expected_size) == 0);
    CHECK(value[expected_size] == '\0');
    quantapdf_free(value);
}

static void expect_missing(
    quantapdf_document *document,
    quantapdf_metadata_field field)
{
    char *value = (char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_document_metadata(document, field, &value, &size) ==
          QUANTAPDF_OK);
    CHECK(value == NULL);
    CHECK(size == 0);
}

int main(void)
{
    static const char title[] = "Phase 5 Caf\xC3\xA9";
    static const char creation[] = "D:20260828123456+09'00'";
    static const char modification[] = "D:20260828124500+09'00'";
    quantapdf_document *document = NULL;
    quantapdf_document *non_pdf = NULL;
    char *value;
    size_t size;

    CHECK(quantapdf_open(METADATA_PDF, NULL, &document) == QUANTAPDF_OK);

    expect_value(document, QUANTAPDF_METADATA_TITLE, title, sizeof(title) - 1);
    CHECK(sizeof(title) - 1 == 13);
    expect_value(document, QUANTAPDF_METADATA_AUTHOR,
                 "QuantaPDF Test", sizeof("QuantaPDF Test") - 1);
    expect_value(document, QUANTAPDF_METADATA_CREATOR,
                 "Metadata Fixture", sizeof("Metadata Fixture") - 1);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_SUBJECT,
                                       &value, &size) == QUANTAPDF_OK);
    CHECK(value != NULL);
    CHECK(size == 0);
    CHECK(value[0] == '\0');
    quantapdf_free(value);

    expect_missing(document, QUANTAPDF_METADATA_KEYWORDS);
    expect_value(document, QUANTAPDF_METADATA_CREATION_DATE,
                 creation, sizeof(creation) - 1);
    expect_value(document, QUANTAPDF_METADATA_MODIFICATION_DATE,
                 modification, sizeof(modification) - 1);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_PRODUCER,
                                       &value, &size) == QUANTAPDF_ERROR_FORMAT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(document, (quantapdf_metadata_field)0,
                                       &value, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(document, (quantapdf_metadata_field)99,
                                       &value, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(NULL, QUANTAPDF_METADATA_TITLE,
                                       &value, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_TITLE,
                                       NULL, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_TITLE,
                                       &value, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);

    CHECK(quantapdf_open(COMPOSITION_NON_PDF, NULL, &non_pdf) == QUANTAPDF_OK);
    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(quantapdf_document_metadata(non_pdf, QUANTAPDF_METADATA_TITLE,
                                       &value, &size) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(value == NULL);
    CHECK(size == 0);
    quantapdf_close(non_pdf);

    value = NULL;
    size = 0;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_TITLE,
                                       &value, &size) == QUANTAPDF_OK);
    CHECK(value != NULL);
    CHECK(size == sizeof(title) - 1);
    quantapdf_close(document);
    CHECK(memcmp(value, title, sizeof(title) - 1) == 0);
    CHECK(value[sizeof(title) - 1] == '\0');
    quantapdf_free(value);

    return EXIT_SUCCESS;
}
```

No public enum/declaration or production implementation is allowed in this commit.

- [ ] **Step 3: Register the RED target and Windows DLL copy**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(quantapdf_test_pdf_metadata test_pdf_metadata.c)
target_link_libraries(quantapdf_test_pdf_metadata PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_metadata PRIVATE
  METADATA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/metadata-info.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
add_test(NAME quantapdf.pdf_metadata COMMAND quantapdf_test_pdf_metadata)
set_tests_properties(quantapdf.pdf_metadata PROPERTIES TIMEOUT 30)
```

Also add `quantapdf_test_pdf_metadata` to the existing Windows shared-library `foreach(test_target IN ITEMS ...)` list. This is test wiring, not production behavior.

- [ ] **Step 4: Build and verify the intended RED**

Use the repository’s existing Linux/static configure/build command, ending with:

```bash
cmake --build build --parallel 2
```

Accept only this boundary:

```text
QuantaPDF library builds
all pre-existing test targets build/link
quantapdf_test_pdf_metadata fails because the metadata enum/API are absent
```

Fixture parse errors or unrelated target failures are not valid RED evidence.

- [ ] **Step 5: Commit and capture RED identity**

```bash
git add tests/fixtures/metadata-info.pdf tests/test_pdf_metadata.c tests/CMakeLists.txt
git commit -m "test: define typed PDF metadata contract"
RED_SHA=$(git rev-parse HEAD)
printf '%s\n' "$RED_SHA"
```

Keep the printed SHA in the execution notes.

- [ ] **Step 6: Push and create a draft PR against master**

Create one draft PR from `feat/document-metadata` to `master`, referencing #31 and #2. Save the numeric PR number returned by GitHub as `PR_NUMBER` in execution notes; do not predict it.

State in the PR body that the head contains no metadata public declaration or production implementation.

- [ ] **Step 7: Verify exact-head CI and capture the RED run**

Inspect workflow runs associated with `RED_SHA`. Save the numeric run ID returned by GitHub as `RED_RUN_ID` in execution notes.

Require:

```text
MuPDF install                  ✅
configure                      ✅
library                        ✅
all pre-existing test targets  ✅ build/link
metadata target                ❌ only on absent enum/API
```

If anything else fails, stop and use systematic debugging before Task 2.

---

### Task 2: Minimal typed PDF Info reader GREEN

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_metadata.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_metadata.c`

**Interfaces:**
- Consumes: existing `src/pdf_internal.h`, `quantapdf_document->{ctx,doc}`, `quantapdf_status_from_mupdf(...)`, `quantapdf_free()` ownership convention.
- Produces only the following public surface:

```c
typedef enum quantapdf_metadata_field {
    QUANTAPDF_METADATA_TITLE = 1,
    QUANTAPDF_METADATA_AUTHOR = 2,
    QUANTAPDF_METADATA_SUBJECT = 3,
    QUANTAPDF_METADATA_KEYWORDS = 4,
    QUANTAPDF_METADATA_CREATOR = 5,
    QUANTAPDF_METADATA_PRODUCER = 6,
    QUANTAPDF_METADATA_CREATION_DATE = 7,
    QUANTAPDF_METADATA_MODIFICATION_DATE = 8
} quantapdf_metadata_field;

QUANTAPDF_API quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);
```

- [ ] **Step 1: Add the enum and getter declaration after RED is proven**

Add exactly the enum and declaration above to `include/quantapdf/quantapdf.h`. Add no setters, generic string-key API, new handle, XMP API, format getter, or encryption getter.

- [ ] **Step 2: Create the field-to-PDF-key mapper**

Start `src/pdf_metadata.c` with:

```c
#include "pdf_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pdf_obj *quantapdf_metadata_key(quantapdf_metadata_field field)
{
    switch (field) {
    case QUANTAPDF_METADATA_TITLE: return PDF_NAME(Title);
    case QUANTAPDF_METADATA_AUTHOR: return PDF_NAME(Author);
    case QUANTAPDF_METADATA_SUBJECT: return PDF_NAME(Subject);
    case QUANTAPDF_METADATA_KEYWORDS: return PDF_NAME(Keywords);
    case QUANTAPDF_METADATA_CREATOR: return PDF_NAME(Creator);
    case QUANTAPDF_METADATA_PRODUCER: return PDF_NAME(Producer);
    case QUANTAPDF_METADATA_CREATION_DATE: return PDF_NAME(CreationDate);
    case QUANTAPDF_METADATA_MODIFICATION_DATE: return PDF_NAME(ModDate);
    default: return NULL;
    }
}
```

The switch is the valid-field authority.

- [ ] **Step 3: Add a dictionary scan that preserves key presence**

Do not use `pdf_dict_get(...)` alone to decide absence because the contract distinguishes an absent key from an explicitly present PDF null.

```c
static int quantapdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count = pdf_dict_len(ctx, dictionary);
    int index;

    *out_value = NULL;
    for (index = 0; index < count; ++index) {
        pdf_obj *candidate = pdf_dict_get_key(ctx, dictionary, index);
        if (pdf_name_eq(ctx, candidate, key)) {
            *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}
```

Use this helper for both trailer `/Info` presence and selected-field presence.

- [ ] **Step 4: Implement reset-first, PDF-only, exact type validation, and copied UTF-8**

Implement:

```c
quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    fz_context *ctx;
    pdf_document *pdf = NULL;
    pdf_obj *trailer = NULL;
    pdf_obj *info = NULL;
    pdf_obj *value = NULL;
    pdf_obj *key;
    const char *text = NULL;
    char *copy;
    size_t text_size = 0;
    int info_present = 0;
    int value_present = 0;
    int malformed_info = 0;
    int malformed_value = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    key = quantapdf_metadata_key(field);
    if (document == NULL || out_utf8 == NULL || out_size == NULL || key == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    ctx = document->ctx;

    fz_var(pdf);
    fz_var(trailer);
    fz_var(info);
    fz_var(value);
    fz_var(text);
    fz_var(text_size);
    fz_var(info_present);
    fz_var(value_present);
    fz_var(malformed_info);
    fz_var(malformed_value);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, document->doc);
        if (pdf != NULL) {
            trailer = pdf_trailer(ctx, pdf);
            if (trailer != NULL)
                info_present = quantapdf_pdf_dict_find(
                    ctx, trailer, PDF_NAME(Info), &info);

            if (info_present) {
                if (!pdf_is_dict(ctx, info)) {
                    malformed_info = 1;
                } else {
                    value_present = quantapdf_pdf_dict_find(
                        ctx, info, key, &value);
                    if (value_present) {
                        if (!pdf_is_string(ctx, value)) {
                            malformed_value = 1;
                        } else {
                            text = pdf_to_text_string(ctx, value);
                            text_size = strlen(text);
                        }
                    }
                }
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    if (pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (malformed_info || malformed_value)
        return QUANTAPDF_ERROR_FORMAT;
    if (!info_present || !value_present)
        return QUANTAPDF_OK;
    if (text_size == SIZE_MAX)
        return QUANTAPDF_ERROR_NOMEM;

    copy = (char *)malloc(text_size + 1);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (text_size != 0)
        memcpy(copy, text, text_size);
    copy[text_size] = '\0';

    *out_utf8 = copy;
    *out_size = text_size;
    return QUANTAPDF_OK;
}
```

Review requirements for this exact implementation:

- `pdf_specifics()` is borrowed; do not keep/drop it separately.
- Presence is tracked separately from value pointer so explicit PDF null is malformed rather than missing.
- `pdf_to_text_string()` is called only for a PDF string.
- No `fz_lookup_metadata()` call is allowed.
- No date normalization is allowed.
- No MuPDF pointer is returned.

- [ ] **Step 5: Register only `src/pdf_metadata.c` in root CMake**

Add:

```cmake
  src/pdf_metadata.c
```

to the existing `add_library(quantapdf ...)` list. Keep `src/document.c`, `src/internal.h`, and composition files unchanged.

- [ ] **Step 6: Build and run focused metadata CTest**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^quantapdf\.pdf_metadata$'
```

Expected: PASS.

- [ ] **Step 7: Run all normal Linux CTests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 8: Run the Linux ASan/UBSan suite**

Use the repository’s existing sanitizer configure flags, then:

```bash
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: all sanitizer tests pass.

- [ ] **Step 9: Commit and capture GREEN identity**

```bash
git add include/quantapdf/quantapdf.h src/pdf_metadata.c CMakeLists.txt
git commit -m "feat: expose typed PDF Info metadata"
GREEN_SHA=$(git rev-parse HEAD)
printf '%s\n' "$GREEN_SHA"
```

Do not amend the RED commit.

- [ ] **Step 10: Push and verify exact-head Linux CI**

Inspect workflow runs associated with `GREEN_SHA`. Save the numeric successful Linux run ID as `GREEN_RUN_ID` in execution notes.

Require on that exact SHA:

```text
Linux static configure/build  ✅
all normal CTests             ✅
ASan/UBSan configure/build    ✅
all sanitizer CTests          ✅
```

No code change is allowed after this point before the full-ci checkpoint.

---

### Task 3: Same-head cross-platform checkpoint and bookkeeping

**Files:**
- No production/test file changes expected.
- Update only the draft PR created in Task 1, issue #31, and umbrella #2 with observed evidence.

**Interfaces:**
- Consumes: `PR_NUMBER`, `RED_SHA`, `RED_RUN_ID`, `GREEN_SHA`, `GREEN_RUN_ID` captured during Tasks 1-2.
- Produces: same-head Linux/macOS/Windows evidence; no merge.

- [ ] **Step 1: Verify final file scope against integrated master**

```bash
git diff --name-only 86cd429b03ab06396b9a511477b806517fe63130...HEAD
```

The feature diff must contain only:

```text
docs/superpowers/specs/2026-08-28-quantapdf-document-metadata-design.md
docs/superpowers/plans/2026-08-28-quantapdf-document-metadata.md
include/quantapdf/quantapdf.h
src/pdf_metadata.c
CMakeLists.txt
tests/fixtures/metadata-info.pdf
tests/test_pdf_metadata.c
tests/CMakeLists.txt
```

Reject unrelated source, dependency, or workflow changes.

- [ ] **Step 2: Review architectural leakage from the actual diff**

Confirm:

```text
one public enum
one public getter
no MuPDF public type
no setter
no XMP
no format/encryption getter
no generic metadata string key
no new public handle
no document-layout change
presence scan used instead of fz_lookup_metadata/simple missing-value lookup
```

- [ ] **Step 3: Update the draft PR using captured evidence**

Use the numeric PR identified by `PR_NUMBER`. Insert the exact values already captured as `RED_SHA`, `RED_RUN_ID`, `GREEN_SHA`, and `GREEN_RUN_ID`; do not substitute guessed numbers.

The PR body must state:

```text
RED: exact RED_SHA; run RED_RUN_ID; only metadata target failed because enum/API were absent.
GREEN: exact GREEN_SHA; run GREEN_RUN_ID; Linux static/all tests and ASan/UBSan/all tests passed.
```

- [ ] **Step 4: Apply `full-ci` without changing the head**

Apply the existing `full-ci` label to the draft PR while its head is still exactly `GREEN_SHA`.

- [ ] **Step 5: Verify all jobs use `GREEN_SHA` and succeed**

Save the numeric full-ci run ID as `FULL_CI_RUN_ID` in execution notes. Require:

```text
Linux static + tests        ✅
Linux ASan/UBSan + tests    ✅
macOS configure/build/test  ✅
Windows DLL build/test      ✅
```

The Windows metadata CTest must execute through the shared-library build, enabled by the Task 1 DLL-copy-list change.

If any fix is needed, the new commit becomes the new `GREEN_SHA`; rerun Linux and full-ci on that same new SHA.

- [ ] **Step 6: Fresh final PR-state verification**

Read the PR from GitHub immediately before reporting completion and verify the actual state is:

```text
open
draft
unmerged
base = master
head = feat/document-metadata
head SHA = GREEN_SHA
```

Also record the actual mergeable state rather than guessing it.

- [ ] **Step 7: Update issue #31**

Add the draft PR number, `RED_SHA`, `RED_RUN_ID`, `GREEN_SHA`, `GREEN_RUN_ID`, `FULL_CI_RUN_ID`, contract proof, and exact final file scope.

Keep #31 open for integration bookkeeping.

- [ ] **Step 8: Update umbrella #2**

Mark only the `Document metadata` Phase 5 item implementation/evidence complete and record `GREEN_SHA` plus the three-platform proof. Leave outline, annotations, forms, mutation policy, Phase 5 overall, and Phase 6 open.

- [ ] **Step 9: Stop at the integration gate**

Report the exact `GREEN_SHA` and captured workflow IDs, then state that the PR remains draft/open/unmerged, #31 remains open for integration bookkeeping, and #2 remains open for later Phase 5/6.

Do not merge without explicit user authorization.
