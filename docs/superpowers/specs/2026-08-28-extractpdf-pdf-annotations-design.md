# ExtractPDF PDF Page Annotation Enumeration V1 Design

## Status

Approved Phase 5 Interactive PDF design for issue #35 under roadmap #2.

Base: integrated outline master exact SHA `a83639752e629225b34b052d537a5d2e61220711`, verified by push workflow #175 (`33176336925`) across Linux static + ASan/UBSan, macOS, and Windows DLL.

This slice is read-only annotation enumeration only. Annotation mutation and forms/widgets remain separate architectures.

## Goals

- Reuse the existing opaque `extractpdf_page`.
- Expose an ExtractPDF-owned immutable page annotation snapshot with no MuPDF pointer or PDF object crossing the C ABI.
- Preserve page annotation array order after deterministic filtering.
- Use the existing Fitz page-space geometry contract for annotation bounds.
- Keep snapshot data valid after the source page is dropped and the source document is closed.
- Define tolerant collection semantics separately from strict per-item materialization semantics.
- Make error publication atomic: success publishes one complete snapshot; failure publishes nothing.
- Keep enumeration indices snapshot-local and explicitly non-persistent.

## Non-goals

V1 does not mutate annotations, expose PDF object numbers/generations, provide persistent annotation IDs, enumerate links, enumerate popup objects, enumerate widgets/forms, render annotation appearance streams, expose annotation-specific vertex/quad/ink geometry, model reply threads, flatten annotations, save mutations, execute JavaScript, or define cross-snapshot identity.

## PDF-only Boundary

```text
PDF page      -> annotation snapshot API
non-PDF page  -> EXTRACTPDF_ERROR_UNSUPPORTED
```

The public surface remains rooted in the existing `extractpdf_page`; no second public PDF page handle is introduced.

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

`extractpdf_annotation_type` is an ExtractPDF enum, not a cast of MuPDF's `pdf_annot_type`. Public numeric values are stable even if MuPDF changes its enum.

## Minimal V1 Data Model

Each surviving ordinary annotation contributes:

```text
type      stable ExtractPDF annotation type, UNKNOWN when absent/unrecognized
bounds    annotation /Rect mapped into Fitz page space
flags     raw PDF annotation /F bitmask as uint32_t
contents  optional decoded PDF text string copied into snapshot storage
```

V1 intentionally stops here. Author, name, modification date, color, opacity, intent, reply/thread relations, appearance state, and subtype-specific geometry are deferred until there is a concrete consumer.

## `struct_size` Compatibility

Callers initialize:

```c
extractpdf_annotation_info info = {0};
info.struct_size = sizeof(info);
```

V1 requires the supplied struct to cover through `flags`. Larger structs are accepted; V1 writes only known fields.

After `struct_size` validation and before later handle/index validation, known output fields reset to:

```text
type   = UNKNOWN
bounds = {0,0,0,0}
flags  = 0
```

## Collection Tolerance Contract

Annotation enumeration deliberately does **not** copy the outline slice's strict structural preflight model.

Pinned MuPDF 1.28.2 semantics establish the compatibility direction: `pdf_array_len()` returns zero for a non-array, and `pdf_sync_annots()` scans `/Annots` in array order, ignores non-dictionary members, excludes `Link` and `Popup`, and routes `Widget` separately. ExtractPDF locks the same collection behavior while implementing it through a read-only raw `/Annots` walk so enumeration itself does not invoke annotation synchronization, appearance resynthesis, or document mutation.

Collection rules:

- no `/Annots` -> empty;
- non-array `/Annots` -> empty;
- empty array -> empty;
- non-dictionary array entry -> ignore;
- `/Subtype /Link` -> ignore; links remain in the Phase 3 link API;
- `/Subtype /Popup` -> ignore;
- `/Subtype /Widget` -> ignore; widgets/forms remain a separate Phase 5 slice;
- every other dictionary entry survives in its original relative array order;
- missing, non-name, or unrecognized `/Subtype` -> public `UNKNOWN`, not a collection error.

