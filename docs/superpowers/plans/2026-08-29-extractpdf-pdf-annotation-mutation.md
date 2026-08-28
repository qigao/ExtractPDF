# ExtractPDF PDF Annotation Mutation V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an isolated PDF annotation editor that keeps `extractpdf_document` immutable, uses session-local annotation refs for atomic create/update/delete, and emits deterministic non-consuming `extractpdf_output` snapshots.

**Architecture:** `extractpdf_pdf_edit_begin()` materializes a private full-PDF fork in its own MuPDF context, rejects encrypted/already-signed sources, disables JavaScript, and enables MuPDF journalling. Immutable annotation enumeration and editor discovery share one private classify/materialize implementation; editor refs map to private annotation objects through a non-recycled registry. Each CRUD call is one outer journal operation and `snapshot()` serializes the current fork into the existing immutable output abstraction without consuming the editor.

**Tech Stack:** C11, CMake 3.20+, pinned MuPDF 1.28.2 through the existing vcpkg overlay, CTest, Linux ASan/UBSan, macOS, Windows DLL.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotation-mutation-design.md`

**Base:** integrated master `58525ce1b4b691ef53dfafc7c4e4d82753c966ba`; branch `feat/pdf-annotation-mutation`; issue #37; roadmap #2.

## Global Constraints

- Keep `extractpdf_document`, `extractpdf_page`, and all existing immutable snapshots read-only; no mutation API writes through those handles.
- `extractpdf_pdf_edit_begin()` must become independent of the source lifetime after success.
- No MuPDF type, PDF object number/generation, `/NM`, filename, or pointer becomes public mutation identity.
- Immutable annotation indices remain snapshot-local/discovery-only and are never accepted by update/delete APIs.
- Editor discovery must use exactly the same Link/Popup/Widget filtering, UNKNOWN mapping, order, and strict Rect/F/Contents materialization contract as Annotation Enumeration V1.
- Public mutation bounds are finite normalized Fitz page-space rectangles. Reversed/non-finite input is `ARGUMENT`; V1 does not silently normalize input.
- V1 create/bounds-update supports only TEXT, FREE_TEXT, SQUARE, and CIRCLE.
- Recognized ordinary annotations may receive generic flags/Contents update and delete; UNKNOWN mutation is `UNSUPPORTED`.
- Preserve the complete raw `uint32_t` `/F` range. Never narrow values above `INT_MAX` through MuPDF's `int` convenience setter.
- Contents are counted UTF-8 with absent versus present-empty preserved. Present input must be valid UTF-8 and contain no embedded NUL.
- Every create/update/delete is one atomic outer MuPDF journal operation. Any failure restores pre-call PDF and ref-registry observable state.
- `snapshot()` is non-consuming, deterministic for unchanged editor state, leaves refs valid, and returns an independent existing `extractpdf_output`.
- Reject every encrypted source and every source with an already-signed signature value as `UNSUPPORTED`. Unsigned signature fields remain allowed.
- Disable JavaScript in the private editor before exposing it. Begin/CRUD/appearance update/snapshot must not execute PDF JavaScript.
- No public undo/redo, incremental save, forms/widgets mutation, link mutation, Popup API, QuadPoints/line/vertex/Ink geometry, persistent identity, encrypted editing, or signed-PDF editing in V1.
- Preserve reset-before-later-validation behavior for every new output pointer.
- New option/update structs use the existing forward-compatible minimum-`struct_size` convention; larger structs are accepted.
- Keep the current single-thread handle contract. Add no mutable process-global/TLS state.
- RED precedes every production declaration/behavior. Final feature head requires Linux static + all CTests, Linux ASan/UBSan + all CTests, macOS, and Windows DLL proof.

---

## File Structure

### Public/API

- Modify `include/extractpdf/extractpdf.h`
  - add opaque `extractpdf_pdf_edit`;
  - add value `extractpdf_annotation_ref`;
  - add update-field/create/update types;
  - append `EXTRACTPDF_ERROR_STATE = 8`;
  - add editor/discovery/getter/CRUD/snapshot/drop APIs.
- Modify `src/status.c`
  - add stable status text for `EXTRACTPDF_ERROR_STATE`.

### Shared private annotation semantics

- Create `src/pdf_annotation_common.h`
  - private `extractpdf_pdf_annotation_view`;
  - survivor classification and strict common-materialization declarations.
- Create `src/pdf_annotation_common.c`
  - move current subtype/filter/Rect/F/Contents logic out of `src/pdf_annotations.c` without changing behavior.
- Modify `src/pdf_annotations.c`
  - consume the shared helpers and retain only immutable snapshot allocation/copy/public accessors.

### Editor implementation

- Create `src/pdf_edit_internal.h`
  - private editor struct, ref registry, token helpers, page/object resolution helpers, test-fault field under `EXTRACTPDF_TESTING`.
- Create `src/pdf_edit.c`
  - begin policy checks, private source clone/context lifecycle, signed-field scan, JavaScript disable, journal enable, snapshot, drop.
- Create `src/pdf_edit_annotations.c`
  - editor discovery, ref registry, getters, UTF-8 validation/copy, create/update/delete, full-uint32 flags write, appearance update, journal rollback.
- Modify `CMakeLists.txt`
  - compile the three new production `.c` files plus the shared annotation module.

### Deterministic tests

- Create `tests/test_pdf_annotation_mutation.c`
  - one executable with named groups (`arguments`, `begin`, `discovery`, `create`, `update-delete`, `contents-flags`, `snapshot`, `javascript`) and no third-party test framework.
- Create `tests/pdf_edit_test_api.h`
  - private test-only fault enum/hook declaration; not installed and not included by the public header.
- Create `tests/pdf_edit_fault_hook.c`
  - test-only implementation compiled into `extractpdf` only when `EXTRACTPDF_BUILD_TESTS=ON`; fault state lives per editor, never globally.
- Create checked-in deterministic fixtures:
  - `tests/fixtures/annotation-mutation.pdf`
  - `tests/fixtures/annotation-mutation-signed.pdf`
  - `tests/fixtures/annotation-mutation-unsigned-signature.pdf`
  - `tests/fixtures/annotation-mutation-js.pdf`
- Reuse existing fixtures:
  - `tests/fixtures/annotations-late-malformed.pdf`
  - `tests/fixtures/encrypted-one-page.pdf` with password `user-pass`
  - `tests/fixtures/composition-non-pdf.txt`
- Modify `tests/CMakeLists.txt`
  - register the new executable/CTest, fixture paths, output paths, and Windows DLL-copy list;
  - conditionally add the private fault-hook source/definition to the library test build.

No page/render/text/image/link/outline/metadata/composition behavior is refactored as part of this work.

---

### Task 1: Capture the strict full-surface RED

**Files:**
- Create: `tests/fixtures/annotation-mutation.pdf`
- Create: `tests/fixtures/annotation-mutation-signed.pdf`
- Create: `tests/fixtures/annotation-mutation-unsigned-signature.pdf`
- Create: `tests/fixtures/annotation-mutation-js.pdf`
- Create: `tests/pdf_edit_test_api.h`
- Create: `tests/test_pdf_annotation_mutation.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: current public `extractpdf_document`, immutable annotation/output/metadata APIs.
- Produces: the complete wished-for Mutation V1 contract as failing C tests. No production declaration is added in this task.

- [ ] **Step 1: Generate the checked-in deterministic PDFs**

Use this one-shot authoring script from the repository root. The script is **not** committed; only its PDF outputs are committed. It writes plain deterministic PDF objects and computes exact xref offsets, so runtime tests remain pure CTest and have no Python dependency.

