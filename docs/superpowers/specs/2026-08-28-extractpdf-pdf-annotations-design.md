# ExtractPDF PDF Page Annotation Enumeration V1 Design

## Status

Approved Phase 5 Interactive PDF design for issue #35 under roadmap #2.

Base: integrated outline master exact SHA `a83639752e629225b34b052d537a5d2e61220711`, verified by push workflow #175 (`33176336925`) across Linux static + ASan/UBSan, macOS, and Windows DLL.

This slice is read-only ordinary-annotation enumeration. Annotation mutation and forms/widgets remain separate architectures.

## Goals

- Reuse the existing opaque `extractpdf_page`.
- Return an ExtractPDF-owned immutable annotation snapshot with no MuPDF/PDF pointer in the public ABI.
- Preserve `/Annots` relative order after deterministic filtering.
- Expose annotation bounds in the existing Fitz page-space coordinate model.
- Keep snapshot data valid after the source page is dropped and document is closed.
- Separate tolerant collection handling from strict materialization of annotations that survive filtering.
- Publish atomically: complete snapshot on success, no snapshot on failure.
- Keep public indices snapshot-local and non-persistent.

## Non-goals

V1 does not mutate annotations, expose PDF object numbers/generations or persistent IDs, enumerate links/popups/widgets as ordinary annotations, render appearance streams, expose subtype-specific geometry, model replies, flatten annotations, save mutations, or define cross-snapshot identity.

## Public ABI

```c
typedef struct extractpdf_annotation_page extractpdf_annotation_page;

typedef enum extractpdf_annotation_type {
    EXTRACTPDF_ANNOTATION_UNKNOWN = 0,
    EXTRACTPDF_ANNOTATION_TEXT = 1,
    EXTRACTPDF_ANNOTATION_FREE_TEXT = 2,
    EXTRACTPDF_ANNOTATION_LINE = 3,
    EXTRACTPDF_ANNOTATION_SQUARE = 4,
    EXTRACTPDF_ANNOTATION_CIRCLE = 5,
    EXTRACTPDF_ANNOTATION_POLYGON = 6,
    EXTRACTPDF_ANNOTATION_POLY_LINE = 7,
    EXTRACTPDF_ANNOTATION_HIGHLIGHT = 8,
    EXTRACTPDF_ANNOTATION_UNDERLINE = 9,
    EXTRACTPDF_ANNOTATION_SQUIGGLY = 10,
    EXTRACTPDF_ANNOTATION_STRIKE_OUT = 11,
    EXTRACTPDF_ANNOTATION_REDACT = 12,
    EXTRACTPDF_ANNOTATION_STAMP = 13,
    EXTRACTPDF_ANNOTATION_CARET = 14,
    EXTRACTPDF_ANNOTATION_INK = 15,
    EXTRACTPDF_ANNOTATION_FILE_ATTACHMENT = 16,
    EXTRACTPDF_ANNOTATION_SOUND = 17,
    EXTRACTPDF_ANNOTATION_MOVIE = 18,
    EXTRACTPDF_ANNOTATION_RICH_MEDIA = 19,
    EXTRACTPDF_ANNOTATION_SCREEN = 20,
    EXTRACTPDF_ANNOTATION_PRINTER_MARK = 21,
    EXTRACTPDF_ANNOTATION_TRAP_NET = 22,
    EXTRACTPDF_ANNOTATION_WATERMARK = 23,
    EXTRACTPDF_ANNOTATION_3D = 24,
    EXTRACTPDF_ANNOTATION_PROJECTION = 25
} extractpdf_annotation_type;

typedef struct extractpdf_annotation_info {
    size_t struct_size;
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
} extractpdf_annotation_info;

EXTRACTPDF_API extractpdf_status extractpdf_extract_annotations(
    extractpdf_page *page,
    extractpdf_annotation_page **out_annotations);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_count(
    const extractpdf_annotation_page *annotations,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_get_info(
    const extractpdf_annotation_page *annotations,
    size_t index,
    extractpdf_annotation_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_contents(
    const extractpdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API void extractpdf_drop_annotation_page(
    extractpdf_annotation_page *annotations);
```

`extractpdf_annotation_type` is an ExtractPDF enum, not a cast of MuPDF's enum. Its numeric values belong to the public ABI.

## Minimal V1 Item

Each surviving annotation exposes only:

```text
type      stable ExtractPDF type; UNKNOWN for missing/non-name/unrecognized subtype
bounds    /Rect converted into Fitz page space
flags     raw PDF /F bitmask as uint32_t
contents  optional decoded PDF text copied into snapshot-owned UTF-8 storage
```

Author/name/date/color/opacity/intent/reply relations/appearance state and subtype-specific geometry remain deferred.

## Malformed vs Tolerance

The `/Annots` collection is intentionally tolerant, following the compatibility direction of pinned MuPDF 1.28.2:

- missing `/Annots` -> empty;
- non-array `/Annots` -> empty;
- empty array -> empty;
- non-dictionary member -> ignore;
- `/Subtype /Link` -> ignore; links already have the Phase 3 link API;
- `/Subtype /Popup` -> ignore;
- `/Subtype /Widget` -> ignore; forms/widgets are a later Phase 5 slice;
- any other dictionary survives in original relative order;
- missing, non-name, or unrecognized `/Subtype` -> `UNKNOWN`.

There is no outline-style structural preflight. This is an enumerator, not a general PDF validator.

Once a dictionary survives filtering, materialization is strict:

- `/Rect` must exist and be an array of exactly four finite numeric values;
- missing `/F` -> `0`; present `/F` must be an integer in `[0, UINT32_MAX]`;
- missing `/Contents` -> absent contents;
- present `/Contents` must be a PDF string;
- present-empty contents remains distinct from absent contents;
- malformed surviving data -> `EXTRACTPDF_ERROR_FORMAT` for the whole extraction.

The implementation must never publish a guessed rectangle/default for malformed surviving data or a prefix containing only earlier valid annotations.

## Geometry

PDF annotation `/Rect` is in PDF page user space. ExtractPDF maps it to the same Fitz page-space model used by links/text/images/page bounds.

Use MuPDF's `pdf_page_transform()` CTM **directly** on the normalized PDF rectangle. This matches MuPDF 1.28.2's own `pdf_bound_annot()` and link-loading paths: the returned page CTM maps PDF page user-space coordinates to Fitz page space. Do not invert that CTM.

After transformation, normalize the output endpoints so `x0 <= x1` and `y0 <= y1`.

## Empty Snapshot

Every valid zero-result case is successful:

```text
extractpdf_extract_annotations() -> EXTRACTPDF_OK
*out_annotations                 -> non-NULL snapshot
extractpdf_annotation_count()    -> 0
```

This includes no `/Annots`, non-array `/Annots`, an empty array, or an array containing only filtered entries.

## Order

Public indices are exactly the original `/Annots` relative order among surviving ordinary annotations.

```text
/Annots = [ Text-A, 17, Link, Unknown-X, Widget, Popup, Highlight-B ]
public   = [ Text-A, Unknown-X, Highlight-B ]
index    = [   0,        1,           2     ]
```

No sorting by subtype, rectangle, PDF object number, appearance, or any other property is allowed.

## Identity

Annotation indices are snapshot-local coordinates only. They are not PDF object identities, object-number/generation pairs, `/NM` values, persistent IDs, cross-snapshot identities, mutation selectors, or future update/delete handles.

Two snapshots of the same unchanged page may contain equal values/order, but callers get no identity guarantee between them. Mutation requires a separate design for stable selectors, transactions/rollback, save/rewrite behavior, and interaction with pre-existing snapshots.

## Lifetime and Ownership

The published snapshot contains only ExtractPDF-owned item records and copied strings. It retains no `pdf_annot *`, `pdf_obj *`, `pdf_page *`, `fz_page *`, `fz_document *`, `extractpdf_page *`, or `extractpdf_document *`.

After successful extraction callers may immediately drop the source page and close the document. Snapshot accessors remain valid until `extractpdf_drop_annotation_page()`; dropping NULL is a no-op.

## Error Atomicity

Extraction begins with:

```text
out_annotations == NULL -> ARGUMENT
otherwise                -> *out_annotations = NULL before later validation/work
```

Traversal, filtering, validation, geometry conversion, allocation, and string copies are private until all survivors are materialized. Only then is the snapshot published.

On failure:

- output remains NULL;
- all temporary item/string storage is freed;
- no partial count/item prefix is observable;
- allocation/size overflow -> `NOMEM`;
- malformed surviving item -> `FORMAT`;
- non-PDF page -> `UNSUPPORTED`;
- MuPDF exceptions -> existing status mapper.

Accessors reset caller-visible outputs to neutral values before later handle/index validation wherever possible. `extractpdf_annotation_get_info()` validates `struct_size` first, then resets known fields before handle/index validation.

## Read-only Integration Boundary

Enumeration uses the already-loaded page and performs a raw read-only `/Annots` walk. It does not itself call `pdf_sync_annots()`, `pdf_load_annots()`, annotation update/resynthesis APIs, or mutation APIs. The existing page-loading behavior is unchanged by this slice.

```text
extractpdf_page
    -> pdf_page_from_fz_page()
    -> raw page /Annots array
    -> tolerant filter
    -> strict survivor Rect/F/Contents materialization
    -> pdf_page_transform() CTM -> Fitz bounds
    -> deep-copy snapshot
    -> atomic publication
```

## Deterministic Fixtures

`annotations-mixed.pdf` interleaves Text, scalar `17`, Link, unknown subtype, Widget, Popup, Highlight. Expected public order is Text / UNKNOWN / Highlight with distinct bounds, flags, and contents.

`annotations-nonarray.pdf` uses `/Annots 17` and must return a non-NULL count-0 snapshot.

`annotations-filtered-only.pdf` contains only scalar/Link/Widget/Popup and must return a non-NULL count-0 snapshot.

`annotations-late-malformed.pdf` has one valid survivor followed by a survivor with `/Contents 123`; extraction must return `FORMAT + NULL` on every call, proving late failure atomicity.

An existing PDF with no annotations proves the missing-key empty case.

## TDD / Acceptance Boundary

Branch base:

```text
a83639752e629225b34b052d537a5d2e61220711
  -> feat/pdf-annotations
```

RED contains deterministic fixtures/test/CMake registration and no annotation production declarations or implementation. Valid RED means all old targets build and only the new annotation target fails on absent ABI.

GREEN production scope is limited to:

```text
include/extractpdf/extractpdf.h
src/pdf_annotations.c
CMakeLists.txt
```

Final exact head must pass Linux strict static/all CTests, Linux ASan/UBSan/all CTests, macOS configure/build/test, and Windows DLL configure/build/test with `extractpdf.pdf_annotations` executed on Windows.
