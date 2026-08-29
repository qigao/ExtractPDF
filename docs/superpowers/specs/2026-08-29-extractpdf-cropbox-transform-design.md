# ExtractPDF Immutable CropBox Transform V1 Design

Issue: #49  
Parent roadmap: #48 / #2  
Baseline master: `10aace7bae934f48f0fbcdefad5a9bb42518293d`  
Baseline content tree: `ef426ab30f07242806d95da98940021237d6d4f8`

## 1. Purpose

Phase 6 begins with one deliberately narrow transform: change the visible region of selected PDF pages by writing page-local `/CropBox` values while preserving the rest of the PDF graph.

This is not a generic page rewrite API and not physical trimming. The purpose of V1 is to establish one reusable invariant for all later geometry-changing transforms:

> Public page-local geometry after a transform is derived from an explicit page-space mapping, while the underlying interactive/content objects remain structurally intact unless a later API explicitly promises to rewrite them.

Poster split and flatten depend on this contract. They must not invent a second coordinate model.

## 2. Why CropBox first

ExtractPDF already has two different PDF operation families:

1. Phase 4 composition creates new documents by grafting selected pages.
2. Phase 5 editing serializes the complete source PDF, reopens a private copy, mutates the private graph, and publishes an immutable output.

CropBox V1 belongs to the second architectural family, but remains an immutable transform rather than a public edit session.

`pdf_graft_mapped_page()` is not an acceptable implementation substrate for this feature. Page grafting is designed for composition and does not define preservation of the complete document-root interactive graph. CropBox V1 must preserve AcroForm, outline, annotations, links, Widgets, destinations, metadata, page-tree structure, and other unmodified PDF state.

## 3. Public API

Add one request structure and one transform primitive:

```c
typedef struct extractpdf_page_crop {
    size_t struct_size;
    int page_index;
    extractpdf_rect bounds;
} extractpdf_page_crop;

EXTRACTPDF_API extractpdf_status extractpdf_crop_pages(
    extractpdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_output **out_output);
```

### 3.1 Ownership

- `document` remains immutable.
- `crops` is caller-owned and read only for the duration of the call.
- on success, `out_output` receives a new independent `extractpdf_output`.
- the output owns copied serialized bytes and survives source document close.
- on every failure, `*out_output == NULL`.

### 3.2 `struct_size`

For every request element, `struct_size` must be at least:

```c
offsetof(extractpdf_page_crop, bounds) + sizeof(extractpdf_rect)
```

Smaller values are `EXTRACTPDF_ERROR_ARGUMENT`. Larger values are accepted for forward ABI compatibility.

## 4. Coordinate contract

`extractpdf_page_crop.bounds` is expressed in the **current source page's public Fitz page-space**, the same coordinate system currently returned by:

- page bounds;
- structured text/search geometry;
- image occurrence geometry;
- link hotspots and internal destination coordinates;
- annotation bounds;
- form Widget bounds.

Callers never supply raw PDF user-space coordinates.

### 4.1 Source visible rectangle

For each requested page, V1 resolves the page's current effective visible region from the effective `/CropBox`, falling back to effective `/MediaBox` when `/CropBox` is absent.

The public visible region used for request validation is the current Fitz page-space rectangle produced by the same page transform semantics used by the existing public read APIs.

### 4.2 Shrink-only rule

A requested crop rectangle must satisfy all of these in current public page space:

```text
finite(x0, y0, x1, y1)
x0 < x1
y0 < y1
request.x0 >= current.x0
request.y0 >= current.y0
request.x1 <= current.x1
request.y1 <= current.y1
```

V1 does not expand or recover content outside the current effective visible region. Any request outside the current effective crop is `EXTRACTPDF_ERROR_ARGUMENT`.

This means V1 is monotonic within one call and relative to the source document supplied to that call.

### 4.3 Output page space

For requested source rectangle:

```text
[x0, y0, x1, y1]
```

reopening the successful output must expose that page with visible dimensions:

```text
width  = x1 - x0
height = y1 - y0
```

and the new visible page origin is `(0, 0)` in public Fitz page space.

For ordinary page-local geometry, the conceptual public mapping is:

```text
new_x = old_x - x0
new_y = old_y - y0
```

