# ExtractPDF Immutable MediaBox Physical Trim V1 Design

Issue: #51  
Parent roadmap: #48 / #2  
Baseline master: `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`  
Baseline content tree: `594499cfea3071f210b5b8781d73e942ba94a94d`  
Depends on: integrated CropBox V1 (#49 / PR #50) and its page-transform direction correction

## 1. Purpose

MediaBox Physical Trim V1 completes the first Phase 6 page-box foundation before poster split.

CropBox V1 changes the visible clipping region. MediaBox V1 changes the **physical page medium** by writing page-local `/MediaBox` values while deliberately preserving the rest of the page/document graph.

The distinction is normative:

```text
CropBox transform
    changes visible clipping intent

MediaBox trim
    changes physical medium extent
```

MediaBox V1 is not a synonym for CropBox V1 and does not automatically rewrite CropBox, BleedBox, TrimBox, or ArtBox.

The reusable Phase 6 invariant remains:

> Geometry-changing transforms alter the relevant page-box primitive and let the resulting PDF page transform determine public observations. They do not rewrite every content/interactive object unless a later API explicitly promises that behavior.

Poster split must build on these two established page-box semantics rather than inventing a third coordinate model.

## 2. Public API

Add one request structure and one immutable transform primitive:

```c
typedef struct extractpdf_page_trim {
    size_t struct_size;
    int page_index;
    extractpdf_rect bounds;
} extractpdf_page_trim;

EXTRACTPDF_API extractpdf_status extractpdf_trim_pages(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_output **out_output);
```

### 2.1 Ownership

- `document` remains immutable.
- `trims` is caller-owned and read-only for the duration of the call.
- success returns a new independent `extractpdf_output`.
- the output owns copied serialized bytes and survives source document close.
- every failure leaves `*out_output == NULL`.

### 2.2 `struct_size`

Every request element must have:

```c
struct_size >= offsetof(extractpdf_page_trim, bounds)
             + sizeof(extractpdf_rect)
```

Smaller values return `EXTRACTPDF_ERROR_ARGUMENT`. Larger values are accepted for ABI-forward compatibility.

## 3. Source coordinate contract

`extractpdf_page_trim.bounds` is expressed in the **current source page's ExtractPDF/Fitz page space**.

The direct public reference surface is:

```c
extractpdf_page_box_bounds(
    page,
    EXTRACTPDF_PAGE_BOX_MEDIA,
    &media_bounds);
```

A caller may use any finite positive-area subrectangle of that current public MediaBox rectangle.

The request is not expressed in raw PDF user space.

### 3.1 Public MediaBox is not necessarily the visible page rectangle

When an effective CropBox exists, MuPDF's page frame is anchored by that CropBox semantics. Therefore the public MediaBox rectangle can:

- extend beyond the current visible page rectangle;
- have negative coordinates;
- have a non-zero origin;
- remain larger than the visible page while still being a valid trim input domain.

This is intentional. MediaBox trim is allowed to operate on physical page area outside the current CropBox.

### 3.2 Strict source box model

For every requested page, strict preflight resolves:

```text
media_pdf = normalized(nearest local/inherited MediaBox)

if a local/inherited CropBox exists:
    has_explicit_crop = true
    crop_pdf = normalized(nearest local/inherited CropBox)
else:
    has_explicit_crop = false
    crop_pdf = media_pdf       // source default only

visible_pdf = intersection(media_pdf, crop_pdf)
```

`visible_pdf` must have positive width and height for the source page.

A raw CropBox extending beyond MediaBox is accepted; only its effective intersection is used for source visible-page validity.

### 3.3 PDF/public matrix direction

This design inherits the already-integrated CropBox matrix correction as a project invariant:

```text
PDF user space --pdf_to_public--> ExtractPDF/Fitz page space
ExtractPDF/Fitz page space --inverse(pdf_to_public)--> PDF user space
```

The matrix returned by MuPDF's page transform path must be treated accordingly inside ExtractPDF, regardless of contradictory prose comments in external headers/source.

Strict public rectangles are derived as:

```text
media_public   = normalize(transform(media_pdf,   pdf_to_public))
visible_public = normalize(transform(visible_pdf, pdf_to_public))
```

The implementation must not use MuPDF's tolerant box fallback/repair result as the strict source of truth.

## 4. Shrink-only MediaBox rule

For every request:

```text
finite(x0, y0, x1, y1)
x0 < x1
y0 < y1
request.x0 >= media_public.x0
request.y0 >= media_public.y0
request.x1 <= media_public.x1
request.y1 <= media_public.y1
```

Requests outside the current public MediaBox are `EXTRACTPDF_ERROR_ARGUMENT`.

After public validation, map the request back to raw PDF user space using:

```text
public_to_pdf = inverse(pdf_to_public)
requested_media_pdf = normalize(
    transform(requested_public, public_to_pdf))
```

The raw request must also remain inside `media_pdf`; this second containment check is mandatory so floating-point or transform mistakes cannot serialize an expansion.

V1 does not uncrop or enlarge MediaBox.

## 5. Post-trim CropBox semantics

MediaBox V1 never writes `/CropBox`.

The post-trim visible-page behavior depends on whether CropBox existed as a real local/inherited page attribute before the trim.

### 5.1 No local/inherited CropBox exists

If CropBox is absent through the page inheritance chain, PDF defines CropBox by MediaBox fallback.

After writing the new page-local MediaBox:

```text
output_media_pdf = requested_media_pdf
output_crop_pdf  = output_media_pdf       // fallback
output_visible_pdf = output_media_pdf
```

Because the page frame now falls back to the new MediaBox, reopening the output re-anchors the public page space to that new medium.

Observable result:

```text
output MediaBox public origin  = (0,0)
output CropBox public origin   = (0,0)
output visible page origin     = (0,0)
```

subject to Rotate/UserUnit orientation and scaling.

Existing content/annotation/link/widget/destination geometry is **not rewritten in PDF space**; its public coordinates change because the page transform changed.

### 5.2 Local or inherited CropBox exists

If a real CropBox attribute exists, its raw value remains structurally unchanged.

Post-trim:

```text
output_media_pdf   = requested_media_pdf
output_crop_pdf    = preserved raw effective CropBox
output_visible_pdf = intersection(output_media_pdf, output_crop_pdf)
```

The intersection must have positive width and height. Otherwise the request returns `EXTRACTPDF_ERROR_ARGUMENT` before private writes.

Because `/CropBox`, `/Rotate`, and `/UserUnit` are not changed, the page transform frame remains anchored by the preserved CropBox semantics.

Therefore the output public visible rectangle is:

```text
normalize(transform(output_visible_pdf, existing pdf_to_public))
```

and **need not begin at `(0,0)`**.

This non-zero-origin behavior is a required regression test. V1 must not secretly rewrite CropBox merely to re-origin the page.

### 5.3 Physical-only trim is valid

A changed MediaBox may still fully contain the preserved effective CropBox.

In that case:

```text
MediaBox changes
visible page does not change
page transform does not change
```

This is a valid physical-only trim and must not be collapsed into a semantic no-op.

No-op is defined only by equality with the current MediaBox, not by equality of the visible page result.

### 5.4 Trim that clips a preserved CropBox

If the new MediaBox cuts into a preserved CropBox but leaves positive intersection:

- MediaBox changes;
- raw CropBox remains unchanged;
- visible page becomes the intersection;
- page frame remains CropBox-anchored;
- existing object geometry is not translated or rewritten.

The resulting public visible bounds may have non-zero x0/y0 and reduced width/height.

## 6. BleedBox / TrimBox / ArtBox policy

MediaBox V1 does not write, normalize, materialize, or delete:

```text
/BleedBox
/TrimBox
/ArtBox
```

PDF semantics define these boxes relative to CropBox when absent and reduce boxes that extend outside MediaBox to their effective intersection with MediaBox.

Therefore changing MediaBox may change their **effective region** without changing their raw dictionary representation.

V1 explicitly permits this derived effect.

- absent Bleed/Trim/Art keys remain absent;
- explicit keys remain byte/structure-equivalent at the semantic-object level;
- V1 does not reject a trim merely because one of these production boxes would have a reduced or empty effective region;
- V1 only requires a positive post-trim effective CropBox intersection because that defines ExtractPDF's visible page contract.

A future production-printing-box API may impose stronger rules. This trim primitive does not silently invent them.

## 7. Strict page-box preflight

Transformation is stricter than tolerant rendering.

Before any source serialization/private write, each requested page must satisfy:

- page object is a dictionary;
- `/Parent` traversal for inheritable attributes is finite and acyclic;
- inheritance traversal depth is at most 256;
- nearest local/inherited `/MediaBox` exists;
- nearest local/inherited `/CropBox`, when present, is tracked as explicitly present rather than losing that provenance through MediaBox fallback;
- MediaBox and present CropBox are arrays of exactly four finite numeric values;
- normalized MediaBox and present CropBox have positive dimensions;
- source MediaBox/CropBox effective intersection has positive dimensions;
- effective inherited `/Rotate`, when present, is an integer multiple of 90;
- page-local `/UserUnit`, when present, is finite and strictly positive;
- public MediaBox and visible rectangles derived from the strict raw model are finite and positive-area.

Malformed structure returns `EXTRACTPDF_ERROR_FORMAT`.

Structurally valid inheritance depth greater than 256 returns `EXTRACTPDF_ERROR_UNSUPPORTED`.

The validator must not rely on MuPDF silently substituting Letter-size/unit rectangles for malformed or empty boxes.

## 8. Rotate and UserUnit

Valid Rotate and UserUnit are supported in V1.

- `/Rotate` uses normal page inheritance and must be a multiple of 90.
- `/UserUnit` remains page-local; absent means 1.0.
- neither key is written by this transform.
- mapping uses the same strict `pdf_to_public` page matrix and its inverse as CropBox V1.
- no hand-coded per-rotation switch table is allowed for request mapping.

Rotate 90 and non-default UserUnit are mandatory deterministic fixtures.

## 9. Atomic batch semantics

`extractpdf_trim_pages()` is an immutable atomic batch transform.

### 9.1 Public validation

Before source serialization/private mutation:

- `out_output != NULL`, then immediately set `*out_output = NULL`;
- `document != NULL`;
- `trims != NULL`;
- `trim_count > 0`;
- allocation arithmetic is overflow-safe;
- every request satisfies the minimum `struct_size`;
- every page index is in range;
- no page index appears more than once;
- every request rectangle is finite and positive-area;
- every requested page passes strict page-box preflight;
- every request is shrink-only against current MediaBox;
- every changed request maps to a finite positive-area raw MediaBox;
- every changed request with explicit/inherited CropBox leaves positive post-trim MediaBox/CropBox intersection;
- encrypted input is rejected;
- already-signed input is rejected.

Duplicate page indices return `EXTRACTPDF_ERROR_ARGUMENT`; no last-writer-wins behavior exists.

### 9.2 No partial publication

All requests are validated before any private write.

For changed batches, every target page is reparsed/revalidated in the private graph before the first `/MediaBox` write.

Any private inconsistency, write failure, or final serialization failure discards the private graph and leaves `*out_output == NULL`.

Failure atomicity is achieved by isolation, not journal rollback.

## 10. Full-document isolation

A batch containing at least one changed request uses:

```text
source extractpdf_document
        ↓
strict source preflight for all requested pages
        ↓
canonical full-PDF serialization
        ↓
fresh private MuPDF context
        ↓
open private PDF / disable JavaScript immediately
        ↓
security recheck
        ↓
re-resolve + revalidate every target page
        ↓
verify private plan remains semantically consistent
        ↓
write local /MediaBox only for changed requests
        ↓
deterministic full-PDF serialization
        ↓
new immutable extractpdf_output
```

No `pdf_obj *`, `pdf_page *`, annotation pointer, Widget pointer, editor ref, or other MuPDF identity crosses from source context into private context.

`pdf_graft_mapped_page()` is prohibited; this is transform preservation, not composition.

## 11. Write surface

For every changed page the sole semantic dictionary write is:

```text
page dictionary /MediaBox = normalized requested raw rectangle
```

The write is page-local even when source MediaBox was inherited.

MediaBox V1 must not write or normalize:

```text
/CropBox
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

It must not invoke:

- content-stream transformation;
- annotation create/update/delete;
- link mutation;
- Widget geometry mutation;
- form value mutation/recalculation;
- appearance regeneration;
- JavaScript;
- form event/validation/format/calculation/activation execution.

## 12. Structural preservation

Except for the page-local MediaBox write and PDF-defined derived page-box effects, the complete source PDF graph is preserved.

Structurally unchanged objects include:

- page content streams;
- resources;
- ordinary annotations;
- Links;
- Widgets;
- AcroForm hierarchy/values/options;
- outline hierarchy/titles/actions;
- internal destination objects;
- metadata;
- page-tree parent/kids structure.

### 12.1 Objects outside the new MediaBox

Objects are not deleted merely because their geometry lies partly or wholly outside the new physical medium.

They remain enumerable through existing APIs when those APIs enumerate by PDF structure rather than visibility.

Their public geometry is determined only by the output page transform; V1 never clamps object bounds to the new MediaBox.

### 12.2 Interactive identity

This immutable transform introduces no persistent public object identity.

Snapshot indices remain snapshot-local and editor refs remain editor-session-local.

## 13. Observable output requirements

The deterministic V1 suite must verify behavior through existing public APIs, not only raw MediaBox inspection.

At minimum it proves both page-frame modes.

### 13.1 No-CropBox fallback case

For a page with no local/inherited CropBox:

1. MediaBox changes to the requested physical region;
2. output CropBox follows MediaBox by fallback;
3. output visible page dimensions match the requested physical dimensions;
4. output visible/public MediaBox origin is `(0,0)`;
5. render is clipped to the new medium;
6. text/image/link/annotation/widget coordinates change according to the newly re-anchored page transform;
7. internal destination resolution on a trimmed target page follows the target page's new transform;
8. source page remains unchanged.

### 13.2 Preserved-CropBox case

For a page with local/inherited CropBox:

1. raw CropBox remains unchanged;
2. MediaBox changes to the requested region;
3. effective visible region is `intersection(new MediaBox, preserved CropBox)`;
4. public visible bounds may be non-zero;
5. if new MediaBox still contains all of CropBox, visible page geometry remains unchanged despite a real physical trim;
6. if new MediaBox clips CropBox, visible bounds shrink without rewriting object geometry;
7. text/image/link/annotation/widget public coordinates remain in the preserved CropBox-anchored frame;
8. URI/content/value/title bytes and logical page/field relationships remain unchanged;
9. outline/internal destinations retain their logical target and follow only the target page transform;
10. objects outside the new medium remain structurally enumerable.

### 13.3 Output lifetime

The output must reopen and remain fully usable after the source document is closed.

## 14. No-op semantics

A request is a semantic no-op only when its public `bounds` are component-wise numerically equal to the page's **current public MediaBox rectangle**.

NaN is rejected before comparison; `-0.0` and `+0.0` compare equal.

A no-op request:

- does not write `/MediaBox`;
- does not materialize inherited MediaBox;
- does not touch page-tree ancestors;
- does not write any other page/document dictionary.

A physical MediaBox change that leaves visible CropBox behavior unchanged is **not** a no-op.

### 14.1 All-no-op batch

If every request is a no-op:

1. validate the full batch, including security and page structure;
2. run the existing deterministic source serialization exactly once;
3. return those bytes directly;
4. do not open a private PDF graph;
5. do not write any page dictionary.

Repeated all-no-op calls on the same unchanged source must be byte-identical.

No promise is made that canonical output bytes equal the original input file bytes.

### 14.2 Mixed batch

No-op pages remain structurally untouched. Changed pages receive local `/MediaBox` values only.

## 15. Error model

Use existing public status values only.

### `EXTRACTPDF_ERROR_ARGUMENT`

- null required public pointer;
- zero `trim_count`;
- insufficient request `struct_size`;
- out-of-range page index;
- duplicate page index;
- NaN/infinity request coordinate;
- zero/inverted request rectangle;
- request extends outside current public MediaBox;
- raw inverse-mapped request extends outside current raw MediaBox;
- changed request leaves empty/non-positive MediaBox/CropBox intersection when a real CropBox exists.

### `EXTRACTPDF_ERROR_FORMAT`

- malformed page dictionary/page tree;
- missing/malformed MediaBox;
- malformed explicitly present CropBox;
- source MediaBox/CropBox intersection is empty;
- invalid inherited Rotate representation;
- invalid page-local UserUnit;
- private reparse produces a structurally inconsistent target page or changed plan.

### `EXTRACTPDF_ERROR_UNSUPPORTED`

- source is not a PDF;
- encrypted source;
- already-signed source;
- structurally valid inheritance depth greater than 256;
- valid but unsupported PDF condition prevents the preservation contract.

### `EXTRACTPDF_ERROR_NOMEM`

Allocation overflow/failure.

### mapped MuPDF operational errors

Unexpected MuPDF failures map through the existing status boundary and never escape as exceptions.

## 16. Security / active behavior policy

MediaBox V1 executes no active PDF behavior.

- private JavaScript is disabled immediately after open;
- no JavaScript action is executed;
- no form validation/format/calculation/activation event is invoked;
- no high-level form setter/recalculation API is used;
- no annotation or Widget appearance regeneration is requested;
- encrypted input fails closed;
- signed input fails closed because full rewrite invalidates signature semantics.

Security rewrite belongs to the later dedicated #48 slice.

## 17. Private architecture boundary

The second page-box transform now justifies one narrow shared private resolver.

Expected direction:

```text
src/pdf_page_box_common.[ch]
    strict page-tree inheritance walk
    MediaBox/CropBox parsing + CropBox provenance
    Rotate/UserUnit validation
    raw MediaBox/CropBox/effective-visible calculation
    PDF -> public matrix capture
    raw <-> public rectangle mapping helpers

src/pdf_crop_preflight.c
    CropBox-specific request policy

src/pdf_trim_preflight.c
    MediaBox-specific request policy

src/pdf_crop.c
    existing CropBox orchestration/write

src/pdf_trim.c
    MediaBox orchestration/write
```

The common unit must remain read-only. It performs no dictionary writes and owns no transform orchestration.

CropBox implementation should be migrated only as necessary to consume this shared resolver; its public behavior and already-proven 22nd CTest contract must remain unchanged.

No generic `transform options` struct, transform registry, visitor framework, or arbitrary page-box rewrite API is authorized.

No new public editor/session object is authorized.

## 18. Test architecture

Add one new CTest:

```text
extractpdf.pdf_trim
```

Integrated baseline:

```text
22 CTests
```

Target after this slice:

```text
23 CTests
```

The first strict RED occurs before any trim production implementation:

- all existing 22 executable targets continue to build;
- the new trim test target fails to compile only because `extractpdf_page_trim` / `extractpdf_trim_pages()` do not yet exist;
- existing CropBox and all Phase 2-5 tests remain untouched and green.

## 19. Required deterministic fixtures/cases

The final trim target must cover at least:

1. **no CropBox fallback** — changed MediaBox becomes both physical and visible region, with output origin re-anchored;
2. **explicit CropBox, physical-only trim** — MediaBox shrinks but still contains CropBox; visible page and object coordinates remain unchanged;
3. **explicit CropBox, clipping trim** — MediaBox clips part of CropBox; raw CropBox unchanged and public visible bounds become smaller/non-zero;
4. **inherited CropBox** — same preserved-frame semantics without materializing CropBox;
5. **inherited MediaBox changed** — changed page receives local MediaBox only;
6. **inherited MediaBox no-op** — no local MediaBox materialization;
7. **raw CropBox outside MediaBox** — valid source intersection and deterministic further MediaBox shrink;
8. **Rotate 90**;
9. **non-default page-local UserUnit**;
10. **explicit BleedBox/TrimBox/ArtBox** — raw entries unchanged;
11. **absent BleedBox/TrimBox/ArtBox** — keys remain absent;
12. **two-page interactive preservation** — text, image, URI link, internal link, ordinary annotation, Widget + AcroForm value, outline destination;
13. **object outside new MediaBox** — remains structurally enumerable;
14. **multi-page changed batch**;
15. **all-no-op batch determinism**;
16. **mixed no-op + changed batch**;
17. **duplicate page index**;
18. **out-of-range page index**;
19. **NaN / infinity**;
20. **zero/inverted request rect**;
21. **request outside current MediaBox**;
22. **request disjoint from preserved CropBox** — `ARGUMENT`;
23. **malformed MediaBox**;
24. **malformed CropBox**;
25. **bad inherited Rotate**;
26. **bad page-local UserUnit**;
27. **source immutability**;
28. **output lifetime after source close**;
29. **repeated changed batch deterministic bytes**;
30. **failure output reset to NULL**.

Fixtures should be deterministic repository data. Existing fixtures may be reused read-only where their contract matches; do not mutate fixtures owned by earlier integrated tests.

## 20. Raw structural assertions

Private test helpers may inspect raw PDF structure only to prove invariants that public APIs cannot observe directly.

Allowed assertions include:

- changed page has local normalized `/MediaBox`;
- changed inherited MediaBox does not modify ancestor;
- no-op inherited MediaBox remains inherited;
- raw explicit/inherited CropBox remains unchanged and is not materialized locally merely by trim;
- explicit Bleed/Trim/Art entries remain semantically equal;
- absent Bleed/Trim/Art entries remain absent;
- Contents/Resources/Annots/AcroForm/Outlines/Names/Dests structures remain semantically preserved.

Tests must not depend on indirect object numbers created by serialization.

Semantic/deep comparison must avoid naïve recursion through cyclic Widget/AcroForm graphs.

## 21. Verification gates

After a later approved implementation plan:

```text
committed design spec
        ↓
implementation plan
        ↓
strict compile RED
        ↓
minimal ABI/runtime RED
        ↓
minimal GREEN
        ↓
Linux static 23/23
        ↓
Linux ASan/UBSan 23/23
        ↓
freeze exact feature SHA
        ↓
same-SHA Linux/macOS/Windows full-ci
        ↓
Critical/Important architecture + scope review
        ↓
STOP — explicit integration authorization
        ↓
merge exact proven feature SHA
        ↓
integrated-master push proof
        ↓
close #51 / update #48/#2
```

Any source/test/spec change after candidate freeze invalidates same-SHA proof.

Do not edit workflow YAML merely to obtain proof. Use the existing `full-ci` label mechanism unless a separate infrastructure defect is identified and approved.

## 22. Explicit non-goals

V1 does not provide:

- explicit CropBox mutation in the same call;
- BleedBox mutation;
- TrimBox mutation;
- ArtBox mutation;
- content-stream translation;
- per-object geometry translation;
- deletion of objects outside MediaBox;
- poster split;
- flatten/bake;
- arbitrary content editing;
- optimize/garbage collection;
- image recompression;
- encryption/decryption/re-encryption;
- incremental save;
- JavaScript/form runtime;
- signature-preserving rewrite;
- persistent PDF object IDs;
- multithreaded handle semantics.

## 23. Completion criterion

MediaBox Physical Trim V1 is complete only when one exact feature head proves all of the following:

- immutable source -> immutable output;
- shrink-only public MediaBox input;
- correct PDF/public inverse mapping under Rotate/UserUnit;
- no-CropBox fallback re-anchors output page space correctly;
- preserved CropBox keeps its frame and can produce non-zero output visible bounds;
- physical-only trim is distinguishable from CropBox crop;
- raw Crop/Bleed/Trim/Art keys remain untouched;
- only changed pages receive local MediaBox;
- complete interactive/document-root graph remains structurally coherent;
- deterministic/no-op/failure-atomic batch behavior;
- 23/23 static + sanitizer tests;
- same-SHA Linux/macOS/Windows proof;
- no Critical/Important architecture blocker;
- explicit integration authorization followed by integrated-master proof.

Only after this page-box foundation is integrated should Phase 6 proceed to poster split.