```bash
python3 - <<'PY'
from pathlib import Path

OUT = Path("tests/fixtures")


def write_pdf(path, objects, trailer_extra=""):
    parts = [b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"]
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(sum(len(p) for p in parts))
        parts.append(f"{number} 0 obj\n".encode("ascii"))
        parts.append(body.encode("latin1"))
        parts.append(b"\nendobj\n")
    xref = sum(len(p) for p in parts)
    parts.append(f"xref\n0 {len(objects)+1}\n".encode("ascii"))
    parts.append(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        parts.append(f"{offset:010d} 00000 n \n".encode("ascii"))
    parts.append(
        (f"trailer\n<< /Size {len(objects)+1} /Root 1 0 R {trailer_extra} >>\n"
         f"startxref\n{xref}\n%%EOF\n").encode("latin1")
    )
    path.write_bytes(b"".join(parts))


write_pdf(OUT / "annotation-mutation.pdf", [
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> "
    "/Annots [5 0 R 17 6 0 R 7 0 R 8 0 R 9 0 R 10 0 R 11 0 R 12 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> "
    "/Annots [13 0 R 14 0 R] >>",
    "<< /Type /Annot /Subtype /Text /Rect [10 20 30 40] /F 2147483649 "
    "/Contents (text-a) /Popup 9 0 R >>",
    "<< /Type /Annot /Subtype /Link /Rect [20 20 40 40] "
    "/A << /S /URI /URI (https://example.com/) >> >>",
    "<< /Type /Annot /Subtype /Square /Rect [40 50 70 80] /F 4 /Contents (square-b) >>",
    "<< /Type /Annot /Subtype /FutureThing /Rect [80 90 100 110] /F 64 /Contents (unknown-c) >>",
    "<< /Type /Annot /Subtype /Popup /Rect [110 90 180 150] /Parent 5 0 R >>",
    "<< /Type /Annot /Subtype /Widget /Rect [130 10 180 30] /FT /Tx /T (widget) >>",
    "<< /Type /Annot /Subtype /Highlight /Rect [20 120 80 140] "
    "/QuadPoints [20 140 80 140 20 120 80 120] /Contents (highlight-d) >>",
    "<< /Type /Annot /Subtype /Ink /Rect [90 120 150 160] "
    "/InkList [[90 120 120 140 150 160]] /Contents (ink-e) >>",
    "<< /Type /Annot /Subtype /FreeText /Rect [10 10 80 40] "
    "/DA (/Helv 12 Tf 0 g) /Contents (free-f) >>",
    "<< /Type /Annot /Subtype /Circle /Rect [100 20 150 70] /Contents (circle-g) >>",
])


def signature_pdf(path, signed):
    value = " /V 6 0 R" if signed else ""
    objects = [
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Annots [5 0 R] >>",
        "<< /Fields [5 0 R] /SigFlags 3 >>",
        f"<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Rect [10 10 120 40] /P 3 0 R{value} >>",
        "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
        "/ByteRange [0 0 0 0] /Contents <00> >>",
    ]
    write_pdf(path, objects)

signature_pdf(OUT / "annotation-mutation-signed.pdf", True)
signature_pdf(OUT / "annotation-mutation-unsigned-signature.pdf", False)

write_pdf(OUT / "annotation-mutation-js.pdf", [
    "<< /Type /Catalog /Pages 2 0 R /OpenAction 5 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Annots [6 0 R] >>",
    "<< /Title (SAFE) >>",
    "<< /S /JavaScript /JS (this.title = \\\"EXECUTED\\\";) >>",
    "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (js-safe) >>",
], "/Info 4 0 R")
PY
```

Verify the four files exist and are stable across a second run:

```bash
sha256sum tests/fixtures/annotation-mutation*.pdf
```

Expected: rerunning the generator produces the same four hashes.

- [ ] **Step 2: Add the private test-fault declaration without implementation**

Create `tests/pdf_edit_test_api.h`:

```c
#ifndef EXTRACTPDF_PDF_EDIT_TEST_API_H
#define EXTRACTPDF_PDF_EDIT_TEST_API_H

#include <extractpdf/extractpdf.h>

typedef enum extractpdf_test_pdf_edit_fault {
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_NONE = 0,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD = 1,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION = 2,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH = 3
} extractpdf_test_pdf_edit_fault;

EXTRACTPDF_API void extractpdf_test_pdf_edit_set_fault(
    extractpdf_pdf_edit *edit,
    extractpdf_test_pdf_edit_fault fault);

#endif
```

This is test-only surface. Its current compile failure is expected because `extractpdf_pdf_edit` is intentionally absent before RED is captured.

- [ ] **Step 3: Add the full mutation test executable**

Create `tests/test_pdf_annotation_mutation.c` with the existing lightweight `CHECK` style. Use an optional first CLI argument so individual groups can be run during incremental GREEN work while the umbrella CTest still runs all groups.

Required helpers:

```c
#include <extractpdf/extractpdf.h>
#include "pdf_edit_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static int close_float(float a, float b)
{
    float d = a - b;
    return (d < 0 ? -d : d) < 0.01f;
}

static void zero_ref(extractpdf_annotation_ref *ref)
{
    ref->opaque[0] = 0;
    ref->opaque[1] = 0;
}

static int ref_is_zero(const extractpdf_annotation_ref *ref)
{
    return ref->opaque[0] == 0 && ref->opaque[1] == 0;
}

static void save_output(const extractpdf_output *out, const char *path)
{
    CHECK(extractpdf_output_save_file(out, path) == EXTRACTPDF_OK);
}

static void expect_snapshot_annotation(
    const char *path,
    size_t index,
    extractpdf_annotation_type type,
    const char *contents)
{
    extractpdf_document *doc = NULL;
    extractpdf_page *page = NULL;
    extractpdf_annotation_page *annots = NULL;
    extractpdf_annotation_info info = {0};
    const char *text = NULL;
    size_t size = 0;

    CHECK(extractpdf_open(path, NULL, &doc) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(doc, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_annotations(page, &annots) == EXTRACTPDF_OK);
    info.struct_size = sizeof(info);
    CHECK(extractpdf_annotation_get_info(annots, index, &info) == EXTRACTPDF_OK);
    CHECK(info.type == type);
    CHECK(extractpdf_annotation_contents(annots, index, &text, &size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(size == strlen(contents));
    CHECK(memcmp(text, contents, size) == 0);

    extractpdf_drop_annotation_page(annots);
    extractpdf_drop_page(page);
    extractpdf_close(doc);
}
```

The RED file must contain these exact behavioral groups and assertions:

```c
static void test_arguments(void)
{
    int sentinel = 0;
    extractpdf_pdf_edit *edit = (extractpdf_pdf_edit *)&sentinel;
    extractpdf_annotation_ref ref = {{UINT64_MAX, UINT64_MAX}};
    extractpdf_output *out = (extractpdf_output *)&sentinel;
    size_t count = 99;

    CHECK(extractpdf_pdf_edit_begin(NULL, &edit) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(edit == NULL);
    CHECK(extractpdf_pdf_edit_begin(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_pdf_edit_annotation_count(NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(extractpdf_pdf_edit_annotation_count(NULL, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_pdf_edit_annotation_ref_at(NULL, 0, 0, &ref) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));

    out = (extractpdf_output *)&sentinel;
    CHECK(extractpdf_pdf_edit_snapshot(NULL, &out) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(out == NULL);
    CHECK(extractpdf_pdf_edit_snapshot(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    extractpdf_drop_pdf_edit(NULL);

    CHECK(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_STATE), "invalid state") == 0);
}

static void test_begin_lifetime_and_fail_closed(void)
{
    int sentinel = 0;
    extractpdf_document *source = NULL;
    extractpdf_pdf_edit *edit = (extractpdf_pdf_edit *)&sentinel;
    extractpdf_output *out = NULL;

    CHECK(extractpdf_open(MUTATION_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
    CHECK(edit != NULL);
    extractpdf_close(source);
    source = NULL;
    CHECK(extractpdf_pdf_edit_snapshot(edit, &out) == EXTRACTPDF_OK);
    CHECK(out != NULL);
    extractpdf_drop_output(out);
    extractpdf_drop_pdf_edit(edit);

    CHECK(extractpdf_open(ENCRYPTED_PDF, "user-pass", &source) == EXTRACTPDF_OK);
    edit = (extractpdf_pdf_edit *)&sentinel;
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(edit == NULL);
    extractpdf_close(source);

    CHECK(extractpdf_open(SIGNED_PDF, NULL, &source) == EXTRACTPDF_OK);
    edit = (extractpdf_pdf_edit *)&sentinel;
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(edit == NULL);
    extractpdf_close(source);

    CHECK(extractpdf_open(UNSIGNED_SIGNATURE_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
    extractpdf_drop_pdf_edit(edit);
    extractpdf_close(source);

    CHECK(extractpdf_open(NON_PDF, NULL, &source) == EXTRACTPDF_OK);
    edit = (extractpdf_pdf_edit *)&sentinel;
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(edit == NULL);
    extractpdf_close(source);
}
```