subject to the existing page rotation/UserUnit transform. This equation describes the observable public result; implementation must derive the raw `/CropBox` through the source page transform rather than manually subtracting coordinates from every PDF object.

### 4.4 Rotation and UserUnit

V1 must support valid existing `/Rotate` and `/UserUnit` values.

The implementation converts the requested public rectangle back to PDF user space using the inverse of the **current source page transform**. It must transform all four public rectangle corners, then compute the normalized axis-aligned PDF rectangle from the transformed points before writing `/CropBox`.

This avoids assuming an unrotated page or a particular PDF origin orientation.

V1 does not modify `/Rotate` or `/UserUnit`.

## 5. Strict page-box preflight

Transformation is stricter than tolerant rendering. Before any private-graph write, each requested page must have a structurally valid page-box environment.

The private preflight resolves page-tree inheritance without mutating it and validates:

- page object is a dictionary;
- `/Parent` traversal is finite, acyclic, and bounded to depth 256;
- effective `/MediaBox` exists;
- effective `/CropBox` is the nearest inherited/local `/CropBox`, or `/MediaBox` if no CropBox exists;
- each resolved box is an array of exactly four numeric finite values;
- normalized width and height are positive;
- effective CropBox is contained by effective MediaBox in PDF user space;
- inherited/local `/Rotate`, when present, is an integer multiple of 90;
- inherited/local `/UserUnit`, when present, is finite and strictly positive.

Malformed page-tree or box structure returns `EXTRACTPDF_ERROR_FORMAT`. Structurally valid page-tree depth greater than 256 returns `EXTRACTPDF_ERROR_UNSUPPORTED`.

The strict validator exists to prevent a transform from depending on silent MuPDF box repair/clamping that the caller cannot observe or reproduce.

## 6. Batch semantics

`extractpdf_crop_pages()` is an atomic immutable batch transform.

### 6.1 Validation

Before source serialization or private mutation:

- `document != NULL`;
- `crops != NULL`;
- `crop_count > 0`;
- `out_output != NULL`;
- `crop_count` must be representable by internal allocation arithmetic;
- every request has valid `struct_size`;
- every page index is in range;
- every page index occurs at most once in the batch;
- every rectangle is finite, positive-area, and shrink-only;
- every requested page passes strict page-box preflight;
- encrypted input is rejected;
- already-signed input is rejected.

Duplicate page indices are `EXTRACTPDF_ERROR_ARGUMENT`. V1 does not define last-writer-wins semantics.

### 6.2 All-or-nothing publication

No output is published until every request has been applied to the isolated private graph and final serialization succeeds.

Because the source document is never mutated, journal rollback is not required for V1. If any private write or serialization fails, the private graph is discarded and `*out_output` remains `NULL`.

This is failure atomicity by isolation rather than mutation rollback.

## 7. Full-document isolation flow

The implementation architecture is:

```text
source extractpdf_document
        |
        | strict source preflight for every requested page
        v
serialize complete source PDF using existing deterministic serializer
        |
        v
open serialized bytes in a fresh private MuPDF context
        |
        +-- disable JavaScript before transform work
        |
        +-- re-resolve and validate target pages in the private graph
        |
        +-- raw page-local /CropBox writes only for changed requests
        v
serialize complete private PDF
        |
        v
new immutable extractpdf_output
```

The private re-resolution is mandatory. Source `pdf_obj *` pointers, page objects, or other MuPDF identities must never cross into the private context.

## 8. Write surface

For each changed page, the only semantic PDF write permitted by CropBox V1 is:

```text
page dictionary /CropBox = [llx lly urx ury]
```

The value is page-local even when the source effective CropBox was inherited.

CropBox V1 must not write or normalize any of the following as a side effect:

```text
/MediaBox
/BleedBox
/TrimBox
/ArtBox
/Rotate
/UserUnit
/Contents
/Resources
/Annots
/AA
/OpenAction
/AcroForm
/Outlines
/Names
/Dests
```

It must not invoke page-wide form recalculation, annotation update, Widget appearance regeneration, JavaScript, form events, validation, formatting, calculation, or activation actions.

## 9. Structural preservation contract

CropBox changes visibility and the page transform. It does not destructively prune the object graph.

The following objects are structurally unchanged except for the page's new `/CropBox` entry:

