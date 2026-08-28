# ExtractPDF Phase 5: Typed PDF Document Metadata Design

Date: 2026-08-28
Issue: #31
Umbrella: #2
Base: integrated Phase 4 `master` at `86cd429b03ab06396b9a511477b806517fe63130`

## Summary

Phase 5 starts with one deliberately small PDF document-root read surface: typed access to the eight standard PDF Info dictionary fields. The API reuses the existing opaque `extractpdf_document`, returns ExtractPDF-owned UTF-8 copies, and keeps all MuPDF PDF object types private.

This slice does not introduce a second PDF document handle, a metadata snapshot lifecycle, generic metadata string keys, XMP merging, metadata mutation, or save/rewrite behavior.

## Goals

- Establish the first PDF-specific document-root read API on the existing `extractpdf_document` handle.
- Expose a stable typed ABI rather than MuPDF's metadata key namespace.
- Preserve the semantic difference between an absent Info value and a present empty text string.
- Return copied UTF-8 that is independent of the source document lifetime.
- Define deterministic error/reset behavior consistent with existing ExtractPDF APIs.
- Keep the implementation small enough to serve as a pattern for later PDF-root read features without prematurely creating a shared subsystem.

## Non-goals

V1 does not expose or merge:

- XMP `/Metadata` streams;
- custom PDF Info keys;
- PDF version/format;
- encryption state or permissions;
- page labels;
- outlines/bookmarks;
- annotations/widgets/forms;
- metadata mutation;
- date normalization or parsed timestamps;
- a generic MuPDF metadata-key API;
- a new public `extractpdf_pdf_document` handle.

## Public ABI

Add a typed field enum:

```c
typedef enum extractpdf_metadata_field {
    EXTRACTPDF_METADATA_TITLE = 1,
    EXTRACTPDF_METADATA_AUTHOR = 2,
    EXTRACTPDF_METADATA_SUBJECT = 3,
    EXTRACTPDF_METADATA_KEYWORDS = 4,
    EXTRACTPDF_METADATA_CREATOR = 5,
    EXTRACTPDF_METADATA_PRODUCER = 6,
    EXTRACTPDF_METADATA_CREATION_DATE = 7,
    EXTRACTPDF_METADATA_MODIFICATION_DATE = 8
} extractpdf_metadata_field;
```

Add one getter:

```c
EXTRACTPDF_API extractpdf_status extractpdf_document_metadata(
    extractpdf_document *document,
    extractpdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);
```

The enum values are explicit and stable. Unknown values are invalid arguments.

The public ABI does not expose strings such as `"info:Title"`, `pdf_obj`, `pdf_document`, or any other MuPDF type.

## Metadata scope

`extractpdf_document_metadata` means **standard PDF Info dictionary metadata only**.

The supported mappings are:

| ExtractPDF field | PDF Info key |
| --- | --- |
| `EXTRACTPDF_METADATA_TITLE` | `/Title` |
| `EXTRACTPDF_METADATA_AUTHOR` | `/Author` |
| `EXTRACTPDF_METADATA_SUBJECT` | `/Subject` |
| `EXTRACTPDF_METADATA_KEYWORDS` | `/Keywords` |
| `EXTRACTPDF_METADATA_CREATOR` | `/Creator` |
| `EXTRACTPDF_METADATA_PRODUCER` | `/Producer` |
| `EXTRACTPDF_METADATA_CREATION_DATE` | `/CreationDate` |
| `EXTRACTPDF_METADATA_MODIFICATION_DATE` | `/ModDate` |

XMP is deliberately not consulted. If Info and XMP disagree, V1 returns the Info value only. This avoids introducing source-precedence/conflict semantics before XMP has its own explicit design.

## Why not generic `fz_lookup_metadata`

Pinned MuPDF 1.28.2 exposes generic metadata keys such as `FZ_META_INFO_TITLE`. However, its PDF lookup path converts an Info value with `pdf_to_text_string()` and then returns `-1` when the resulting string length is zero. Therefore generic lookup collapses these two states:

```text
missing value
present empty text string
```

ExtractPDF V1 intentionally distinguishes them, so the implementation must read the PDF Info dictionary directly rather than making `fz_lookup_metadata()` authoritative.

The public API remains independent of this MuPDF implementation detail.

## PDF-only enforcement

The existing `extractpdf_document` remains the only public document handle:

```c
struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};
```

The implementation privately down-casts with MuPDF's PDF-specific API:

```text
extractpdf_document
    ↓
pdf_specifics(document->ctx, document->doc)
    ├─ NULL -> EXTRACTPDF_ERROR_UNSUPPORTED
    └─ borrowed pdf_document *
```

No extra reference is retained and no second document lifetime is introduced.

A successfully opened non-PDF document is therefore valid for generic ExtractPDF operations but returns `EXTRACTPDF_ERROR_UNSUPPORTED` from this PDF-only metadata getter.

