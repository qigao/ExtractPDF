# ExtractPDF PDF Annotation Mutation V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an isolated PDF annotation editor that keeps `extractpdf_document` immutable, uses session-local annotation refs for atomic create/update/delete, and emits deterministic non-consuming `extractpdf_output` snapshots.

**Architecture:** `extractpdf_pdf_edit_begin()` first rejects unsupported source policies, then serializes the accepted source into deterministic owned bytes and reopens those bytes in a private MuPDF context. Immutable annotation enumeration and editor discovery share one private classifier/materializer. Editor refs are canonical session-local value tokens backed by a non-recycled private registry. Each CRUD call is one outer MuPDF journal operation; editor snapshots use MuPDF 1.28.2 `pdf_write_snapshot()` so the in-memory edit journal is not finalized or consumed.

**Tech Stack:** C11, CMake 3.20+, pinned MuPDF 1.28.2 through the existing vcpkg overlay, CTest, Linux ASan/UBSan, macOS, Windows DLL.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-pdf-annotation-mutation-design.md`

**Base:** integrated master `58525ce1b4b691ef53dfafc7c4e4d82753c966ba`; branch `feat/pdf-annotation-mutation`; issue #37; roadmap #2.

## Global Constraints

- Keep `extractpdf_document`, `extractpdf_page`, and all existing immutable snapshots read-only; no mutation API writes through those handles.
- `extractpdf_pdf_edit_begin()` becomes independent of the source lifetime after success.
- No MuPDF type, PDF object number/generation, `/NM`, filename, or pointer becomes public mutation identity.
- Immutable annotation indices remain snapshot-local/discovery-only and are never accepted by update/delete APIs.
- Editor discovery uses exactly the same Link/Popup/Widget filtering, UNKNOWN mapping, relative order, and strict Rect/F/Contents materialization contract as Annotation Enumeration V1.
- Public mutation bounds are finite ordered Fitz page-space rectangles. Reversed/non-finite input is `ARGUMENT`; V1 does not silently normalize mutation input.
- V1 create/bounds-update supports only TEXT, FREE_TEXT, SQUARE, and CIRCLE.
- Recognized ordinary annotations may receive generic flags/Contents update and delete. UNKNOWN mutation is `UNSUPPORTED`; a zero-field update is a validated no-op rather than a mutation.
- Preserve the complete raw `uint32_t` `/F` range. Never narrow values above `INT_MAX` through MuPDF's `int` convenience setter.
- Contents are counted UTF-8 with absent versus present-empty preserved. Present input must be valid UTF-8 and contain no embedded NUL.
- Every create/update/delete is one atomic outer MuPDF journal operation. Any failure restores the pre-call PDF and ref-registry observable state.
- `snapshot()` is non-consuming, deterministic for unchanged editor state, leaves refs valid, and returns an independent existing `extractpdf_output`.
- Reject every encrypted source and every source with an already-signed signature value as `UNSUPPORTED`. Unsigned signature fields remain allowed.
- Disable JavaScript in the private editor before exposing it. Begin/CRUD/appearance update/snapshot must not execute PDF JavaScript.
- No public undo/redo, incremental-save API, forms/widgets mutation, link mutation, Popup API, QuadPoints/line/vertex/Ink geometry, persistent identity, encrypted editing, or signed-PDF editing in V1.
- Preserve reset-before-later-validation behavior for every new output pointer.
- New option/update structs use the existing forward-compatible minimum-`struct_size` convention; larger structs are accepted.
- Keep the current single-thread handle contract. Add no mutable process-global/TLS state.
- Strict RED precedes all new production declarations/behavior. Final feature head requires Linux static + all CTests, Linux ASan/UBSan + all CTests, macOS, and Windows DLL proof.

---

## File Structure

### Public/API

- Modify `include/extractpdf/extractpdf.h`
  - add opaque `extractpdf_pdf_edit`;
  - add value `extractpdf_annotation_ref`;
  - add update-field/create/update types;
  - append `EXTRACTPDF_ERROR_STATE = 8`;
  - add begin/discovery/getter/CRUD/snapshot/drop APIs.
- Modify `src/status.c`
  - add stable status text for `EXTRACTPDF_ERROR_STATE`.

### Shared private annotation semantics

- Create `src/pdf_annotation_common.h`
  - private `extractpdf_pdf_annotation_view`;
  - survivor classification and strict common-materialization declarations.
- Create `src/pdf_annotation_common.c`
  - move the current subtype/filter/Rect/F/Contents logic out of `src/pdf_annotations.c` without changing its behavior.
- Modify `src/pdf_annotations.c`
  - consume the shared helpers and retain immutable snapshot allocation/copy/public accessors.

### Editor implementation

- Create `src/pdf_edit_internal.h`
  - private editor struct, ref registry, token helpers, page/object resolution helpers, and test-fault numeric constants under `EXTRACTPDF_TESTING`.
- Create `src/pdf_edit.c`
  - begin policy checks, source-to-private clone/context lifecycle, signed-field scan, JavaScript disable, journal enable, non-consuming snapshot, drop.
- Create `src/pdf_edit_annotations.c`
  - editor discovery, canonical ref registry, live getters, counted UTF-8 validation/copy, create/update/delete, full-uint32 flags write, appearance update, journal rollback.
- Modify `CMakeLists.txt`
  - compile the new production modules.

### Deterministic tests

- Create `tests/test_pdf_annotation_mutation.c`
  - one executable with named groups `arguments`, `begin`, `discovery`, `create`, `update-delete`, `contents-flags`, `snapshot`, `javascript` and no third-party framework.
- Create `tests/pdf_edit_test_api.h`
  - test-only fault enum/hook declaration; not installed and not included by the public header.
- Create `tests/pdf_edit_fault_hook.c`
  - test-only exported hook compiled into `extractpdf` only when tests are built; state remains per editor, never global.
- Create checked-in deterministic fixtures:
  - `tests/fixtures/annotation-mutation.pdf`
  - `tests/fixtures/annotation-mutation-signed.pdf`
  - `tests/fixtures/annotation-mutation-unsigned-signature.pdf`
  - `tests/fixtures/annotation-mutation-js.pdf`
- Reuse:
  - `tests/fixtures/annotations-late-malformed.pdf`
  - `tests/fixtures/encrypted-one-page.pdf` with password `user-pass`
  - `tests/fixtures/composition-non-pdf.txt`
- Modify `tests/CMakeLists.txt`
  - register the mutation target/CTest, fixture/output paths, fault-hook compilation, and Windows DLL-copy target.

No page/render/text/image/link/outline/metadata/composition behavior is refactored as part of this feature.

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
- Consumes: current public document, immutable annotation, metadata, and output APIs.
- Produces: the complete wished-for Mutation V1 contract as a compile RED. No new production declaration appears in this task.

- [ ] **Step 1: Generate deterministic checked-in PDFs**

Run this one-shot authoring script from repository root. Do not commit the script; commit only its outputs.

```bash
python3 - <<'PY'
from pathlib import Path