- page content streams;
- page resources;
- ordinary annotations;
- Link annotations;
- Widget annotations;
- AcroForm field hierarchy and values;
- outline hierarchy;
- internal destination objects;
- metadata and other document-root state.

### 9.1 Objects outside the crop

An annotation, Link, Widget, or destination coordinate outside the new visible rectangle remains in the PDF graph.

After reopening the output, existing public enumeration APIs may report negative or out-of-bounds page-space coordinates for preserved objects that now lie partly or wholly outside the visible page.

The transform must not delete such objects merely because they are no longer visible.

### 9.2 Interactive identity

CropBox V1 does not create a public persistent identity model. Existing immutable snapshot indices remain snapshot-local. Existing editor refs remain session-local to their own edit session and are unrelated to this immutable transform call.

## 10. Observable preservation requirements

The deterministic V1 fixture must verify one transformed output through existing public APIs, not by inspecting `/CropBox` alone.

At minimum it proves:

1. page visible width/height changed as requested;
2. `extractpdf_page_box_bounds(..., CROP, ...)` reflects the new visible region;
3. rendering clips to the new CropBox;
4. surviving text geometry is shifted into the new public page space;
5. surviving image occurrence geometry is shifted likewise;
6. external-link hotspot is shifted, while URI bytes are unchanged;
7. an internal link still targets the same logical page and its resolved public target coordinate follows the target page's new page transform;
8. ordinary annotation type/flags/contents are unchanged and its public bounds follow the new transform;
9. Widget remains associated with the same logical form field, with unchanged flags/button-state/value semantics, while its public bounds follow the new transform;
10. outline hierarchy/title/destination kind are unchanged and its resolved internal destination coordinate follows the target page's new transform;
11. an object outside the new crop remains structurally enumerable rather than being deleted;
12. the source document continues to expose its original page geometry after the transform call;
13. the output remains usable after the source document is closed.

## 11. No-op semantics

No-op is defined explicitly.

A request is a semantic no-op when its public `bounds` exactly equal the page's current effective public visible rectangle after normalization into `extractpdf_rect` values.

For a no-op request:

- no page-local `/CropBox` is written;
- an inherited CropBox is not materialized merely because the page appeared in the request batch;
- no other PDF dictionary is touched.

The API still returns a successful immutable output because `extractpdf_crop_pages()` is an output-producing transform primitive.

If every request in the batch is a no-op, output bytes must equal the existing deterministic serialization of the complete source PDF. Repeating the same no-op call on the same unchanged source must return byte-identical output bytes.

The output is **not** required to be byte-identical to the original input file bytes, because ExtractPDF's serializer may canonicalize a valid source PDF.

In a mixed batch, no-op pages remain structurally untouched while changed pages receive local `/CropBox` entries.

## 12. Error model

Use existing status values only.

### `EXTRACTPDF_ERROR_ARGUMENT`

- null required public pointer;
- zero `crop_count`;
- insufficient request `struct_size`;
- invalid/out-of-range page index;
- duplicate page index;
- NaN or infinity in requested rectangle;
- non-positive requested width/height;
- request extends outside current effective public visible region.

### `EXTRACTPDF_ERROR_FORMAT`

- malformed page dictionary/page-tree structure;
- malformed MediaBox/CropBox;
- invalid effective Rotate/UserUnit representation;
- private reparse produces inconsistent page structure.

### `EXTRACTPDF_ERROR_UNSUPPORTED`

- source is not a PDF;
- encrypted input;
- already-signed input;
- page-tree inheritance depth exceeds 256;
- a valid but unsupported PDF condition prevents the locked preservation contract.

### `EXTRACTPDF_ERROR_NOMEM`

Allocation overflow/failure.

### `EXTRACTPDF_ERROR_MUPDF` / mapped MuPDF statuses

Unexpected MuPDF operational failures that do not map to a more specific public category.

## 13. Security and execution policy

CropBox V1 is not allowed to execute active PDF behavior.

- JavaScript is disabled in the private context immediately after open.
- no form event, validation, format, calculation, or activation action is invoked;
- no high-level form update/recalculation API is used;
- no annotation appearance regeneration is requested;
- encrypted input is fail-closed;
- already-signed input is fail-closed because any full rewrite would invalidate the signature semantics.