## Value semantics

The getter distinguishes the structure of `/Info` before inspecting the selected field.

### Missing Info dictionary

If the trailer has no `/Info` entry, return:

```text
EXTRACTPDF_OK
*out_utf8 = NULL
*out_size = 0
```

Absence is data, not an error.

### Malformed Info object

If the trailer has a present `/Info` entry but it does not resolve to a PDF dictionary, return:

```text
EXTRACTPDF_ERROR_FORMAT
*out_utf8 = NULL
*out_size = 0
```

A malformed document-root metadata object is not silently reclassified as absent.

### Missing selected key

If `/Info` is a valid dictionary but the selected key is absent, return:

```text
EXTRACTPDF_OK
*out_utf8 = NULL
*out_size = 0
```

### Present empty PDF text string

Return:

```text
EXTRACTPDF_OK
*out_utf8 = allocated ""
*out_size = 0
```

A non-NULL allocated empty string distinguishes this from absence. The caller releases it with `extractpdf_free()`.

### Present non-empty PDF text string

Decode through MuPDF's PDF text-string conversion to UTF-8, copy into ExtractPDF-owned memory, append a NUL terminator, and return the byte size excluding that terminator.

### Present value with wrong PDF object type

If the selected standard Info key exists but is not a PDF string, including a PDF null or numeric value, return:

```text
EXTRACTPDF_ERROR_FORMAT
*out_utf8 = NULL
*out_size = 0
```

A malformed present value is not silently reclassified as missing.

## Date policy

`CreationDate` and `ModDate` use the same text contract as the other Info fields.

V1 returns the decoded PDF text **without normalization or parsing**. For example:

```text
D:20260828123456+09'00'
```

is returned exactly as text.

No ISO-8601 conversion, timezone normalization, epoch timestamp, component completion, or validation is promised. A future typed-date helper can be designed separately without changing the raw metadata getter.

## Ownership and lifetime

Returned strings follow the existing `extractpdf_extract_text()` ownership convention:

- allocated with ExtractPDF-owned `malloc` memory;
- NUL-terminated;
- `out_size` excludes the NUL;
- caller releases with `extractpdf_free()`;
- returned memory does not retain a document/context/MuPDF object;
- after success the caller may immediately call `extractpdf_close(document)` and continue using the returned string.

Missing metadata is the only successful case that returns `NULL` rather than allocated memory.

## Validation and output-reset contract

At function entry:

```text
if out_utf8 != NULL -> *out_utf8 = NULL
if out_size != NULL -> *out_size = 0
```

Then validate:

- `document == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`;
- `out_utf8 == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`;
- `out_size == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`;
- field outside the eight defined enum values -> `EXTRACTPDF_ERROR_ARGUMENT`.

All failures leave any supplied output slots reset.

## Error mapping and atomicity

After validation:

- non-PDF document -> `EXTRACTPDF_ERROR_UNSUPPORTED`;
- present `/Info` that is not a dictionary -> `EXTRACTPDF_ERROR_FORMAT`;
- malformed non-string selected Info value -> `EXTRACTPDF_ERROR_FORMAT`;
- ExtractPDF allocation failure -> `EXTRACTPDF_ERROR_NOMEM`;
- MuPDF exceptions -> existing `extractpdf_status_from_mupdf()` mapping;
- otherwise -> `EXTRACTPDF_OK`.

The function allocates/copies before publishing output pointers. There is no partial public result. On failure no caller-owned cleanup is required.

## Private implementation boundary

Production changes are intentionally limited to:

```text
include/extractpdf/extractpdf.h
    + extractpdf_metadata_field
    + extractpdf_document_metadata(...)

src/pdf_metadata.c
    + field enum -> fixed PDF Info key mapping
    + private PDF down-cast
    + trailer /Info lookup
    + /Info dictionary validation
    + selected-value type validation
    + PDF text-string UTF-8 decode
    + ExtractPDF-owned copy

CMakeLists.txt
    + src/pdf_metadata.c
```

`src/pdf_metadata.c` may include `src/pdf_internal.h`, which is already the private `<mupdf/pdf.h>` boundary established by Phase 4.

Do not modify:

- `src/document.c`;
- `src/internal.h` document layout;
- Phase 4 composition/output implementation;
- Page/Render/Text/Search/Image/Links implementation.

Do not extract a generic `extractpdf_require_pdf_document()` helper in this first read slice. When a later PDF-root feature such as outline or annotations demonstrates the same repeated private gate/error policy, helper extraction can be justified by real duplication.

## Phase 5 document-root architecture

Phase 5 keeps one public document handle and layers PDF-only read surfaces on top:

```text
extractpdf_document
    │
    ├─ generic document/page capabilities
    │
    └─ PDF-only read surfaces
          ├─ metadata      -> copied UTF-8 value
          ├─ outline       -> later immutable tree snapshot
          ├─ annotations   -> later immutable page snapshot
          └─ forms/widgets -> later dedicated contract
```

Simple scalar/string metadata does not justify a metadata handle. Tree/collection features can introduce snapshots when their structure and lifetime require them.

Read and mutation remain separate architectural stages. Metadata read, outline read, and annotation enumeration can establish immutable read contracts. Annotation create/update/delete, form-value mutation, and other dirty-document operations require a separate design covering MuPDF journal/dirty state and save/rewrite interaction.

## Deterministic fixture

Add one hand-authored fixture:

```text
tests/fixtures/metadata-info.pdf
```

Its `/Info` dictionary contains deterministic values:

```text
Title        = "Phase 5 Café"      (encoded as a valid PDF text string exercising Unicode decode)
Author       = "ExtractPDF Test"
Subject      = ""                  (present empty PDF string)
Keywords     = absent
Creator      = "Metadata Fixture"
Producer     = 42                  (deliberately malformed non-string)
CreationDate = "D:20260828123456+09'00'"
ModDate      = "D:20260828124500+09'00'"
```

The PDF content itself can be minimal because this test targets document-root Info semantics rather than page content.

Reuse the existing non-PDF fixture already used by Phase 4 for the unsupported-format case; do not add another non-PDF fixture.

## Deterministic test contract

Create:

```text
tests/test_pdf_metadata.c
CTest: extractpdf.pdf_metadata
```

The test proves:

1. Title decodes to exact UTF-8 `Phase 5 Café` and correct byte size.
2. Ordinary Author/Creator values are returned exactly.
3. Present-empty Subject returns `OK`, non-NULL allocated `""`, size 0.
4. Absent Keywords returns `OK`, NULL, size 0.
5. CreationDate and ModDate return the exact raw PDF date strings.
6. Malformed numeric Producer returns `EXTRACTPDF_ERROR_FORMAT` and reset outputs.
7. Invalid enum and NULL arguments return `EXTRACTPDF_ERROR_ARGUMENT` with reset semantics where output slots exist.
8. Existing non-PDF fixture returns `EXTRACTPDF_ERROR_UNSUPPORTED` with reset outputs.
9. Retrieve Title, close the source document, then confirm the copied Title remains valid before `extractpdf_free()`.

A malformed `/Info`-object fixture is not required for V1 because the malformed-object branch is a direct structural validation requirement and introducing a second malformed PDF fixture would enlarge the slice without changing the public contract. The implementation must nevertheless map a present non-dictionary `/Info` to `EXTRACTPDF_ERROR_FORMAT`.

The Windows shared-library test list must include the new metadata test target so CTest exercises the DLL export rather than only building the library.

## TDD sequence

This is new behavior and requires a real RED.

### RED

Only add:

```text
tests/fixtures/metadata-info.pdf
tests/test_pdf_metadata.c
tests/CMakeLists.txt
```

No public declaration or production implementation may exist in the RED commit.

Exact-head PR CI must demonstrate:

- pinned MuPDF install succeeds;
- configure succeeds;
- ExtractPDF library builds;
- every pre-existing test target builds/links;
- only the new metadata contract target fails because `extractpdf_metadata_field` and/or `extractpdf_document_metadata` are absent.

### GREEN

Then add only the approved public enum/function, `src/pdf_metadata.c`, and root CMake registration.

On the exact GREEN head require:

- Linux strict static configure/build;
- all normal CTests;
- Linux ASan/UBSan configure/build;
- all sanitizer CTests.

### Architecture/platform checkpoint

Because this is the first Phase 5 PDF document-root ABI and a new Windows DLL export, apply the existing `full-ci` PR label on the exact same GREEN head and require:

- Linux static + sanitizers;
- macOS configure/build/test;
- Windows DLL configure/build/test.

No code change is allowed between the Linux GREEN and this full-ci checkpoint.

## Acceptance criteria

The slice is implementation-complete when all of the following hold:

- typed eight-field ABI is the only new public metadata surface;
- PDF-only behavior is enforced without a new public document handle;
- empty and absent values are observably distinct;
- malformed `/Info` structure and malformed selected values map to `EXTRACTPDF_ERROR_FORMAT`;
- date values remain raw decoded text;
- returned UTF-8 is independent and freed through `extractpdf_free()`;
- no XMP/custom-key/format/encryption behavior is accidentally exposed;
- deterministic RED proves the missing API boundary;
- exact GREEN Linux static + sanitizer suites pass;
- same-head Linux/macOS/Windows full-ci passes;
- no unrelated Phase 4 or Page/Content implementation is changed.

The child issue remains open for PR integration bookkeeping until explicit integration. Umbrella #2 remains open for the rest of Phase 5 and Phase 6.