This is a tolerant enumerator, not a general PDF validator.

## Strict Surviving-item Materialization

Tolerance of the collection container does not mean silently inventing required public item data.

For each surviving ordinary annotation:

- `/Rect` must be an array of exactly four numeric values; otherwise extraction returns `EXTRACTPDF_ERROR_FORMAT`;
- all four rectangle values must be finite; otherwise `FORMAT`;
- rectangle endpoints are normalized after mapping to Fitz page space so `x0 <= x1` and `y0 <= y1`;
- missing `/F` means flags `0`;
- present `/F` must be an integer representable as `uint32_t`; otherwise `FORMAT`;
- missing `/Contents` means absent contents (`OK + NULL + 0` from the accessor);
- present `/Contents` must be a PDF string; otherwise `FORMAT`;
- present empty contents remains distinct from absent contents;
- PDF text strings are decoded through MuPDF's PDF text-string decoding, then copied into snapshot-owned UTF-8 storage.

A malformed surviving item fails the **whole extraction**. ExtractPDF never returns a snapshot containing a guessed/defaulted rectangle or a partial prefix of earlier valid items.

## Geometry

The PDF annotation `/Rect` is defined in PDF page user space. ExtractPDF converts it to the same Fitz page-space coordinate model already used by page bounds, text/search, images, and links.

Implementation uses the loaded `pdf_page`'s page object plus MuPDF's page transform and inverse transform. No raw PDF coordinate system appears in the public ABI.

## Empty Snapshot

All valid zero-result cases return:

```text
extractpdf_extract_annotations() -> EXTRACTPDF_OK
*out_annotations                 -> non-NULL snapshot
extractpdf_annotation_count()    -> 0
```

This includes:

- no `/Annots`;
- non-array `/Annots`;
- empty array;
- array containing only non-dictionaries, Link, Popup, and Widget entries.

Success always returns a valid snapshot handle, including count zero.

## Order

Public indices preserve original `/Annots` array order among surviving ordinary annotations.

```text
/Annots = [ Text-A, 17, Link, Unknown-X, Widget, Popup, Highlight-B ]
public   = [ Text-A, Unknown-X, Highlight-B ]
index    = [   0,        1,           2     ]
```

No sorting by type, bounds, object number, appearance, or any other property is permitted.

## Identity

Annotation indices are snapshot-local coordinates only.

They are not:

- PDF object numbers;
- PDF object-number/generation pairs;
- NM/name values;
- persistent annotation IDs;
- cross-snapshot identities;
- mutation selectors;
- future update/delete handles.

V1 exposes no PDF object identity in the public ABI. Two snapshots of the same unchanged page may happen to contain the same values in the same order, but the API makes no identity promise across those snapshots.

Future create/update/delete gets a separate design covering stable selectors, transactions/rollback, mutation ordering, save/rewrite integration, and interaction with pre-existing snapshots.

## Snapshot Lifetime and Ownership

Conceptually:

```text
extractpdf_annotation_page
  ├── items[]
  └── strings[]
```

The published snapshot retains no `pdf_annot *`, `pdf_obj *`, `pdf_page *`, `fz_page *`, `fz_document *`, `extractpdf_page *`, or `extractpdf_document *`.

After successful extraction, callers may immediately drop the source page and close the source document; annotation info and contents remain valid until `extractpdf_drop_annotation_page()`.

`extractpdf_drop_annotation_page(NULL)` is a safe no-op.

## Error Atomicity

`extractpdf_extract_annotations()` follows an atomic publication contract:

```text
out_annotations == NULL -> ARGUMENT
otherwise first action  -> *out_annotations = NULL
```

All PDF traversal, filtering, validation, geometry conversion, allocation, and text copying occur into private temporary state. `*out_annotations` is assigned only after the complete snapshot succeeds.

On any failure:

- `*out_annotations == NULL`;
- all temporary item/string storage is released;
- no partial count or item prefix is observable;
- no MuPDF/PDF pointer escapes;
- allocation or size overflow -> `EXTRACTPDF_ERROR_NOMEM`;
- malformed surviving item -> `EXTRACTPDF_ERROR_FORMAT`;
- non-PDF page -> `EXTRACTPDF_ERROR_UNSUPPORTED`;
- MuPDF exceptions -> existing `extractpdf_status_from_mupdf()` mapping.

Accessors reset caller-visible outputs to neutral values before later handle/index/type validation wherever an output pointer is available.

## Read-only MuPDF Integration Boundary

The implementation must not call `pdf_sync_annots()`, `pdf_load_annots()`, annotation update/resynthesis APIs, or mutation APIs merely to enumerate annotations.

Preferred internal flow:

```text
extractpdf_page
    ↓
pdf_page_from_fz_page()
    ↓
loaded pdf_page->obj
    ↓
read-only /Annots array walk
    ↓
filter non-dict / Link / Popup / Widget
    ↓
strictly materialize survivor Rect/F/Contents
    ↓
PDF-space Rect -> Fitz page-space bounds
    ↓
copy immutable item/string snapshot
    ↓
publish atomically
```

## Deterministic Test Fixtures

### `annotations-mixed.pdf`

One page with this logical `/Annots` order:

```text
Text-A
integer scalar 17
Link
Unknown-X
Widget
Popup
Highlight-B
```

Expected public snapshot:

```text
0 TEXT       contents="alpha"
1 UNKNOWN    contents="unknown"
2 HIGHLIGHT  contents="bravo"
```

The fixture uses intentionally distinct rectangles/flags/contents so exact order can be asserted without object identity.

### `annotations-nonarray.pdf`

`/Annots 17`. Expected: `OK + non-NULL count-0 snapshot`.

### `annotations-filtered-only.pdf`

Contains only a scalar, Link, Popup, and Widget. Expected: `OK + non-NULL count-0 snapshot`.

### Existing no-annotation PDF

Reuse an existing valid PDF with no `/Annots` to prove the missing-key empty case.

### `annotations-late-malformed.pdf`

First survivor is valid. Second survivor has present non-string `/Contents 123`.

Expected: `EXTRACTPDF_ERROR_FORMAT`, output snapshot NULL. Repeat the call on the same page to prove deterministic failure and no hidden partial publication.

## Strict TDD Boundary

Branch starts from integrated master exact SHA:

```text
a83639752e629225b34b052d537a5d2e61220711
  ↓
feat/pdf-annotations
```

RED contains only deterministic fixtures, `tests/test_pdf_annotations.c`, and `tests/CMakeLists.txt` changes. No production annotation type/function declaration or implementation may exist in the RED commit.

Acceptable RED: existing library and all pre-existing targets build; only the new annotation test target fails to compile because the new opaque snapshot/type/info/API are absent. Fixture parse errors, unrelated regressions, crashes, timeouts, or a RED that reaches runtime are invalid.

GREEN production scope:

```text
Modify: include/extractpdf/extractpdf.h
Create: src/pdf_annotations.c
Modify: CMakeLists.txt
```

Avoid unrelated changes to existing page, links, outline, metadata, composition, rendering, text, image, or output implementation.

Exact GREEN head must pass:

```text
Linux strict static + all CTests       ✅
Linux ASan/UBSan + all CTests          ✅
same-head macOS configure/build/test   ✅
same-head Windows DLL build/test       ✅
```

Windows must run the new annotation CTest through the shared-library build so the new ABI is proven exported, linkable, and runnable.

## Architecture Summary

```text
PDF-only page annotation enumeration
+ existing extractpdf_page
+ immutable page snapshot
+ tolerant /Annots collection filtering
+ strict surviving-item materialization
+ original relative order
+ UNKNOWN for absent/unrecognized subtype
+ Link/Popup/Widget excluded
+ Fitz page-space bounds
+ uint32 flags
+ snapshot-owned optional UTF-8 contents
+ valid empty snapshot
+ document-independent lifetime
+ snapshot-local indices only
+ atomic publication on success
+ mutation/forms kept separate
```