Support for decryption/re-encryption belongs to the later security slice under #48. Signed-document rewrite policy is separately out of scope.

## 14. Implementation boundaries

Expected new implementation units should remain narrow, for example:

```text
src/pdf_crop.c
src/pdf_page_box_common.c   (only if a reusable strict page-box resolver is justified)
```

Do not move unrelated Phase 4 or Phase 5 code merely to create a generic transform framework before a second transform proves that abstraction necessary.

A reusable strict page-box helper is acceptable only if it has a single clear responsibility: resolve/validate effective page boxes and page transform inputs without performing mutation.

## 15. Test architecture

Add one new CTest executable/target:

```text
extractpdf.pdf_crop
```

Suite progression:

```text
21 existing CTests -> 22 CTests
```

### 15.1 First RED

The first implementation checkpoint is a strict compile RED:

- all existing #1-#21 executable targets still build;
- only the new crop target fails because the approved public ABI is absent;
- no production CropBox implementation exists yet.

The RED fixture/test commit must not add unrelated implementation changes.

### 15.2 Required fixtures/cases

The final test surface must cover at least:

- two-page interactive deterministic PDF with text, image, URI link, internal link, ordinary annotation, Widget/AcroForm value, and outline destination;
- crop of the source page containing those objects;
- crop of the internal-destination target page;
- at least one object partially or fully outside the new visible region;
- rotated page (`/Rotate 90` at minimum);
- non-default valid `/UserUnit` case or a deterministic combined rotation/UserUnit fixture;
- inherited CropBox materialized locally on mutation;
- all-no-op batch;
- mixed no-op + changed batch;
- multi-page changed batch;
- duplicate page index;
- negative/out-of-range page index;
- NaN/infinity;
- zero/inverted rectangle;
- crop rectangle extending beyond current effective crop;
- malformed MediaBox/CropBox;
- malformed Rotate/UserUnit;
- source immutability;
- output lifetime after source close.

### 15.3 Raw structural assertions

Public observation is primary, but a small private test helper may also parse serialized output to prove that:

- changed pages have exactly the expected local `/CropBox` values;
- no-op pages do not gain a local `/CropBox` merely due to request membership;
- `/MediaBox`, `/Contents`, `/Resources`, `/Annots`, root `/AcroForm`, and root `/Outlines` object content is unchanged by the transform except for serializer-level object renumbering/serialization representation.

Tests must compare semantics rather than relying on stable indirect object numbers across full serialization.

## 16. Verification gates

After RED and implementation, the feature must follow the existing repository discipline:

```text
committed design spec
    ↓
implementation plan
    ↓
strict compile RED
    ↓
minimal GREEN
    ↓
Linux static 22/22
    ↓
Linux ASan/UBSan 22/22
    ↓
frozen exact feature SHA
    ↓
Linux/macOS/Windows full-ci on that exact SHA
    ↓
Critical/Important architecture + scope review
    ↓
STOP — explicit integration authorization
    ↓
merge exact proven feature SHA
    ↓
integrated-master push proof on exact merge SHA
    ↓
close #49 / update #48 and #2
```

No workflow YAML changes are part of this feature unless a separately identified infrastructure defect makes them necessary and receives its own approval.

## 17. Explicit non-goals

CropBox V1 does not include:

- MediaBox rewrite;
- physical trim;
- BleedBox/TrimBox/ArtBox mutation;
- page content translation;
- changing the PDF user-space origin;
- destructive removal of hidden annotations/links/Widgets;
- annotation geometry rewriting;
- destination object rewriting;
- poster split;
- flatten/bake;
- arbitrary content-stream editing;
- image recompression;
- optimization/garbage-collection policy;
- encryption/decryption/re-encryption;
- incremental save;
- JavaScript or form-runtime execution;
- preserving signed-document validity under rewrite;
- multi-threaded handle use.

## 18. Follow-on dependency

After CropBox V1 is integrated, the next Phase 6 design may reuse only the **proven coordinate mapping and strict page-box resolver**.

Poster split must still define its own semantics for duplicating/clipping interactive objects across multiple output pages. Physical MediaBox trim must define printing/page-box semantics independently. Neither may be smuggled into CropBox V1 as an implementation shortcut.