Discovery/ref group:

```c
static void test_discovery_and_refs(void)
{
    extractpdf_document *source = NULL;
    extractpdf_pdf_edit *a = NULL;
    extractpdf_pdf_edit *b = NULL;
    extractpdf_annotation_ref text = {{0,0}};
    extractpdf_annotation_ref square = {{0,0}};
    extractpdf_annotation_ref square_again = {{0,0}};
    extractpdf_annotation_ref wrong = {{0,0}};
    extractpdf_annotation_info info = {0};
    size_t count = 0;

    CHECK(extractpdf_open(MUTATION_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &a) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &b) == EXTRACTPDF_OK);

    CHECK(extractpdf_pdf_edit_annotation_count(a, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 6); /* Text, Square, UNKNOWN, Highlight, Ink + scalar filtering leaves 5? */
    /* Correct the fixture expectation explicitly below: survivors are Text, Square,
       FutureThing, Highlight, Ink = 5. */
    CHECK(extractpdf_pdf_edit_annotation_count(a, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 5);

    CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 0, &text) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 1, &square) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 1, &square_again) == EXTRACTPDF_OK);
    CHECK(memcmp(&square, &square_again, sizeof(square)) == 0);

    info.struct_size = sizeof(info);
    CHECK(extractpdf_pdf_edit_annotation_get_info(a, &square, &info) == EXTRACTPDF_OK);
    CHECK(info.type == EXTRACTPDF_ANNOTATION_SQUARE);
    CHECK(info.flags == 4u);

    wrong = square;
    CHECK(extractpdf_pdf_edit_annotation_get_info(b, &wrong, &info) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_pdf_edit_annotation_count(a, -1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    zero_ref(&wrong);
    CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 99, &wrong) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&wrong));

    extractpdf_drop_pdf_edit(b);
    extractpdf_drop_pdf_edit(a);
    extractpdf_close(source);
}
```

**Important correction while authoring:** do **not** leave the temporary `count == 6` assertion shown above in the committed test. The committed code must contain only `count == 5`. This note exists to make the logical count auditable: page 0 survivors are Text, Square, FutureThing/UNKNOWN, Highlight, Ink; scalar 17, Link, Popup, Widget are filtered.

Malformed-discovery atomicity must be in the same group:

```c
static void test_malformed_discovery_is_atomic(void)
{
    extractpdf_document *source = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_annotation_ref ref = {{UINT64_MAX, UINT64_MAX}};
    size_t count = 99;

    CHECK(extractpdf_open(LATE_MALFORMED_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_annotation_count(edit, 0, &count) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(count == 0);
    CHECK(extractpdf_pdf_edit_annotation_ref_at(edit, 0, 0, &ref) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(ref_is_zero(&ref));
    extractpdf_drop_pdf_edit(edit);
    extractpdf_close(source);
}
```

Create/update/delete group must cover all four create types and unsupported geometry. Use this common create helper:

```c
static extractpdf_annotation_ref create_rect_annot(
    extractpdf_pdf_edit *edit,
    int page,
    extractpdf_annotation_type type,
    extractpdf_rect bounds,
    uint32_t flags,
    const char *text,
    size_t size)
{
    extractpdf_annotation_create_options options = {0};
    extractpdf_annotation_ref ref = {{0,0}};
    options.struct_size = sizeof(options);
    options.type = type;
    options.bounds = bounds;
    options.flags = flags;
    options.contents_utf8 = text;
    options.contents_size = size;
    CHECK(extractpdf_pdf_edit_annotation_create(edit, page, &options, &ref) == EXTRACTPDF_OK);
    CHECK(!ref_is_zero(&ref));
    return ref;
}
```

In `test_create()` create TEXT, FREE_TEXT, SQUARE, CIRCLE with distinct rectangles/contents, then verify each through `extractpdf_pdf_edit_annotation_get_info()` and allocated `extractpdf_pdf_edit_annotation_contents()`. Also execute:

```c
options.type = EXTRACTPDF_ANNOTATION_HIGHLIGHT;
zero_ref(&ref);
CHECK(extractpdf_pdf_edit_annotation_create(edit, 0, &options, &ref) == EXTRACTPDF_ERROR_UNSUPPORTED);
CHECK(ref_is_zero(&ref));

options.type = EXTRACTPDF_ANNOTATION_UNKNOWN;
CHECK(extractpdf_pdf_edit_annotation_create(edit, 0, &options, &ref) == EXTRACTPDF_ERROR_UNSUPPORTED);
CHECK(ref_is_zero(&ref));
```

`test_update_delete()` must acquire Text and Square refs, delete Text, verify Text getter/update/delete/contents all return `EXTRACTPDF_ERROR_STATE`, reacquire current index 0 and prove it is the same Square ref, then update Square bounds/flags/Contents through the original ref. It must also acquire Highlight and prove `BOUNDS` update returns `UNSUPPORTED` while `FLAGS` and `CONTENTS` update succeeds. A zero-field update on a live UNKNOWN ref is `OK`; a zero-field update on a tombstone is `STATE`; a zero-field update with a ref from another editor is `ARGUMENT`.

Atomic rollback tests use the private per-editor hook:

```c
extractpdf_annotation_update update = {0};
update.struct_size = sizeof(update);
update.fields = EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS |
                EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
update.bounds = (extractpdf_rect){20, 20, 90, 90};
update.contents_utf8 = "changed";
update.contents_size = 7;

extractpdf_test_pdf_edit_set_fault(
    edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD);
CHECK(extractpdf_pdf_edit_annotation_update(edit, &square, &update) != EXTRACTPDF_OK);
/* Re-read info + contents and assert both equal the pre-call values. */
```

Create rollback:

```c
extractpdf_test_pdf_edit_set_fault(
    edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION);
zero_ref(&ref);
CHECK(extractpdf_pdf_edit_annotation_create(edit, 0, &options, &ref) != EXTRACTPDF_OK);
CHECK(ref_is_zero(&ref));
CHECK(extractpdf_pdf_edit_annotation_count(edit, 0, &after) == EXTRACTPDF_OK);
CHECK(after == before);
```

Contents/flags group must include:

```c
/* Existing Text has /F 2147483649 = 0x80000001. */
info.struct_size = sizeof(info);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &text, &info) == EXTRACTPDF_OK);
CHECK(info.flags == UINT32_C(2147483649));

update.struct_size = sizeof(update);
update.fields = EXTRACTPDF_ANNOTATION_UPDATE_FLAGS;
update.flags = UINT32_MAX;
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &text, &info) == EXTRACTPDF_OK);
CHECK(info.flags == UINT32_MAX);
```

Also test absent/present-empty/non-empty Contents, counted non-NUL-terminated input, invalid UTF-8, embedded NUL, and live getter ownership:

```c
static const char counted[3] = {'c','a','t'};
static const char bad_utf8[2] = {(char)0xC0, (char)0xAF};
static const char embedded_nul[3] = {'a','\0','b'};
char *owned = NULL;
size_t owned_size = 0;

update.fields = EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
update.contents_utf8 = counted;
update.contents_size = sizeof(counted);
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_contents(edit, &text, &owned, &owned_size) == EXTRACTPDF_OK);
CHECK(owned_size == 3 && memcmp(owned, "cat", 3) == 0 && owned[3] == '\0');

/* Mutate after the getter; the allocated old copy must remain "cat". */
update.contents_utf8 = "dog";
update.contents_size = 3;
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_OK);
CHECK(memcmp(owned, "cat", 3) == 0);
extractpdf_free(owned);

update.contents_utf8 = bad_utf8;
update.contents_size = sizeof(bad_utf8);
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_ERROR_ARGUMENT);

update.contents_utf8 = embedded_nul;
update.contents_size = sizeof(embedded_nul);
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_ERROR_ARGUMENT);
```