OUT = Path("tests/fixtures")


def write_pdf(path, objects, trailer_extra=""):
    parts = [b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"]
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(sum(len(part) for part in parts))
        parts.append(f"{number} 0 obj\n".encode("ascii"))
        parts.append(body.encode("latin1"))
        parts.append(b"\nendobj\n")
    xref = sum(len(part) for part in parts)
    parts.append(f"xref\n0 {len(objects)+1}\n".encode("ascii"))
    parts.append(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        parts.append(f"{offset:010d} 00000 n \n".encode("ascii"))
    parts.append(
        (f"trailer\n<< /Size {len(objects)+1} /Root 1 0 R {trailer_extra} >>\n"
         f"startxref\n{xref}\n%%EOF\n").encode("latin1"))
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
    write_pdf(path, [
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Annots [5 0 R] >>",
        "<< /Fields [5 0 R] /SigFlags 3 >>",
        f"<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Rect [10 10 120 40] /P 3 0 R{value} >>",
        "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
        "/ByteRange [0 0 0 0] /Contents <00> >>",
    ])


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
sha256sum tests/fixtures/annotation-mutation*.pdf
```

Run the generator a second time and repeat `sha256sum`. The four hashes must be unchanged.

Fixture contract:

```text
annotation-mutation.pdf page 0 survivors:
0 Text       flags 2147483649  contents text-a
1 Square     flags 4           contents square-b
2 UNKNOWN    flags 64          contents unknown-c
3 Highlight  flags 0           contents highlight-d
4 Ink        flags 0           contents ink-e

filtered page-0 entries: scalar 17, Link, Popup, Widget
page 1: FreeText then Circle
```

- [ ] **Step 2: Add the test-only fault declaration, with no implementation yet**

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

The unknown `extractpdf_pdf_edit` type is intentionally part of RED.

- [ ] **Step 3: Add the mutation test executable and common helpers**

Create `tests/test_pdf_annotation_mutation.c` using the existing `CHECK` style:

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
    return (d < 0.0f ? -d : d) < 0.01f;
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
```

Add helpers that:

```text
open source document
save extractpdf_output with extractpdf_output_save_file
read live edit info/Contents
reparse saved output through extractpdf_open -> load_page -> extract_annotations
verify missing/present-empty/non-empty Contents distinctly
```

`expect_snapshot_annotation()` must verify type, bounds, flags, and Contents through public immutable APIs; it must not inspect raw PDF objects.

- [ ] **Step 4: Lock output reset, struct-size, and basic argument contracts**

`test_arguments()` must include these exact checks:

```c
int sentinel = 0;
extractpdf_pdf_edit *edit = (extractpdf_pdf_edit *)&sentinel;
extractpdf_annotation_ref ref = {{UINT64_MAX, UINT64_MAX}};
extractpdf_annotation_info info = {0};
extractpdf_annotation_create_options create = {0};
extractpdf_annotation_update update = {0};
extractpdf_output *output = (extractpdf_output *)&sentinel;
char *text = (char *)(uintptr_t)1;
size_t size = 99;
size_t count = 99;

CHECK(extractpdf_pdf_edit_begin(NULL, &edit) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(edit == NULL);
CHECK(extractpdf_pdf_edit_begin(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

CHECK(extractpdf_pdf_edit_annotation_count(NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(count == 0);
CHECK(extractpdf_pdf_edit_annotation_count(NULL, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

CHECK(extractpdf_pdf_edit_annotation_ref_at(NULL, 0, 0, &ref) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(ref_is_zero(&ref));

info.struct_size = offsetof(extractpdf_annotation_info, flags);
CHECK(extractpdf_pdf_edit_annotation_get_info(NULL, &ref, &info) == EXTRACTPDF_ERROR_ARGUMENT);

text = (char *)(uintptr_t)1;
size = 99;
CHECK(extractpdf_pdf_edit_annotation_contents(NULL, &ref, &text, &size) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(text == NULL && size == 0);

create.struct_size = offsetof(extractpdf_annotation_create_options, contents_size);
zero_ref(&ref);
CHECK(extractpdf_pdf_edit_annotation_create(NULL, 0, &create, &ref) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(ref_is_zero(&ref));

update.struct_size = offsetof(extractpdf_annotation_update, contents_size);
CHECK(extractpdf_pdf_edit_annotation_update(NULL, &ref, &update) == EXTRACTPDF_ERROR_ARGUMENT);

output = (extractpdf_output *)&sentinel;
CHECK(extractpdf_pdf_edit_snapshot(NULL, &output) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(output == NULL);
CHECK(extractpdf_pdf_edit_snapshot(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

extractpdf_drop_pdf_edit(NULL);
CHECK(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_STATE), "invalid state") == 0);
```

After a real editor is available later, this same group must also verify too-small create/update `struct_size`, unknown update bits, bad page indices, and reset values. Larger-than-V1 structs must be accepted by providing a wrapper struct whose first member is the V1 struct plus trailing bytes.

- [ ] **Step 5: Lock begin lifetime and fail-closed policies**

`test_begin_lifetime_and_fail_closed()`:

```c
extractpdf_document *source = NULL;
extractpdf_pdf_edit *edit = NULL;
extractpdf_output *output = NULL;

CHECK(extractpdf_open(MUTATION_PDF, NULL, &source) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
CHECK(edit != NULL);
extractpdf_close(source);
source = NULL;
CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
CHECK(output != NULL);
extractpdf_drop_output(output);
extractpdf_drop_pdf_edit(edit);

CHECK(extractpdf_open(ENCRYPTED_PDF, "user-pass", &source) == EXTRACTPDF_OK);
edit = (extractpdf_pdf_edit *)(uintptr_t)1;
CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
CHECK(edit == NULL);
extractpdf_close(source);

CHECK(extractpdf_open(SIGNED_PDF, NULL, &source) == EXTRACTPDF_OK);
edit = (extractpdf_pdf_edit *)(uintptr_t)1;
CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
CHECK(edit == NULL);
extractpdf_close(source);

CHECK(extractpdf_open(UNSIGNED_SIGNATURE_PDF, NULL, &source) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_OK);
extractpdf_drop_pdf_edit(edit);
extractpdf_close(source);

CHECK(extractpdf_open(NON_PDF, NULL, &source) == EXTRACTPDF_OK);
edit = (extractpdf_pdf_edit *)(uintptr_t)1;
CHECK(extractpdf_pdf_edit_begin(source, &edit) == EXTRACTPDF_ERROR_UNSUPPORTED);
CHECK(edit == NULL);
extractpdf_close(source);
```

- [ ] **Step 6: Lock discovery/ref identity and malformed atomicity**

`test_discovery_and_refs()` must assert exactly five page-0 survivors and canonical repeated ref acquisition:

```c
CHECK(extractpdf_pdf_edit_annotation_count(a, 0, &count) == EXTRACTPDF_OK);
CHECK(count == 5);
CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 0, &text_ref) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 1, &square_ref) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_ref_at(a, 0, 1, &square_again) == EXTRACTPDF_OK);
CHECK(memcmp(&square_ref, &square_again, sizeof(square_ref)) == 0);
```

Open the same source into editor B and verify `square_ref` used with B returns `ARGUMENT`. Verify page `-1`, page `2`, and index `99` are `ARGUMENT` with reset outputs.

For `annotations-late-malformed.pdf`:

```c
count = 99;
CHECK(extractpdf_pdf_edit_annotation_count(edit, 0, &count) == EXTRACTPDF_ERROR_FORMAT);
CHECK(count == 0);
ref.opaque[0] = UINT64_MAX;
ref.opaque[1] = UINT64_MAX;
CHECK(extractpdf_pdf_edit_annotation_ref_at(edit, 0, 0, &ref) == EXTRACTPDF_ERROR_FORMAT);
CHECK(ref_is_zero(&ref));
```

No prefix ref may be registered by the failed scan.

- [ ] **Step 7: Lock create/update/delete/rollback/type-matrix behavior**

Create helper:

```c
static extractpdf_annotation_ref create_rect_annot(
    extractpdf_pdf_edit *edit,
    int page_index,
    extractpdf_annotation_type type,
    extractpdf_rect bounds,
    uint32_t flags,
    const char *contents,
    size_t contents_size)
{
    extractpdf_annotation_create_options options = {0};
    extractpdf_annotation_ref ref = {{0,0}};
    options.struct_size = sizeof(options);
    options.type = type;
    options.bounds = bounds;
    options.flags = flags;
    options.contents_utf8 = contents;
    options.contents_size = contents_size;
    CHECK(extractpdf_pdf_edit_annotation_create(
              edit, page_index, &options, &ref) == EXTRACTPDF_OK);
    CHECK(!ref_is_zero(&ref));
    return ref;
}
```

`test_create()` creates these four exact cases and verifies each through live getters:

```text
page 0 TEXT      [20,20,35,35]   flags 1   Contents "new-text"
page 1 FREE_TEXT [20,20,90,55]   flags 4   Contents "new-free"
page 1 SQUARE    [100,20,150,70] flags 8   Contents absent
page 1 CIRCLE    [20,90,70,140]  flags 16  Contents present-empty
```

Then set the same valid options to HIGHLIGHT and UNKNOWN and require `UNSUPPORTED + zero ref`.

`test_update_delete()` must:

```text
1. acquire Text ref and Square ref from page 0;
2. delete Text;
3. require Text get_info/contents/update/delete => STATE;
4. reacquire page-0 index 0 and require byte-equal token to original Square ref;
5. update Square bounds + flags + Contents using original Square ref;
6. verify all three changed;
7. acquire Highlight ref; BOUNDS update => UNSUPPORTED;
8. Highlight FLAGS|CONTENTS update => OK;
9. live UNKNOWN zero-field update => OK;
10. tombstone zero-field update => STATE;
11. wrong-session zero-field update => ARGUMENT.
```

Atomic update fault must save pre-call values explicitly:

```c
extractpdf_annotation_info before = {0};
extractpdf_annotation_info after = {0};
char *before_text = NULL;
char *after_text = NULL;
size_t before_size = 0;
size_t after_size = 0;
extractpdf_annotation_update change = {0};

before.struct_size = sizeof(before);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &square_ref, &before) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_contents(
          edit, &square_ref, &before_text, &before_size) == EXTRACTPDF_OK);

change.struct_size = sizeof(change);
change.fields = EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS |
                EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
change.bounds = (extractpdf_rect){20, 20, 90, 90};
change.contents_utf8 = "rollback-new";
change.contents_size = sizeof("rollback-new") - 1;
extractpdf_test_pdf_edit_set_fault(
    edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD);
CHECK(extractpdf_pdf_edit_annotation_update(edit, &square_ref, &change) != EXTRACTPDF_OK);

after.struct_size = sizeof(after);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &square_ref, &after) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_contents(
          edit, &square_ref, &after_text, &after_size) == EXTRACTPDF_OK);
CHECK(close_float(before.bounds.x0, after.bounds.x0));
CHECK(close_float(before.bounds.y0, after.bounds.y0));
CHECK(close_float(before.bounds.x1, after.bounds.x1));
CHECK(close_float(before.bounds.y1, after.bounds.y1));
CHECK(before_size == after_size);
CHECK(before_size == 0 || memcmp(before_text, after_text, before_size) == 0);
extractpdf_free(before_text);
extractpdf_free(after_text);
```

Create rollback:

```c
CHECK(extractpdf_pdf_edit_annotation_count(edit, 0, &before_count) == EXTRACTPDF_OK);
extractpdf_test_pdf_edit_set_fault(
    edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION);
zero_ref(&new_ref);
CHECK(extractpdf_pdf_edit_annotation_create(edit, 0, &options, &new_ref) != EXTRACTPDF_OK);
CHECK(ref_is_zero(&new_ref));
CHECK(extractpdf_pdf_edit_annotation_count(edit, 0, &after_count) == EXTRACTPDF_OK);
CHECK(after_count == before_count);
```

- [ ] **Step 8: Lock full-u32 flags and counted Contents semantics**

`test_contents_flags()` begins with the existing Text:

```c
info.struct_size = sizeof(info);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &text_ref, &info) == EXTRACTPDF_OK);
CHECK(info.flags == UINT32_C(2147483649));

update.struct_size = sizeof(update);
update.fields = EXTRACTPDF_ANNOTATION_UPDATE_FLAGS;
update.flags = UINT32_MAX;
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text_ref, &update) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_annotation_get_info(edit, &text_ref, &info) == EXTRACTPDF_OK);
CHECK(info.flags == UINT32_MAX);
```

Also use:

```c
static const char counted[3] = {'c','a','t'};
static const char bad_utf8[2] = {(char)0xC0, (char)0xAF};
static const char embedded_nul[3] = {'a','\0','b'};
```

Required sequence:

```text
CONTENTS=counted/3 -> OK, getter returns allocated "cat" + NUL
mutate to "dog" -> previously returned allocated copy still equals "cat"
CONTENTS=NULL/0 -> remove; getter returns NULL/0
CONTENTS=nonNULL/0 -> present empty; getter returns nonNULL/0
bad_utf8/2 -> ARGUMENT, state unchanged
embedded_nul/3 -> ARGUMENT, state unchanged
NULL/1 -> ARGUMENT
```

- [ ] **Step 9: Lock source/snapshot/output isolation and JavaScript non-execution**

Before beginning an editor, obtain an immutable source annotation snapshot and retain it. After begin succeeds, close source, mutate editor, then prove the retained source snapshot still reads:

```text
Text bounds [10,160,30,180]
flags 2147483649
Contents "text-a"
```

Snapshot test:

```c
CHECK(extractpdf_pdf_edit_snapshot(edit, &a) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_snapshot(edit, &repeat) == EXTRACTPDF_OK);
CHECK(extractpdf_output_data(a, &a_data, &a_size) == EXTRACTPDF_OK);
CHECK(extractpdf_output_data(repeat, &repeat_data, &repeat_size) == EXTRACTPDF_OK);
CHECK(a_size == repeat_size);
CHECK(memcmp(a_data, repeat_data, a_size) == 0);

a_copy = malloc(a_size);
CHECK(a_copy != NULL);
memcpy(a_copy, a_data, a_size);

update.fields = EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
update.contents_utf8 = "snapshot-b";
update.contents_size = sizeof("snapshot-b") - 1;
CHECK(extractpdf_pdf_edit_annotation_update(edit, &text_ref, &update) == EXTRACTPDF_OK);
CHECK(extractpdf_pdf_edit_snapshot(edit, &b) == EXTRACTPDF_OK);

CHECK(extractpdf_output_data(a, &a_data, &a_size) == EXTRACTPDF_OK);
CHECK(memcmp(a_data, a_copy, a_size) == 0);
```

Save A/B, reparse both through immutable annotation enumeration, and require A=`text-a`, B=`snapshot-b`. Drop the editor and re-read A/B output bytes again to prove output lifetime independence.

Arm `SNAPSHOT_BEFORE_PUBLISH`, require failure + NULL output, then perform a real Contents update through the same ref and require success; a mere getter is not sufficient proof that the editor remains usable after failed snapshot.

JavaScript fixture starts with `/Info /Title (SAFE)` and `/OpenAction` JavaScript `this.title = "EXECUTED"`. `test_javascript_disabled()` must begin an editor, update the Text annotation so `pdf_update_annot()` executes, snapshot, reparse, and require public metadata Title remains exactly `SAFE`.

- [ ] **Step 10: Add named test-group dispatch**

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

- [ ] **Step 11: Register the RED target**

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

Add `extractpdf_test_pdf_annotation_mutation` to the existing Windows DLL-copy list. Do **not** add `pdf_edit_fault_hook.c` in RED.

- [ ] **Step 12: Run and capture strict RED**

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
```

Valid RED:

```text
extractpdf library builds
all 18 pre-existing test targets build
only extractpdf_test_pdf_annotation_mutation fails to compile
failure is absent approved ABI: editor/ref/types/status/constants/functions
```

A fixture/runtime error, missing header/path, or old target regression is not valid RED.

Commit:

```bash
git add tests/fixtures/annotation-mutation.pdf \
        tests/fixtures/annotation-mutation-signed.pdf \
        tests/fixtures/annotation-mutation-unsigned-signature.pdf \
        tests/fixtures/annotation-mutation-js.pdf \
        tests/pdf_edit_test_api.h \
        tests/test_pdf_annotation_mutation.c \
        tests/CMakeLists.txt
git commit -m "test: define PDF annotation mutation red"
```

Open a draft PR to `master`, link #37/#2, push, and record exact RED SHA + workflow in PR #body and #37 before any production ABI is added.

---

### Task 2: Extract one shared annotation semantic core

**Files:**
- Create: `src/pdf_annotation_common.h`
- Create: `src/pdf_annotation_common.c`
- Modify: `src/pdf_annotations.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: current immutable Annotation Enumeration V1 behavior.
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
    pdf_obj *annotation,
    extractpdf_annotation_type type,
    fz_matrix page_ctm,
    extractpdf_pdf_annotation_view *out_view);
```

- [ ] **Step 1: Prove the existing annotation target is green before refactor**

The RED build already produced every old executable before failing the new target. Run:

```bash
ctest --test-dir build -R '^extractpdf\.pdf_annotations$' --output-on-failure
```

Expected: pass.

- [ ] **Step 2: Move classification/materialization helpers without behavior changes**

Create `src/pdf_annotation_common.h/.c`. Move the existing private logic for:

```text
dictionary key presence lookup
Subtype explicit mapping
Link/Popup/Widget filtering
UNKNOWN mapping
strict four-finite-number Rect validation
strict uint32 /F validation
strict optional PDF-string /Contents validation + pdf_to_text_string decoding
```

`read_view()` receives a caller-computed `page_ctm`; it must not recompute page transform for each annotation. It zero-initializes all output fields and maps normalized PDF Rect to normalized Fitz bounds with `fz_transform_rect(raw, page_ctm)`.

The returned Contents pointer is private/borrowed and must be copied before leaving the current MuPDF access scope.

- [ ] **Step 3: Rewrite immutable enumeration to use shared helpers**

First pass continues raw `/Annots` traversal and calls only `classify()`.

Second pass computes `pdf_page_transform()` once, then:

```c
extractpdf_pdf_annotation_view view;
status = extractpdf_pdf_annotation_read_view(
    ctx, annotation, type, page_ctm, &view);
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

Delete the duplicate static classifier/Rect/F/Contents code from `src/pdf_annotations.c`.

- [ ] **Step 4: Build every old target explicitly while mutation stays compile-RED**

```bash
cmake --build build --parallel 2 --target \
  extractpdf_test_status \
  extractpdf_test_document \
  extractpdf_test_render \
  extractpdf_test_text \
  extractpdf_test_structured_text \
  extractpdf_test_text_search \
  extractpdf_test_images \
  extractpdf_test_image_bitmap \
  extractpdf_test_links \
  extractpdf_test_pdf_export \
  extractpdf_test_pdf_range \
  extractpdf_test_pdf_order \
  extractpdf_test_pdf_delete \
  extractpdf_test_pdf_merge \
  extractpdf_test_output_file \
  extractpdf_test_pdf_metadata \
  extractpdf_test_pdf_outline \
  extractpdf_test_pdf_annotations
ctest --test-dir build -E '^extractpdf\.pdf_annotation_mutation$' --output-on-failure
```

Expected: 18/18 old CTests pass. Do not run a full default build and misclassify the still-intentional mutation compile RED as a refactor regression.

- [ ] **Step 5: Commit**

```bash
git add src/pdf_annotation_common.h src/pdf_annotation_common.c \
        src/pdf_annotations.c CMakeLists.txt
git commit -m "refactor: share PDF annotation semantics"
```

Reviewer rejects this task if any old annotation behavior changes or editor-specific state enters the common module.

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
- Consumes: `extractpdf_serialize_pdf()`, private `extractpdf_output` bytes, MuPDF PDF document/stream/javascript/journal/signature APIs.
- Produces: complete linkable public Mutation V1 ABI; real begin/snapshot/drop; annotation calls initially provide exact reset/validation shells and otherwise `UNSUPPORTED` until Tasks 4-6.

- [ ] **Step 1: Add exact public ABI**

Add to `include/extractpdf/extractpdf.h`:

```c
typedef struct extractpdf_pdf_edit extractpdf_pdf_edit;

typedef struct extractpdf_annotation_ref {
    uint64_t opaque[2];
} extractpdf_annotation_ref;

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

Append status value without renumbering old values:

```c
EXTRACTPDF_ERROR_STATE = 8
```

Add exactly these public functions:

```c
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_begin(
    extractpdf_document *, extractpdf_pdf_edit **);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_count(
    extractpdf_pdf_edit *, int, size_t *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_ref_at(
    extractpdf_pdf_edit *, int, size_t, extractpdf_annotation_ref *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_get_info(
    extractpdf_pdf_edit *, const extractpdf_annotation_ref *, extractpdf_annotation_info *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_contents(
    extractpdf_pdf_edit *, const extractpdf_annotation_ref *, char **, size_t *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_create(
    extractpdf_pdf_edit *, int, const extractpdf_annotation_create_options *, extractpdf_annotation_ref *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_update(
    extractpdf_pdf_edit *, const extractpdf_annotation_ref *, const extractpdf_annotation_update *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_delete(
    extractpdf_pdf_edit *, const extractpdf_annotation_ref *);
EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_snapshot(
    extractpdf_pdf_edit *, extractpdf_output **);
EXTRACTPDF_API void extractpdf_drop_pdf_edit(extractpdf_pdf_edit *);
```

- [ ] **Step 2: Add stable STATE string**

In `src/status.c`:

```c
case EXTRACTPDF_ERROR_STATE:
    return "invalid state";
```

- [ ] **Step 3: Define private editor/registry/test-fault representation**

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

#if defined(EXTRACTPDF_TESTING)
enum {
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE = 0,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD = 1,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_AFTER_CREATE_MUTATION = 2,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_SNAPSHOT_BEFORE_PUBLISH = 3
};
#endif

struct extractpdf_pdf_edit {
    fz_context *ctx;
    pdf_document *document;
    extractpdf_output *seed_output;
    uint64_t session_cookie;
    extractpdf_pdf_edit_annotation_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
#if defined(EXTRACTPDF_TESTING)
    int test_fault;
#endif
};

#endif
```

The test header and private enum intentionally share numeric values but not a header dependency from `src/` to `tests/`.

- [ ] **Step 4: Implement signed-field scan without widget/event execution**

In `src/pdf_edit.c`, walk `Root/AcroForm/Fields` using `pdf_walk_tree()` and inherited `/FT`. For a `/Sig` field call `pdf_signature_is_signed(ctx, document, field)`. A found signed field is enough to reject the source. Do not load widgets, enable JS, or dispatch events for this scan.

- [ ] **Step 5: Implement atomic `extractpdf_pdf_edit_begin()`**

Required order:

```text
reset *out_edit
validate source internals
pdf_document_from_fz_document -> NULL => UNSUPPORTED
raw trailer /Encrypt present => UNSUPPORTED
signed-field scan => signed => UNSUPPORTED
extractpdf_serialize_pdf(source ctx/pdf) -> seed_output
allocate edit
new independent fz_context + discard callbacks
fz_open_memory(seed_output bytes)
pdf_open_document_with_stream
pdf_disable_js
pdf_enable_journal
create nonzero session_cookie
publish edit
```

Keep `seed_output` alive for the whole editor lifetime because the private document's input stream may reference its memory.

Use a private 64-bit mixer:

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

Build the nonzero session discriminator from editor/context addresses, `time(NULL)`, `clock()`, and eight bytes from editor-local `fz_memrnd()`. This is an opaque session discriminator, not a cryptographic authentication boundary; no mutable global counter/state is introduced.

Any exception tears down all partial state and maps through `extractpdf_status_from_mupdf()`.

- [ ] **Step 6: Implement non-consuming snapshot with MuPDF's snapshot writer**

Do **not** call the existing full-finalizing `extractpdf_serialize_pdf()` on the live edit document. MuPDF 1.28.2 provides `pdf_write_snapshot()`, specifically documented to avoid finalizing the in-memory incremental xref.

In `src/pdf_edit.c`, implement a private buffer materializer:

```c
static extractpdf_status snapshot_pdf(
    extractpdf_pdf_edit *edit,
    extractpdf_output **out_output)
{
    fz_buffer *buffer = NULL;
    fz_output *memory_output = NULL;
    unsigned char *data = NULL;
    size_t size = 0;
    extractpdf_output *result = NULL;
    int caught = FZ_ERROR_NONE;

    *out_output = NULL;
    fz_var(buffer);
    fz_var(memory_output);
    fz_var(data);
    fz_var(size);
    fz_var(caught);

    fz_try(edit->ctx) {
        buffer = fz_new_buffer(edit->ctx, 0);
        memory_output = fz_new_output_with_buffer(edit->ctx, buffer);
        pdf_write_snapshot(edit->ctx, edit->document, memory_output);
        fz_close_output(edit->ctx, memory_output);
        size = fz_buffer_storage(edit->ctx, buffer, &data);
    }
    fz_catch(edit->ctx) {
        caught = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }
    if (caught != FZ_ERROR_NONE) {
        if (memory_output != NULL) fz_drop_output(edit->ctx, memory_output);
        if (buffer != NULL) fz_drop_buffer(edit->ctx, buffer);
        return extractpdf_status_from_mupdf(caught);
    }
    if (data == NULL || size == 0) {
        fz_drop_output(edit->ctx, memory_output);
        fz_drop_buffer(edit->ctx, buffer);
        return EXTRACTPDF_ERROR_MUPDF;
    }

    result = calloc(1, sizeof(*result));
    if (result == NULL) {
        fz_drop_output(edit->ctx, memory_output);
        fz_drop_buffer(edit->ctx, buffer);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    result->data = malloc(size);
    if (result->data == NULL) {
        free(result);
        fz_drop_output(edit->ctx, memory_output);
        fz_drop_buffer(edit->ctx, buffer);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    memcpy(result->data, data, size);
    result->size = size;
    fz_drop_output(edit->ctx, memory_output);
    fz_drop_buffer(edit->ctx, buffer);
    *out_output = result;
    return EXTRACTPDF_OK;
}
```

`extractpdf_pdf_edit_snapshot()` resets output, validates edit, calls this helper, then under `EXTRACTPDF_TESTING` consumes `SNAPSHOT_BEFORE_PUBLISH` by dropping the just-created result and returning `EXTRACTPDF_ERROR_MUPDF`. Otherwise publish result.

The same-state byte-identity RED test is the acceptance proof that the pinned snapshot writer is deterministic for this editor model; do not weaken that assertion.

- [ ] **Step 7: Implement drop and exact annotation API shells**

`extractpdf_drop_pdf_edit()` drops every kept registry object, registry allocation, private PDF document, private context, and finally `seed_output`; NULL is no-op. Drop document before context and seed bytes only after document no longer references its input stream.

Define all annotation functions in `src/pdf_edit_annotations.c` so the test executable links. At this task they must implement exact output reset/pointer/`struct_size` validation and otherwise return `UNSUPPORTED` for a valid editor until the corresponding later task.

- [ ] **Step 8: Add per-editor test fault hook**

In `tests/CMakeLists.txt`:

```cmake
target_sources(extractpdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/pdf_edit_fault_hook.c")
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

Production sources compare `edit->test_fault` only against the private `EXTRACTPDF_PDF_EDIT_TEST_FAULT_*` constants, never identifiers from `tests/pdf_edit_test_api.h`.

- [ ] **Step 9: Register production sources and run lifecycle groups**

Add to root `add_library(extractpdf ...)`:

```cmake
src/pdf_annotation_common.c
src/pdf_edit.c
src/pdf_edit_annotations.c
```

Then:

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation arguments
./build/tests/extractpdf_test_pdf_annotation_mutation begin
```

Expected: `arguments` and `begin` pass. Discovery/CRUD groups remain behavioral RED because their shells return `UNSUPPORTED`.

- [ ] **Step 10: Commit**

```bash
git add include/extractpdf/extractpdf.h src/status.c \
        src/pdf_edit_internal.h src/pdf_edit.c src/pdf_edit_annotations.c \
        tests/pdf_edit_fault_hook.c tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add isolated PDF editor lifecycle"
```

---

### Task 4: Implement discovery, canonical refs, and live getters

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: shared annotation classifier/view and private editor PDF.
- Produces: real count/ref_at/get_info/contents plus canonical refs that survive index shifts.

- [ ] **Step 1: Run focused failing discovery group**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation discovery
```

Expected: fail on current `UNSUPPORTED` shells.

- [ ] **Step 2: Add private identity and registry-capacity helpers**

Compare private annotation identity exactly:

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

Never compare dictionary contents. `reserve_entries()` overflow-checks allocation and runs before an operation that cannot tolerate a later allocation failure.

- [ ] **Step 3: Define canonical token encoding and validation**

Never recycle a tombstoned slot. Repeated acquisition of a live object returns its existing slot/token.

Concrete token layout:

```text
opaque[0] = edit->session_cookie
opaque[1] = (uint64_t(entry->tag) << 32) | uint64_t(slot + 1)
```

Maximum slots: `UINT32_MAX - 1`; exceeding it is `NOMEM`.

Derive `entry->tag` deterministically from `mix64(edit->session_cookie ^ (slot + 1))`, force zero to `1`; no second random/global state is required.

Resolution order:

```text
NULL edit/ref                        ARGUMENT
opaque[0] != session_cookie          ARGUMENT
encoded slot 0/out of range          ARGUMENT
tag mismatch                         ARGUMENT
matching slot with live==0           STATE
matching live entry                  OK
```

- [ ] **Step 4: Scan and validate the entire requested page before publication**

Implement:

```c
static extractpdf_status scan_page(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t wanted_index,
    int want_object,
    size_t *out_count,
    pdf_obj **out_object);
```

Sequence:

```text
validate page index with pdf_count_pages
pdf_load_page
pdf_page_transform once
raw page->obj /Annots walk
non-dict skip
shared classify: Link/Popup/Widget skip; other dict survives
shared read_view for EVERY survivor
only after whole scan succeeds keep wanted object
release page
publish count/object
```

A malformed late survivor returns `FORMAT`; it publishes no new object/ref from the prefix.

- [ ] **Step 5: Implement count/ref_at**

`count()` resets output and publishes the scanned count only on complete success.

`ref_at()` zeros output, scans/validates the complete page, checks `index < count`, then either finds the existing canonical registry entry or appends one kept private `pdf_obj *` and publishes its token.

- [ ] **Step 6: Resolve a ref to a live `pdf_page` + borrowed `pdf_annot`**

Do not return a `pdf_annot *` after its page is dropped because the annotation borrows its page pointer.

Private resolver shape:

```c
static extractpdf_status resolve_live_annot(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_annotation_entry *entry,
    pdf_page **out_page,
    pdf_annot **out_annot);
```

It loads `entry->page_index`, iterates `pdf_first_annot()/pdf_next_annot()`, compares `pdf_annot_obj()` to the kept registry object, and returns a borrowed annot while `*out_page` remains alive. Caller drops the page after all work. If the registry says live but object is no longer present, return `STATE`.

- [ ] **Step 7: Implement live getters**

`get_info()` validates minimum existing `extractpdf_annotation_info` size, preserves `struct_size`, resets known fields, resolves page/annot, computes one page CTM, calls shared `read_view()`, and copies type/bounds/flags.

`contents()` independently resets non-NULL outputs, requires both outputs, resolves/read_view, and returns an allocated copy:

```c
if (!view.has_contents)
    return EXTRACTPDF_OK;
if (view.contents_size == SIZE_MAX)
    return EXTRACTPDF_ERROR_NOMEM;
copy = malloc(view.contents_size + 1);
if (copy == NULL)
    return EXTRACTPDF_ERROR_NOMEM;
memcpy(copy, view.contents_utf8, view.contents_size + 1);
*out_utf8 = copy;
*out_size = view.contents_size;
```

- [ ] **Step 8: Run discovery + immutable regression gates**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation discovery
ctest --test-dir build -R '^extractpdf\.pdf_annotations$' --output-on-failure
```

Expected: both pass.

- [ ] **Step 9: Commit**

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
- Consumes: editor page loading, registry reserve/publish, MuPDF journal/create/setter APIs.
- Produces: atomic TEXT/FREE_TEXT/SQUARE/CIRCLE create with immediate canonical live ref.

- [ ] **Step 1: Run focused failing create group**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation create
```

Expected: fail on create `UNSUPPORTED` shell.

- [ ] **Step 2: Implement counted UTF-8 validation/copy**

Canonical accepted byte forms:

```text
ASCII: 01..7F
2-byte: C2..DF 80..BF
3-byte: E0 A0..BF 80..BF
        E1..EC 80..BF 80..BF
        ED 80..9F 80..BF
        EE..EF 80..BF 80..BF
4-byte: F0 90..BF 80..BF 80..BF
        F1..F3 80..BF 80..BF 80..BF
        F4 80..8F 80..BF 80..BF
```

Reject byte 00 anywhere in a present counted range, overlong sequences, surrogates, >U+10FFFF, and truncated/invalid continuations. For present data, overflow-check `size + 1`, allocate, copy exactly `size`, append NUL. NULL/0 is the absent marker and allocates nothing.

- [ ] **Step 3: Validate create request before journal mutation**

Minimum size:

```c
offsetof(extractpdf_annotation_create_options, contents_size) +
    sizeof(options->contents_size)
```

Validate page index, supported type, finite ordered bounds, and Contents tuple. Allow zero-width/zero-height rectangles.

Map only:

```c
TEXT      -> PDF_ANNOT_TEXT
FREE_TEXT -> PDF_ANNOT_FREE_TEXT
SQUARE    -> PDF_ANNOT_SQUARE
CIRCLE    -> PDF_ANNOT_CIRCLE
```

All other types are `UNSUPPORTED`.

- [ ] **Step 4: Implement full-uint32 flag writer**

```c
static void set_annot_flags_u32(
    fz_context *ctx, pdf_annot *annot, uint32_t flags)
{
    if (flags <= (uint32_t)INT_MAX) {
        pdf_set_annot_flags(ctx, annot, (int)flags);
    } else {
        pdf_dict_put_int(
            ctx,
            pdf_annot_obj(ctx, annot),
            PDF_NAME(F),
            (int64_t)(uint64_t)flags);
    }
}
```

This mirrors the low-range setter's dictionary result without signed narrowing. The enclosing public operation and later `pdf_update_annot()` provide the same mutation/appearance boundary.

- [ ] **Step 5: Implement create inside one outer journal operation**

Reserve registry capacity before `pdf_begin_operation()`.

```text
begin outer operation
pdf_create_annot
set Rect
set full-u32 flags
set Contents only when present
pdf_update_annot
optional deterministic test fault before outer end
pdf_end_operation
fill pre-reserved registry slot from pdf_annot_obj
publish token
```

Under `EXTRACTPDF_TESTING`, `AFTER_CREATE_MUTATION` clears itself and throws `FZ_ERROR_GENERIC` before `pdf_end_operation()`.

On exception: `pdf_abandon_operation()`, drop owned annot/page/temp Contents, leave output ref zero, and do not increment registry count.

- [ ] **Step 6: Run create group and create-rollback assertions**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation create
```

Expected: all four create types pass, Highlight/UNKNOWN create are `UNSUPPORTED`, injected create failure leaves count unchanged and output ref zero.

- [ ] **Step 7: Commit**

```bash
git add src/pdf_edit_annotations.c
git commit -m "feat: create Rect-based PDF annotations"
```

---

### Task 6: Implement partial update, delete, Contents ownership, and rollback

**Files:**
- Modify: `src/pdf_edit_annotations.c`
- Test: `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: canonical refs, UTF-8 helper, full-u32 flags helper, journal/setter/delete APIs, per-editor fault.
- Produces: atomic partial update/delete, tombstones, exact Contents presence semantics, full-u32 round-trip.

- [ ] **Step 1: Run focused failing groups**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation update-delete
./build/tests/extractpdf_test_pdf_annotation_mutation contents-flags
```

Expected: fail on current update/delete shells.

- [ ] **Step 2: Validate entire update request before mutation**

Minimum size is through `contents_size`. Resolve ref **before** zero-field no-op handling.

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

Then:

```text
UNKNOWN + nonzero fields -> UNSUPPORTED
BOUNDS on non Text/FreeText/Square/Circle -> UNSUPPORTED
requested bounds -> validate finite/ordered
requested Contents -> validate tuple/UTF-8 and allocate temp C string before operation
```

- [ ] **Step 3: Preserve absent versus present-empty Contents**

Present string uses `pdf_set_annot_contents()`.

Removal uses:

```c
pdf_dict_del(ctx, pdf_annot_obj(ctx, annot), PDF_NAME(Contents));
pdf_annot_request_resynthesis(ctx, annot);
```

Do not turn removal into `pdf_set_annot_contents(annot, "")`.

- [ ] **Step 4: Apply fixed-order update atomically**

Order: BOUNDS -> FLAGS -> CONTENTS. After each requested field increments `applied_fields`; when `AFTER_FIRST_UPDATE_FIELD` is armed and `applied_fields == 1`, clear it and throw before the next field.

After all fields:

```c
pdf_update_annot(ctx, annot);
pdf_end_operation(ctx, edit->document);
```

Any exception abandons the outer operation and leaves registry state untouched.

- [ ] **Step 5: Implement delete and tombstone only after successful PDF delete**

Resolve the live ref to page/annot. UNKNOWN is `UNSUPPORTED`; any recognized ordinary annotation is deletable.

```c
pdf_begin_operation(ctx, edit->document, "ExtractPDF delete annotation");
pdf_delete_annot(ctx, page, annot);
pdf_end_operation(ctx, edit->document);
entry->live = 0;
```

On exception abandon and keep `entry->live == 1`. Never recycle that slot. Associated Popup removal performed by MuPDF is accepted internal consistency behavior.

- [ ] **Step 6: Run update/delete/Contents/full-u32 tests**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation update-delete
./build/tests/extractpdf_test_pdf_annotation_mutation contents-flags
```

Expected:

```text
failed multi-field update restores all fields
Text tombstone returns STATE from get/info/contents/update/delete
Square ref remains stable after Text deletion shifts index
Highlight bounds UNSUPPORTED, generic flags/Contents OK
live UNKNOWN zero-mask OK
wrong-session zero-mask ARGUMENT
tombstone zero-mask STATE
raw 2147483649 and UINT32_MAX round-trip exactly
counted non-NUL-terminated input works
owned getter string survives later edit mutation
absent vs present-empty preserved
invalid UTF-8 / embedded NUL / NULL+nonzero rejected before mutation
```

- [ ] **Step 7: Commit**

```bash
git add src/pdf_edit_annotations.c tests/test_pdf_annotation_mutation.c
git commit -m "feat: atomically update and delete annotations"
```

---

### Task 7: Prove snapshot/source isolation and JavaScript non-execution through public outputs

**Files:**
- Modify only if a real defect is exposed: `src/pdf_edit.c`, `src/pdf_edit_annotations.c`, `tests/test_pdf_annotation_mutation.c`

**Interfaces:**
- Consumes: completed lifecycle/discovery/CRUD implementation.
- Produces: final semantic proof before all-suite GREEN.

- [ ] **Step 1: Run snapshot group unchanged**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation snapshot
```

Expected: pass. Failure is a product defect; do not weaken same-state byte identity, source isolation, output independence, or non-consuming assertions.

- [ ] **Step 2: Require retained source snapshot to stay original after source close + editor mutation**

Verify through retained immutable snapshot:

```text
Text type
Fitz bounds 10,160,30,180
flags 2147483649
Contents text-a
```

No source handle remains open solely for this assertion.

- [ ] **Step 3: Reparse snapshots A and B only through public immutable APIs**

Required:

```text
A bytes == repeat bytes with no mutation
A bytes remain unchanged after mutation
B contains later mutation
A reparse -> text-a
B reparse -> snapshot-b
existing Text ref stays valid after snapshots
outputs remain valid after editor drop
```

- [ ] **Step 4: Prove failed snapshot does not consume/corrupt editor**

Arm `SNAPSHOT_BEFORE_PUBLISH`, require non-OK + NULL output, then update Contents to `after-failed-snapshot`, read it through the same ref, snapshot again, reparse, and require the new value. This proves post-failure mutation and publication both remain usable.

- [ ] **Step 5: Run JavaScript group after real appearance path exists**

```bash
./build/tests/extractpdf_test_pdf_annotation_mutation javascript
```

The group performs a Text Contents update, snapshots, reparses metadata, and requires Title `SAFE`. Any `EXECUTED` result is a blocker.

- [ ] **Step 6: Run full mutation executable**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_annotation_mutation
```

Expected: all groups pass.

- [ ] **Step 7: Commit only real corrections**

If Step 1-6 required changes:

```bash
git add src/pdf_edit.c src/pdf_edit_annotations.c tests/test_pdf_annotation_mutation.c
git commit -m "test: lock PDF annotation editor isolation"
```

If no files changed, create no empty commit.

---

### Task 8: Reach full GREEN and exact-head cross-platform proof

**Files:**
- No planned feature expansion.
- Modify only approved-scope files if a real compiler/portability defect is found.

**Interfaces:**
- Consumes: Tasks 1-7.
- Produces: merge-ready exact-head evidence; does not merge.

- [ ] **Step 1: Fresh Linux static build + all CTests**

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

Expected: **19/19 CTests pass**.

- [ ] **Step 2: Fresh Linux ASan/UBSan + all CTests**

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

Expected: 19/19, no sanitizer issue in editor/ref/string/registry code.

- [ ] **Step 3: Exact-head scope review**

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

No unrelated page/render/text/image/link/outline/metadata/composition/output-file change is acceptable.

- [ ] **Step 4: Push final GREEN and capture exact-head Linux PR CI**

Normal PR synchronize on the final exact SHA must pass:

```text
Linux strict static build + 19/19 CTests
Linux ASan/UBSan + 19/19 CTests
```

Update draft PR and #37 with RED SHA/workflow, first production GREEN SHA/workflow, and final exact SHA.

- [ ] **Step 5: Trigger same-head `full-ci`**

Add `full-ci` only after final SHA is fixed. Verify labeled workflow `head_sha` equals that SHA.

Acceptance:

```text
Linux static + 19/19                 success
Linux ASan/UBSan + 19/19            success
macOS configure/build/19/19         success
Windows DLL configure/build/19/19   success
```

Windows logs must show editor source compilation, `extractpdf_test_pdf_annotation_mutation.exe`, and `extractpdf.pdf_annotation_mutation` passing through the DLL build.

- [ ] **Step 6: Final exact-head review against every locked boundary**

Review:

```text
source immutability
begin source-lifetime independence
encrypted/signed fail-closed
JavaScript disabled
discovery semantic reuse + malformed atomicity
canonical refs + wrong-session + tombstone + no slot reuse
full uint32 flags
Contents absent/empty/counting/UTF-8/ownership
create type matrix
zero-field validation ordering
update/create rollback
appearance update before operation completion
snapshot determinism/non-consumption/output independence
no MuPDF/PDF identity in public ABI
```

Any Critical/Important fix creates a new exact SHA and requires Linux + same-head full-ci proof again.

- [ ] **Step 7: Mark implementation/evidence complete, integration pending**

Only after Step 1-6 pass, mark PR ready and update #37/roadmap to say implementation/evidence complete; integration pending. Do not merge in this task.

---

### Task 9: Explicit integration gate

**Files:**
- Bookkeeping only after successful integration: PR, issue #37, roadmap #2.

**Interfaces:**
- Consumes: proven exact-head full-ci from Task 8.
- Produces: integrated master proof and closes #37.

A plan executor must stop before this task until the user gives separate explicit integration authorization.

- [ ] **Step 1: Re-fetch merge state immediately before merge**

Require:

```text
PR open + ready
head SHA exactly equals proven full-ci SHA
base master expected descendant
mergeable true
no unresolved review thread
no new review/comment blocker
same-head full-ci success
```

- [ ] **Step 2: Merge with `expected_head_sha` and merge method `merge`**

GitHub must reject if head moved.

- [ ] **Step 3: Verify integrated master push workflow by merge SHA**

Do not close #37 from PR CI. Find the `push` run whose `head_sha` equals merge commit and require:

```text
Linux static + all CTests       success
Linux ASan/UBSan + all CTests  success
macOS                           success
Windows DLL                     success
```

Inspect Windows integrated logs to confirm mutation CTest runs.

- [ ] **Step 4: Close bookkeeping after integrated GREEN only**

Close #37 completed and update roadmap #2:

```text
[x] Annotation mutation editor — #37 / PR #... — integrated
```

Record final feature SHA, merge SHA, same-head full-ci workflow, and integrated-master push workflow. Leave subtype-specific geometry and Forms/widgets separate.

---

## Execution Checkpoints

```text
Task 1  strict compile RED + remote RED evidence
   ↓
Task 2  shared semantics refactor; 18/18 old-test gate while new target remains RED
   ↓
Task 3  public ABI + isolated lifecycle + fail-closed + non-consuming snapshot
   ↓
Task 4  discovery + canonical refs + live getters
   ↓
Task 5  create Text/FreeText/Square/Circle + create rollback
   ↓
Task 6  update/delete + tombstone + full-u32 + Contents + rollback
   ↓
Task 7  source/snapshot/JavaScript/public-output semantic proof
   ↓
Task 8  19/19 + sanitizers + same-SHA Linux/macOS/Windows full-ci + review
   ↓
STOP: explicit integration authorization
   ↓
Task 9  expected-head merge + integrated master proof + close #37
```

The completion boundary is not “CRUD works.” It is: public reparse proves edited outputs, every identity/rollback/fail-closed/ownership contract is deterministic, and the exact feature head passes all three operating-system jobs before integration.