Snapshot group must copy A bytes before further mutation, produce B, and reparse both through public APIs:

```c
const unsigned char *a_data = NULL;
const unsigned char *repeat_data = NULL;
size_t a_size = 0, repeat_size = 0;
unsigned char *a_copy = NULL;

CHECK(extractpdf_pdf_edit_snapshot(edit, &a) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_snapshot(edit, &repeat) == EXTRACTPDF_OK);
CHECK(extractpdf_output_data(a, &a_data, &a_size) == EXTRACTPDF_OK);
CHECK(extractpdf_output_data(repeat, &repeat_data, &repeat_size) == EXTRACTPDF_OK);
CHECK(a_size == repeat_size && memcmp(a_data, repeat_data, a_size) == 0);
a_copy = malloc(a_size);
CHECK(a_copy != NULL);
memcpy(a_copy, a_data, a_size);

/* mutate existing Text contents to "snapshot-b" */
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text, &update) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_snapshot(edit, &b) == EXTRACTPDF_OK);
CHECK(extractpdf_output_data(a, &a_data, &a_size) == EXTRACTPDF_OK);
CHECK(memcmp(a_data, a_copy, a_size) == 0);

save_output(a, OUTPUT_A_PDF);
save_output(b, OUTPUT_B_PDF);
expect_snapshot_annotation(OUTPUT_A_PDF, 0, EXTRACTPDF_ANNOTATION_TEXT, "text-a");
expect_snapshot_annotation(OUTPUT_B_PDF, 0, EXTRACTPDF_ANNOTATION_TEXT, "snapshot-b");

extractpdf_test_pdf_edit_set_fault(
    edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH);
out = (extractpdf_output *)(uintptr_t)1;
CHECK(extractpdf_pdf_edit_snapshot(edit, &out) != EXTRACTPDF_OK);
CHECK(out == NULL);
/* editor/ref must still work after failed snapshot */
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &text, &info) == EXTRACTPDF_OK);
```

Source immutability belongs in the snapshot group: create a source-side immutable annotation snapshot **before** beginning mutations, mutate the editor, then re-read that old snapshot after source close and assert original `text-a`, original bounds, and original `2147483649` flags.

JavaScript group:

```c
static void test_javascript_disabled(void)
{
    extractpdf_document *source = NULL;
    extractpdf_document *reparsed = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_output *out = NULL;
    char *title = NULL;
    size_t title_size = 0;

    CHECK(extractpdf_open(JS_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &out) == EXTRACTPDF_OK);
    save_output(out, OUTPUT_JS_PDF);
    CHECK(extractpdf_open(OUTPUT_JS_PDF, NULL, &reparsed) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_metadata(reparsed, EXTRACTPDF_METADATA_TITLE,
                                       &title, &title_size) == EXTRACTPDF_OK);
    CHECK(title_size == 4 && memcmp(title, "SAFE", 4) == 0);
    extractpdf_free(title);
    extractpdf_close(reparsed);
    extractpdf_drop_output(out);
    extractpdf_drop_pdf_edit(edit);
    extractpdf_close(source);
}
```

The fixture OpenAction uses MuPDF-supported document title assignment (`this.title = "EXECUTED"`). Therefore observing `SAFE` after begin/snapshot proves this editor path did not execute the script.

`main()` must run every group with no argument and exactly one named group when an argument is supplied:

```c
static int selected(int argc, char **argv, const char *name)
{
    return argc == 1 || (argc == 2 && strcmp(argv[1], name) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc <= 2);
    if (selected(argc, argv, "arguments")) test_arguments();
    if (selected(argc, argv, "begin")) test_begin_lifetime_and_fail_closed();
    if (selected(argc, argv, "discovery")) {
        test_discovery_and_refs();
        test_malformed_discovery_is_atomic();
    }
    if (selected(argc, argv, "create")) test_create();
    if (selected(argc, argv, "update-delete")) test_update_delete();
    if (selected(argc, argv, "contents-flags")) test_contents_flags();
    if (selected(argc, argv, "snapshot")) test_snapshot_isolation();
    if (selected(argc, argv, "javascript")) test_javascript_disabled();
    return 0;
}
```

- [ ] **Step 4: Register the RED target**

Append to `tests/CMakeLists.txt`:

```cmake
set(MUTATION_OUTPUT_A "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-a.pdf")
set(MUTATION_OUTPUT_B "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-b.pdf")
set(MUTATION_OUTPUT_JS "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-js-output.pdf")

add_executable(extractpdf_test_pdf_annotation_mutation
  test_pdf_annotation_mutation.c)
target_link_libraries(extractpdf_test_pdf_annotation_mutation PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_annotation_mutation PRIVATE
  MUTATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation.pdf"
  SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  UNSIGNED_SIGNATURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-unsigned-signature.pdf"
  JS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-js.pdf"
  LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-late-malformed.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  OUTPUT_A_PDF="${MUTATION_OUTPUT_A}"
  OUTPUT_B_PDF="${MUTATION_OUTPUT_B}"
  OUTPUT_JS_PDF="${MUTATION_OUTPUT_JS}")
add_test(NAME extractpdf.pdf_annotation_mutation
  COMMAND extractpdf_test_pdf_annotation_mutation)
set_tests_properties(extractpdf.pdf_annotation_mutation PROPERTIES TIMEOUT 60)
```

Add `extractpdf_test_pdf_annotation_mutation` to the existing `WIN32 AND BUILD_SHARED_LIBS` DLL-copy list.

Do **not** add `pdf_edit_fault_hook.c` to the library in RED. Its absence must not become the compile failure because the public mutation types themselves are still absent.

- [ ] **Step 5: Run RED locally**

Use the same Linux static configuration as CI after the pinned vcpkg dependency is available:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
```

Expected RED:

```text
extractpdf library                                    builds
all existing test targets through pdf_annotations     build
extractpdf_test_pdf_annotation_mutation                fails to compile
```

The new target must fail on the **approved absent public ABI**: unknown `extractpdf_pdf_edit`, `extractpdf_annotation_ref`, create/update types/field constants, `EXTRACTPDF_ERROR_STATE`, and implicit/undefined new API declarations. A failure caused by malformed fixtures, missing fixture paths, missing `pdf_edit_test_api.h`, unrelated source warnings, or an existing test is not valid RED.

- [ ] **Step 6: Commit and capture remote RED evidence**

```bash
git add tests/fixtures/annotation-mutation*.pdf \
        tests/pdf_edit_test_api.h \
        tests/test_pdf_annotation_mutation.c \
        tests/CMakeLists.txt
git commit -m "test: define PDF annotation mutation red"
```

Open a draft PR targeting `master` and linking #37 / roadmap #2. The first PR workflow must reproduce the same boundary: all existing library/test targets build and only the mutation target fails because the new ABI is absent. Record the exact RED SHA and workflow run in the PR body and #37 before adding production declarations.

---

### Task 2: Extract one shared annotation semantic core

**Files:**
- Create: `src/pdf_annotation_common.h`
- Create: `src/pdf_annotation_common.c`
- Modify: `src/pdf_annotations.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: current Annotation Enumeration V1 behavior from `src/pdf_annotations.c`.
- Produces:

```c
typedef struct extractpdf_pdf_annotation_view {
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
    int has_contents;
} extractpdf_pdf_annotation_view;

int extractpdf_pdf_annotation_classify(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type *out_type);

extractpdf_status extractpdf_pdf_annotation_read_view(
    fz_context *ctx,
    pdf_page *page,
    pdf_obj *annotation,
    extractpdf_annotation_type type,
    extractpdf_pdf_annotation_view *out_view);
```

- [ ] **Step 1: Confirm the refactor safety net is green before editing**

```bash
ctest --test-dir build -R '^extractpdf\.pdf_annotations$' --output-on-failure
```

Expected: `extractpdf.pdf_annotations` passes on the integrated behavior.

- [ ] **Step 2: Move classification/materialization helpers into the common module**

Create `src/pdf_annotation_common.h` with the interface above and include `pdf_internal.h`.

Move, without semantic changes, the current private logic for:

```text
dictionary key presence lookup
Subtype mapping
Link / Popup / Widget filtering
UNKNOWN mapping
strict Rect validation + direct pdf_page_transform() conversion
strict uint32 /F validation
strict optional /Contents string validation + decoded borrowed view
```

`extractpdf_pdf_annotation_read_view()` must initialize every field in `out_view`, call `pdf_page_transform()` directly for public bounds, and use `strlen()` on `pdf_to_text_string()` exactly as the current immutable implementation does. The returned `contents_utf8` is private/borrowed and may only be copied immediately by callers.

- [ ] **Step 3: Rewrite immutable enumeration to consume the shared view**

The first pass remains a raw `/Annots` scan using only `extractpdf_pdf_annotation_classify()`.

The second pass becomes equivalent to:

```c
extractpdf_pdf_annotation_view view;
status = extractpdf_pdf_annotation_read_view(
    ctx, pdf_page, annotation, type, &view);
if (status != EXTRACTPDF_OK)
    break;

item->type = view.type;
item->bounds = view.bounds;
item->flags = view.flags;
if (view.has_contents) {
    item->has_contents = 1;
    status = extractpdf_annotation_append_string(
        annotations,
        view.contents_utf8,
        &item->contents_offset,
        &item->contents_size);
}
```

Delete the duplicate static classify/Rect/F/Contents code from `pdf_annotations.c`.

- [ ] **Step 4: Build and prove no enumeration regression**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_annotations$' --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected: all existing 18 CTests pass. The mutation target remains the known compile RED until the public ABI is introduced; when building only existing targets, there is no behavior change.

- [ ] **Step 5: Commit**

```bash
git add src/pdf_annotation_common.h src/pdf_annotation_common.c \
        src/pdf_annotations.c CMakeLists.txt
git commit -m "refactor: share PDF annotation semantics"
```

Reviewer gate: reject this task if any old annotation fixture changes meaning, if editor-specific behavior leaks into the common module, or if common helpers publish borrowed pointers through the public ABI.

---

### Task 3: Add the ABI shell and isolated editor lifecycle

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Modify: `src/status.c`
- Create: `src/pdf_edit_internal.h`
- Create: `src/pdf_edit.c`
- Create: `src/pdf_edit_annotations.c`
- Create: `tests/pdf_edit_fault_hook.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `extractpdf_serialize_pdf()`, existing `extractpdf_output`, MuPDF 1.28.2 `pdf_document_from_fz_document`, `pdf_open_document_with_stream`, `pdf_disable_js`, `pdf_enable_journal`, `pdf_walk_tree`, signature helpers.
- Produces: every approved public Mutation V1 symbol so the RED test binary links; lifecycle/begin/snapshot/drop and fail-closed policy are real, while annotation-specific calls may temporarily return `UNSUPPORTED` after correct output resets until Tasks 4-6.

- [ ] **Step 1: Add the exact public ABI**

Append the new opaque handle near the other handles:

```c
typedef struct extractpdf_pdf_edit extractpdf_pdf_edit;

typedef struct extractpdf_annotation_ref {
    uint64_t opaque[2];
} extractpdf_annotation_ref;
```

Add:

```c
typedef enum extractpdf_annotation_update_field {
    EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS = 1u << 0,
    EXTRACTPDF_ANNOTATION_UPDATE_FLAGS = 1u << 1,
    EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS = 1u << 2
} extractpdf_annotation_update_field;

typedef struct extractpdf_annotation_create_options {
    size_t struct_size;
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} extractpdf_annotation_create_options;

typedef struct extractpdf_annotation_update {
    size_t struct_size;
    uint32_t fields;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} extractpdf_annotation_update;
```

Append to `extractpdf_status` without renumbering existing values:

```c
EXTRACTPDF_ERROR_STATE = 8
```

Add the nine approved functions exactly as written in the spec: begin, count, ref_at, get_info, contents, create, update, delete, snapshot, plus `extractpdf_drop_pdf_edit()`.

- [ ] **Step 2: Add the stable status text**

In `src/status.c`:

```c
case EXTRACTPDF_ERROR_STATE:
    return "invalid state";
```

- [ ] **Step 3: Define private editor ownership**

Create `src/pdf_edit_internal.h`:

```c
#ifndef EXTRACTPDF_PDF_EDIT_INTERNAL_H
#define EXTRACTPDF_PDF_EDIT_INTERNAL_H

#include "pdf_annotation_common.h"

typedef struct extractpdf_pdf_edit_annotation_entry {
    pdf_obj *object;
    int page_index;
    uint32_t tag;
    int live;
} extractpdf_pdf_edit_annotation_entry;

struct extractpdf_pdf_edit {
    fz_context *ctx;
    pdf_document *document;
    extractpdf_output *seed_output; /* owns bytes backing the private input stream */
    uint64_t session_cookie;
    extractpdf_pdf_edit_annotation_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
#if defined(EXTRACTPDF_TESTING)
    int test_fault;
#endif
};

extractpdf_status extractpdf_pdf_edit_load_page(
    extractpdf_pdf_edit *edit, int page_index, pdf_page **out_page);

#endif
```

Do not put editor state into `struct extractpdf_document`.

- [ ] **Step 4: Implement signed-field scanning without loading widgets or executing actions**

In `src/pdf_edit.c`, use `pdf_walk_tree()` on `Root/AcroForm/Fields` with inherited `/FT`. The arrive callback checks signature fields with `pdf_signature_is_signed()` and sets a local flag. It must not enable JavaScript or dispatch events.

Equivalent private callback shape:

```c
typedef struct extractpdf_signed_scan {
    pdf_document *document;
    int found;
} extractpdf_signed_scan;

static void scan_signed_field(
    fz_context *ctx, pdf_obj *field, void *arg, pdf_obj **inherited)
{
    extractpdf_signed_scan *scan = arg;
    pdf_obj *ft = pdf_dict_get(ctx, field, PDF_NAME(FT));
    if (ft == NULL)
        ft = inherited[0];
    if (!scan->found && pdf_name_eq(ctx, ft, PDF_NAME(Sig)) &&
        pdf_signature_is_signed(ctx, scan->document, field))
        scan->found = 1;
}
```

Use `pdf_walk_tree()`'s cycle/depth protection rather than writing recursive unbounded traversal.

- [ ] **Step 5: Implement `extractpdf_pdf_edit_begin()` atomically**

Required sequence:

```text
reset *out_edit
validate source internals
pdf_document_from_fz_document -> NULL means UNSUPPORTED
raw trailer /Encrypt present -> UNSUPPORTED
signed field scan -> signed means UNSUPPORTED
serialize source with extractpdf_serialize_pdf() into seed_output
allocate editor
new independent fz_context
set discard warning/error callbacks
fz_open_memory(seed_output->data, seed_output->size)
pdf_open_document_with_stream()
pdf_disable_js()
pdf_enable_journal()
create nonzero private session cookie
publish editor
```

Keep `seed_output` alive for the whole editor lifetime because the private PDF stream may continue reading its memory after open. Drop the local stream reference after `pdf_open_document_with_stream()` because the PDF document keeps its own stream reference.

Generate `session_cookie` with editor-local data only; no global counter. A suitable private mix is:

```c
static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}
```

Mix `uintptr_t(edit)`, `uintptr_t(edit->ctx)`, and bytes from `fz_memrnd(edit->ctx, ...)`; force zero to a nonzero constant. This token is an opaque session discriminator, not a cryptographic authentication promise and exposes no PDF identity.

Every caught MuPDF exception maps through `extractpdf_status_from_mupdf()` and tears down all partial editor state before return.

- [ ] **Step 6: Implement non-consuming baseline snapshot/drop**

`extractpdf_pdf_edit_snapshot()`:

```c
extractpdf_output *result = NULL;
if (out_output == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
*out_output = NULL;
if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
status = extractpdf_serialize_pdf(edit->ctx, edit->document, &result);
if (status != EXTRACTPDF_OK)
    return status;
#if defined(EXTRACTPDF_TESTING)
if (edit->test_fault == EXTRACTPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH) {
    edit->test_fault = EXTRACTPDF_TEST_PDF_EDIT_FAULT_NONE;
    extractpdf_drop_output(result);
    return EXTRACTPDF_ERROR_MUPDF;
}
#endif
*out_output = result;
return EXTRACTPDF_OK;
```

`extractpdf_drop_pdf_edit()` drops all kept registry objects, registry storage, private PDF document, private context, and `seed_output`; NULL is a no-op. Drop the PDF document before the context and drop `seed_output` only after the PDF document no longer references its memory stream.

- [ ] **Step 7: Add linkable annotation API shells with correct reset semantics**

In `src/pdf_edit_annotations.c`, define all remaining public mutation functions so the test binary links. At this task they may return `EXTRACTPDF_ERROR_UNSUPPORTED` after required pointer/reset/`struct_size` checks when passed an otherwise valid editor. Do not fake discovery or CRUD success.

Examples:

```c
extractpdf_status extractpdf_pdf_edit_annotation_count(
    extractpdf_pdf_edit *edit, int page_index, size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (edit == NULL || out_count == NULL || page_index < 0)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_ref_at(
    extractpdf_pdf_edit *edit, int page_index, size_t index,
    extractpdf_annotation_ref *out_ref)
{
    (void)index;
    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (edit == NULL || out_ref == NULL || page_index < 0)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

- [ ] **Step 8: Add the private per-editor deterministic fault hook**

In `tests/CMakeLists.txt`:

```cmake
target_sources(extractpdf PRIVATE pdf_edit_fault_hook.c)
target_compile_definitions(extractpdf PRIVATE EXTRACTPDF_TESTING=1)
```

Create `tests/pdf_edit_fault_hook.c`:

```c
#include "../src/pdf_edit_internal.h"
#include "pdf_edit_test_api.h"

void extractpdf_test_pdf_edit_set_fault(
    extractpdf_pdf_edit *edit,
    extractpdf_test_pdf_edit_fault fault)
{
#if defined(EXTRACTPDF_TESTING)
    if (edit != NULL)
        edit->test_fault = (int)fault;
#else
    (void)edit;
    (void)fault;
#endif
}
```

Because this source is added to the library only from the tests subdirectory, the hook is not part of test-disabled production builds or the installed public header. Fault state is per editor, satisfying the no-global-state constraint.

- [ ] **Step 9: Register production sources and run lifecycle groups**

Add to root `add_library(extractpdf ...)`:

```cmake
src/pdf_annotation_common.c
src/pdf_edit.c
src/pdf_edit_annotations.c
```

Run:

```bash
cmake -S . -B build ...same pinned-vcpkg args...
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation arguments
./build/tests/extractpdf_test_pdf_annotation_mutation begin
./build/tests/extractpdf_test_pdf_annotation_mutation javascript
```

Expected: `arguments`, `begin`, and baseline `javascript` pass. Discovery/CRUD groups are still behavioral RED (`UNSUPPORTED`) and must not be claimed GREEN yet.

If two baseline snapshots with no mutation are not byte-identical, stop this task and fix `snapshot()` without weakening the spec; direct `pdf_write_document()` use is acceptable only if the unchanged-state determinism/non-consuming tests pass.

- [ ] **Step 10: Commit**

```bash
git add include/extractpdf/extractpdf.h src/status.c \
        src/pdf_edit_internal.h src/pdf_edit.c src/pdf_edit_annotations.c \
        tests/pdf_edit_fault_hook.c tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add isolated PDF editor lifecycle"
```

---

### Task 4: Implement discovery, canonical session refs, and live getters

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: shared `extractpdf_pdf_annotation_classify/read_view`, private editor PDF.
- Produces: real count/ref_at/get_info/contents behavior and canonical session refs that survive index shifts.

- [ ] **Step 1: Run the focused failing discovery group**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation discovery
```

Expected: FAIL because count/ref_at/getters still return `UNSUPPORTED`.

- [ ] **Step 2: Add identity comparison and registry reserve helpers**

Registry entries keep a private `pdf_obj *` reference and page index. Compare indirect objects by private object number+generation and direct dictionaries by object pointer; never compare dictionary contents, because two distinct annotations may be value-equal.

```c
static int same_pdf_identity(fz_context *ctx, pdf_obj *a, pdf_obj *b)
{
    int ai = pdf_is_indirect(ctx, a);
    int bi = pdf_is_indirect(ctx, b);
    if (ai || bi)
        return ai && bi &&
               pdf_to_num(ctx, a) == pdf_to_num(ctx, b) &&
               pdf_to_gen(ctx, a) == pdf_to_gen(ctx, b);
    return a == b;
}
```

`reserve_entries(edit, needed)` must overflow-check `SIZE_MAX / sizeof(entry)` and allocate before any operation that cannot safely fail afterward.

- [ ] **Step 3: Canonicalize one ref per annotation object**

Before allocating a new slot, search all registry entries for the same private identity. If found live, return its existing token. Never recycle a tombstoned slot during the edit session.

Token layout is private but concrete:

```text
opaque[0] = edit->session_cookie
opaque[1] = (uint64_t(entry->tag) << 32) | uint64_t(slot + 1)
```

Limit registry slots to `UINT32_MAX - 1`; exceeding that is `NOMEM`. Generate a nonzero 32-bit `tag` from the editor-local random/mix helper.

Resolution checks, in order:

```text
NULL edit/ref                       ARGUMENT
opaque[0] != session_cookie         ARGUMENT
slot field == 0/out of range        ARGUMENT
tag mismatch                        ARGUMENT
matching entry live==0              STATE
matching live entry                 OK
```

- [ ] **Step 4: Scan and validate a whole page before returning count/ref**

Implement a private scanner that loads `pdf_page`, reads raw `/Annots`, filters via shared classify, and calls shared strict `read_view()` for **every survivor**. It records count and optionally keeps the object at one requested survivor index only after the entire scan succeeds.

Pseudo-interface:

```c
static extractpdf_status scan_page(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t wanted_index,
    int want_object,
    size_t *out_count,
    pdf_obj **out_object);
```

On malformed later survivor, return `FORMAT` and publish/register nothing from the prefix.

- [ ] **Step 5: Implement count/ref_at**

`count()` resets output, validates page range using `pdf_count_pages`, scans the full page, and publishes count only on success.

`ref_at()` zeros token, scans/validates the full page, rejects `index >= count`, then canonical-registers the wanted object and publishes the token.

- [ ] **Step 6: Implement getters from ref**

Resolve the entry, load its page, confirm the object is still present as a surviving ordinary annotation on that page, then use shared `read_view()`.

`get_info()` preserves caller `struct_size`, validates the existing minimum size through `flags`, resets type/bounds/flags, and copies type/bounds/flags.

`contents()` independently resets non-NULL outputs, requires both, and deep-copies the view string:

```c
if (!view.has_contents)
    return EXTRACTPDF_OK;
copy = malloc(view.contents_size + 1);
if (copy == NULL)
    return EXTRACTPDF_ERROR_NOMEM;
memcpy(copy, view.contents_utf8, view.contents_size + 1);
*out_utf8 = copy;
*out_size = view.contents_size;
```

If a registry entry is marked live but its object is no longer present because of an internal consistency change, return `EXTRACTPDF_ERROR_STATE` rather than silently retargeting another index.

- [ ] **Step 7: Run focused and old-semantic tests**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation discovery
ctest --test-dir build -R '^extractpdf\.pdf_annotations$' --output-on-failure
```

Expected: discovery group passes; immutable enumeration still passes unchanged.

- [ ] **Step 8: Commit**

```bash
git add src/pdf_edit_internal.h src/pdf_edit_annotations.c
git commit -m "feat: add annotation edit refs and discovery"
```

---

### Task 5: Implement safe Rect-based annotation creation

**Files:**
- Modify: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: editor page loading, registry reserve/canonical publication, MuPDF journal/create/setter APIs.
- Produces: atomic TEXT/FREE_TEXT/SQUARE/CIRCLE creation with immediate live ref.

- [ ] **Step 1: Run the focused failing create group**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation create
```

Expected: FAIL because create still returns `UNSUPPORTED`.

- [ ] **Step 2: Implement counted UTF-8 validation/copy**

Add a private validator that rejects NUL and invalid UTF-8, including overlong encodings, UTF-16 surrogate code points, values above U+10FFFF, and truncated continuations. Accept ASCII plus canonical 2/3/4-byte sequences.

Use these byte ranges:

```text
1 byte: 00..7F, except 00 is rejected by Contents policy
2 byte: C2..DF 80..BF
3 byte: E0 A0..BF 80..BF
        E1..EC 80..BF 80..BF
        ED 80..9F 80..BF
        EE..EF 80..BF 80..BF
4 byte: F0 90..BF 80..BF 80..BF
        F1..F3 80..BF 80..BF 80..BF
        F4 80..8F 80..BF 80..BF
```

For present input allocate `size + 1` with overflow check, copy exactly `size` bytes, append `\0`, and return the temporary C string. NULL/0 remains the absent marker and allocates nothing.

- [ ] **Step 3: Validate creation inputs before mutation**

Minimum struct size is:

```c
offsetof(extractpdf_annotation_create_options, contents_size) +
    sizeof(options->contents_size)
```

Require valid page index, supported type, finite ordered bounds, and valid Contents tuple. Zero-width/height is accepted.

Map only:

```c
TEXT      -> PDF_ANNOT_TEXT
FREE_TEXT -> PDF_ANNOT_FREE_TEXT
SQUARE    -> PDF_ANNOT_SQUARE
CIRCLE    -> PDF_ANNOT_CIRCLE
```

All other public types are `UNSUPPORTED`.

- [ ] **Step 4: Implement full-uint32 `/F` writing**

Add one private helper used by create and update:

```c
static void set_annot_flags_u32(
    fz_context *ctx, pdf_annot *annot, uint32_t flags)
{
    if (flags <= (uint32_t)INT_MAX) {
        pdf_set_annot_flags(ctx, annot, (int)flags);
    } else {
        pdf_obj *obj = pdf_annot_obj(ctx, annot);
        pdf_dict_put_int(ctx, obj, PDF_NAME(F), (int64_t)(uint64_t)flags);
        pdf_annot_request_resynthesis(ctx, annot);
    }
}
```

Do not cast `UINT32_MAX` through signed `int`.

- [ ] **Step 5: Create inside one outer journal operation**

Before `pdf_begin_operation()`, reserve one registry slot so no registry allocation is needed after a successful PDF operation.

Inside `fz_try`:

```c
pdf_begin_operation(ctx, edit->document, "ExtractPDF create annotation");
annot = pdf_create_annot(ctx, page, mupdf_type);
pdf_set_annot_rect(ctx, annot, public_bounds);
set_annot_flags_u32(ctx, annot, options->flags);
if (options->contents_utf8 != NULL)
    pdf_set_annot_contents(ctx, annot, temporary_nul_terminated_text);
pdf_update_annot(ctx, annot);
```

Under test builds, if fault `AFTER_CREATE_MUTATION` is armed, clear it and `fz_throw(ctx, FZ_ERROR_GENERIC, "ExtractPDF test create fault")` before `pdf_end_operation()`.

On normal success, call `pdf_end_operation()` first, then fill the already-reserved registry entry from `pdf_annot_obj()` and publish the token. `pdf_keep_obj()` is only a refcount increment and must not introduce an allocation/publication failure after commit.

On exception, call `pdf_abandon_operation()`, drop the returned annot reference, free temporary Contents, and leave `out_ref` zero.

- [ ] **Step 6: Run create + full flags seed checks**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation create
./build/tests/extractpdf_test_pdf_annotation_mutation contents-flags
```

At this point the create assertions should pass. The contents-flags group may still fail on update-specific assertions; the pre-existing high-bit `/F` getter assertion must already pass.

- [ ] **Step 7: Commit**

```bash
git add src/pdf_edit_annotations.c
git commit -m "feat: create Rect-based PDF annotations"
```

---

### Task 6: Implement partial update, delete, and deterministic rollback

**Files:**
- Modify: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: canonical refs, UTF-8 helper, full-u32 flags helper, MuPDF journal/setter/delete APIs, private test fault.
- Produces: atomic partial update/delete with tombstone semantics.

- [ ] **Step 1: Run focused failing groups**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation update-delete
./build/tests/extractpdf_test_pdf_annotation_mutation contents-flags
```

Expected: update/delete behavior is still RED.

- [ ] **Step 2: Validate the whole update request before opening the operation**

Require non-NULL edit/ref/update and minimum struct size through `contents_size`. Resolve the ref first.

Then:

```c
const uint32_t known =
    EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS |
    EXTRACTPDF_ANNOTATION_UPDATE_FLAGS |
    EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
if (update->fields & ~known)
    return EXTRACTPDF_ERROR_ARGUMENT;
if (update->fields == 0)
    return EXTRACTPDF_OK;
```

This ordering is mandatory: wrong-session zero-mask -> `ARGUMENT`, tombstone zero-mask -> `STATE`, live UNKNOWN zero-mask -> `OK`.

For nonzero fields, UNKNOWN -> `UNSUPPORTED`. BOUNDS on anything except TEXT/FREE_TEXT/SQUARE/CIRCLE -> `UNSUPPORTED`. Prevalidate bounds and Contents before mutation.

- [ ] **Step 3: Implement absent/present Contents mutation**

Present string uses `pdf_set_annot_contents()`.

Removing `/Contents` must be journalled directly:

```c
pdf_obj *obj = pdf_annot_obj(ctx, annot);
pdf_dict_del(ctx, obj, PDF_NAME(Contents));
pdf_annot_request_resynthesis(ctx, annot);
```

This preserves absent versus present-empty instead of converting removal into an empty string.

- [ ] **Step 4: Apply updates in fixed order inside one outer operation**

Use fixed private order BOUNDS -> FLAGS -> CONTENTS. After each actually applied field increment `applied_fields`. When the test fault `AFTER_FIRST_UPDATE_FIELD` is armed and `applied_fields == 1`, clear it and throw before applying later fields.

After all requested setters/raw edits:

```c
pdf_update_annot(ctx, annot);
pdf_end_operation(ctx, edit->document);
```

Any exception calls `pdf_abandon_operation()` and returns mapped error. The registry is unchanged by update.

- [ ] **Step 5: Implement delete + tombstone only after success**

Resolve ref and locate the current `pdf_annot *` by private object identity on its page. UNKNOWN delete is `UNSUPPORTED`; every recognized ordinary subtype is allowed.

```c
pdf_begin_operation(ctx, edit->document, "ExtractPDF delete annotation");
pdf_delete_annot(ctx, page, annot);
pdf_end_operation(ctx, edit->document);
entry->live = 0;
```

If delete throws, abandon and leave `entry->live == 1`.

Do not recycle the slot. Associated Popup cleanup remains MuPDF internal behavior.

- [ ] **Step 6: Run rollback/tombstone/index-shift tests**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation update-delete
./build/tests/extractpdf_test_pdf_annotation_mutation contents-flags
```

Expected:

```text
failed two-field update -> old bounds + old Contents both remain
failed create fault     -> count unchanged + zero ref
delete Text              -> Text ref STATE
Square index shifts      -> old Square ref still targets Square
Highlight bounds         -> UNSUPPORTED
Highlight flags/content  -> succeeds
UINT32_MAX flags         -> round-trip unchanged
invalid UTF-8/NUL        -> ARGUMENT before mutation
```

- [ ] **Step 7: Commit**

```bash
git add src/pdf_edit_annotations.c
git commit -m "feat: atomically update and delete annotations"
```

---

### Task 7: Prove source isolation, snapshot isolation, output reparse, and JavaScript non-execution

**Files:**
- Modify if verification exposes an implementation defect: `src/pdf_edit.c`
- Modify if verification exposes a mutation defect: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: complete editor/ref/CRUD behavior.
- Produces: final public semantic proof before all-suite GREEN.

- [ ] **Step 1: Run snapshot group before changing production code**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation snapshot
```

Expected after Tasks 3-6: PASS. If it fails, treat the failure as a product bug; do not weaken byte-identity, source-isolation, or non-consuming assertions.

- [ ] **Step 2: Confirm immutable source state survives editor mutation**

The test must hold an `extractpdf_annotation_page` from the source before `edit_begin`, close source after begin, mutate the editor, and then verify the old source snapshot remains:

```text
TEXT
original Fitz bounds [10,160,30,180]
flags 2147483649
Contents "text-a"
```

No source handle is kept alive merely to make this pass.

- [ ] **Step 3: Confirm snapshot A/B semantics through only public reparse APIs**

Required checks are already in RED and must all pass:

```text
snapshot A == repeat snapshot bytes when no mutation intervenes
A stays byte-identical after later mutation
B differs after later mutation
A reparse immutable enumeration -> old Contents
B reparse immutable enumeration -> new Contents
snapshot does not invalidate Text ref
failed snapshot fault returns NULL output
failed snapshot leaves editor/ref usable
output remains valid after drop editor
```

If direct `extractpdf_serialize_pdf(edit->document)` changes editor bookkeeping enough to violate these tests, do not special-case the assertions. Introduce a private serialization strategy that preserves the editor's observable state and keep the public contract unchanged.

- [ ] **Step 4: Run JavaScript group after CRUD/appearance paths exist**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation javascript
```

Extend the group to perform one Text Contents update before snapshot, so `pdf_update_annot()` is exercised. Reparsed metadata Title must remain `SAFE`, never `EXECUTED`.

- [ ] **Step 5: Verify outputs with immutable annotation enumeration**

The test must not use raw MuPDF objects to prove the core result. `extractpdf_open` + `extractpdf_load_page` + `extractpdf_extract_annotations` is the acceptance surface for created/updated/deleted results and full `uint32_t` flags.

- [ ] **Step 6: Run the complete mutation executable**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation
```

Expected: PASS with all named groups.

- [ ] **Step 7: Commit only if this task required production/test corrections**

```bash
git add src/pdf_edit.c src/pdf_edit_annotations.c tests/test_pdf_annotation_mutation.c
git commit -m "test: lock PDF annotation editor isolation"
```

If no file changed, do not create an empty commit.

---

### Task 8: Reach full GREEN and exact-head cross-platform proof

**Files:**
- No planned feature expansion.
- Modify only files already in the approved scope if a real portability/compiler defect is found.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: merge-ready exact-head evidence; does not merge.

- [ ] **Step 1: Fresh Linux static build and all CTests**

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: **19/19 CTests pass**, with `extractpdf.pdf_annotation_mutation` present as the new test.

- [ ] **Step 2: Fresh Linux ASan/UBSan build and all CTests**

```bash
rm -rf build-asan
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

Expected: 19/19 pass with no sanitizer finding in ExtractPDF-owned editor/ref/string/registry code.

- [ ] **Step 3: Exact-head diff review**

Compare feature head against base `58525ce1b4b691ef53dfafc7c4e4d82753c966ba`.

Allowed production paths:

```text
include/extractpdf/extractpdf.h
src/status.c
src/pdf_annotation_common.h
src/pdf_annotation_common.c
src/pdf_annotations.c
src/pdf_edit_internal.h
src/pdf_edit.c
src/pdf_edit_annotations.c
CMakeLists.txt
```

Allowed test/docs paths:

```text
docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotation-mutation-design.md
docs/superpowers/plans/2026-08-29-extractpdf-pdf-annotation-mutation.md
tests/CMakeLists.txt
tests/pdf_edit_test_api.h
tests/pdf_edit_fault_hook.c
tests/test_pdf_annotation_mutation.c
tests/fixtures/annotation-mutation.pdf
tests/fixtures/annotation-mutation-signed.pdf
tests/fixtures/annotation-mutation-unsigned-signature.pdf
tests/fixtures/annotation-mutation-js.pdf
```

No unrelated page/render/text/image/link/outline/metadata/composition/output-file changes are acceptable. `pdf_output.c` should remain unchanged unless a demonstrated snapshot-contract defect requires a narrowly reviewed serializer fix.

- [ ] **Step 4: Push final GREEN and capture Linux PR CI**

The normal PR synchronize run on the final exact SHA must pass Linux static + all CTests and Linux ASan/UBSan + all CTests. Update the draft PR body and #37 with:

```text
RED SHA + workflow
first production GREEN SHA + workflow
final exact SHA
19/19 Linux static
19/19 Linux ASan/UBSan
```

- [ ] **Step 5: Trigger same-head `full-ci`**

Add label `full-ci` to the draft PR **only after the final exact SHA is fixed**. Because the workflow's macOS/Windows PR jobs run only on the labeled event, verify the labeled workflow uses the same final `head_sha`.

Acceptance:

```text
Linux static + 19/19 CTests       success
Linux ASan/UBSan + 19/19 CTests  success
macOS build + 19/19 CTests        success
Windows DLL build + 19/19 CTests success
```

Windows logs must explicitly show construction/linking of the new editor sources, `extractpdf_test_pdf_annotation_mutation.exe`, and `extractpdf.pdf_annotation_mutation` passing through the DLL configuration.

- [ ] **Step 6: Final review gate**

Review exact head against these locked boundaries:

```text
source immutability
begin lifetime independence
discovery semantic reuse
ref canonicality + wrong-session + tombstone
full uint32 flags
Contents absent/empty/counting/UTF-8 ownership
create type matrix
update/delete atomic rollback
appearance before journal completion
snapshot determinism/non-consumption/output independence
encrypted + signed fail-closed
JavaScript never executed
no MuPDF/PDF identity in public ABI
```

Any Critical/Important finding gets a new commit and invalidates the previous exact-head proof; rerun Linux and same-head full-ci after the fix.

- [ ] **Step 7: Mark PR ready only after evidence is complete**

Update PR/issue/roadmap bookkeeping to say **implementation/evidence complete; integration pending**. Do not merge as part of this task.

---

### Task 9: Explicit integration gate

**Files:**
- Bookkeeping only after successful merge/proof: PR #37 successor PR body, issue #37, roadmap #2.

**Interfaces:**
- Consumes: final reviewed exact-head full-ci evidence.
- Produces: integrated master proof and closes #37.

This task requires a separate explicit integration authorization after Task 8. A plan executor must stop before merge if that authorization has not been given.

- [ ] **Step 1: Re-fetch merge state immediately before merging**

Confirm:

```text
PR open + ready
head SHA exactly equals proven full-ci SHA
base master is expected integrated base/descendant
mergeable true
no unresolved review thread
no new review/comment blocker
same-head full-ci success
```

- [ ] **Step 2: Merge with `expected_head_sha`**

Use merge method `merge`, not an unreviewed squash/rebase transformation. GitHub must reject if head moved.

- [ ] **Step 3: Verify integrated master push workflow**

Find the `push` workflow whose `head_sha` equals the merge commit. Do not close #37 based only on PR CI.

Required integrated evidence:

```text
Linux static + all CTests       success
Linux ASan/UBSan + all CTests  success
macOS                           success
Windows DLL                     success
```

Inspect Windows logs to confirm the mutation test runs in the integrated DLL build.

- [ ] **Step 4: Close bookkeeping only after integrated GREEN**

Close #37 as completed and update roadmap #2 to:

```text
[x] Annotation mutation editor — #37 / PR #... — integrated
```

Record final feature SHA, merge SHA, same-head full-ci workflow, and integrated master push workflow. Keep Forms/widgets and subtype-specific geometry as separate future work.

---

## Execution Checkpoints

```text
Task 1  strict compile RED + remote RED evidence
   ↓
Task 2  shared annotation semantics; old 18-test regression gate
   ↓
Task 3  ABI + isolated lifecycle + fail-closed + baseline snapshot
   ↓
Task 4  discovery + canonical refs + live getters
   ↓
Task 5  create TEXT/FREE_TEXT/SQUARE/CIRCLE
   ↓
Task 6  update/delete + rollback + tombstone
   ↓
Task 7  source/snapshot/JS/output semantic proof
   ↓
Task 8  19/19 + sanitizers + same-SHA Linux/macOS/Windows full-ci + review
   ↓
STOP: explicit integration authorization
   ↓
Task 9  expected-head merge + integrated master proof + close #37
```

The implementation is not complete merely because CRUD works. The completion boundary is: deterministic public reparse proves the edited output, all ref/rollback/fail-closed contracts are locked, and the exact feature head passes all three operating-system jobs before integration.