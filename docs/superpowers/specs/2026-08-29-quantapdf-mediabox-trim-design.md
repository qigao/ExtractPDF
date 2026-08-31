# QuantaPDF Immutable MediaBox Physical Trim V1 Design

Issue: #51  
Parent roadmap: #48 / #2  
Baseline master: `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`  
Baseline content tree: `594499cfea3071f210b5b8781d73e942ba94a94d`  
Depends on: integrated CropBox V1 (#49 / PR #50) and its page-transform direction correction

> **Normative correction:** `docs/superpowers/specs/2026-08-29-quantapdf-mediabox-trim-frame-correction.md` supersedes this document wherever this original design says that preserving a raw `/CropBox` necessarily preserves the same public page frame after `/MediaBox` clips that CropBox, or says that the output visible public rectangle may retain a non-zero origin. Implementation planning is blocked until that correction is reviewed and approved.

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
typedef struct quantapdf_page_trim {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_trim;

QUANTAPDF_API quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output);
```

### 2.1 Ownership

- `document` remains immutable.
- `trims` is caller-owned and read-only for the duration of the call.
- success returns a new independent `quantapdf_output`.
- the output owns copied serialized bytes and survives source document close.
- every failure leaves `*out_output == NULL`.

### 2.2 `struct_size`

Every request element must have:

```c
struct_size >= offsetof(quantapdf_page_trim, bounds)
             + sizeof(quantapdf_rect)
```

Because requests are passed as a C array without an explicit stride, every V1
element must cover through `bounds` and must not exceed
`sizeof(quantapdf_page_trim)`. Larger values return
`QUANTAPDF_ERROR_ARGUMENT`. Future request growth requires a new element
type/API or an explicit-stride API; `struct_size` alone cannot version an array.

## 3. Source coordinate contract

`quantapdf_page_trim.bounds` is expressed in the **current source page's QuantaPDF/Fitz page space**.

The direct public reference surface is:

```c
quantapdf_page_box_bounds(
    page,
    QUANTAPDF_PAGE_BOX_MEDIA,
    &media_bounds);
```

A caller may use any finite positive-area subrectangle of that current public MediaBox rectangle.

The request is not expressed in raw PDF user space.

### 3.1 Public MediaBox is not necessarily the visible page rectangle

When an effective CropBox exists, MuPDF's page frame is anchored by the current effective CropBox/MediaBox intersection. Therefore the public MediaBox rectangle can:

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
PDF user space --pdf_to_public--> QuantaPDF/Fitz page space
QuantaPDF/Fitz page space --inverse(pdf_to_public)--> PDF user space
```

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

Requests outside the current public MediaBox are `QUANTAPDF_ERROR_ARGUMENT`.

After public validation, map the request back to raw PDF user space using:

```text
public_to_pdf = inverse(pdf_to_public)
requested_media_pdf = normalize(
    transform(requested_public, public_to_pdf))
```

The raw request must also remain inside `media_pdf`.

V1 does not uncrop or enlarge MediaBox.

## 5. Post-trim CropBox and page-frame semantics

MediaBox V1 never writes `/CropBox`.

The authoritative page-frame behavior is defined by the normative correction referenced at the top of this document.

### 5.1 No local/inherited CropBox exists

If CropBox is absent through the page inheritance chain, it falls back to MediaBox.

After writing the new MediaBox:

```text
output_media_pdf   = requested_media_pdf
output_visible_pdf = requested_media_pdf
```

Reopening the output re-anchors the public page frame to the new medium.

For deterministic unrotated/UserUnit=1 fixtures:

```text
output visible public bounds = [0,0,width,height]
```

The source request x0/y0 are not required to survive as output public x0/y0.

### 5.2 Local or inherited CropBox exists

The raw CropBox remains structurally unchanged.

Post-trim:

```text
output_media_pdf   = requested_media_pdf
output_crop_pdf    = preserved raw effective CropBox
output_visible_pdf = intersection(output_media_pdf, output_crop_pdf)
```

The intersection must have positive area or the request returns `QUANTAPDF_ERROR_ARGUMENT` before private writes.

Two subcases are normative:

```text
output_visible_pdf == source visible_pdf
    => physical-only trim
    => page frame unchanged
    => visible/object public geometry unchanged

output_visible_pdf != source visible_pdf
    => MediaBox clips effective CropBox
    => output page frame re-anchors to output_visible_pdf
    => visible public origin is re-established at (0,0)
    => object public geometry follows the new page transform
```

The transform must not rewrite CropBox or individual object coordinates merely to preserve the old frame.

### 5.3 Physical-only trim is valid

A changed MediaBox that leaves `output_visible_pdf == source visible_pdf` is still a real transform and is not a no-op.

No-op is defined only by equality with the current public MediaBox.

### 5.4 Output MediaBox may be non-zero

When MediaBox extends outside `output_visible_pdf`, the **output public MediaBox** may have negative/non-zero coordinates relative to the output visible frame.

This is distinct from visible public bounds, which are anchored to the current effective visible origin.

## 6. BleedBox / TrimBox / ArtBox policy

MediaBox V1 does not write, normalize, materialize, or delete:

```text
/BleedBox
/TrimBox
/ArtBox
```

PDF semantics may change their effective region when MediaBox changes. V1 permits this derived effect.

- absent Bleed/Trim/Art keys remain absent;
- explicit keys remain semantically/structurally unchanged;
- V1 does not reject a trim merely because one of these production boxes would have a reduced or empty effective region;
- V1 only requires a positive post-trim effective CropBox intersection because that defines QuantaPDF's visible page contract.

These keys are **opaque preservation state** for this slice. V1 does not parse or validate their values as trim-preflight inputs.

## 7. Strict page-box preflight

Before any source serialization/private write, each requested page must satisfy:

- page object is a dictionary;
- `/Parent` traversal is finite and acyclic;
- inheritance traversal depth is at most 256;
- nearest local/inherited `/MediaBox` exists;
- nearest local/inherited `/CropBox`, when present, is tracked as truly present rather than losing provenance through fallback;
- MediaBox and present CropBox are arrays of exactly four finite numeric values;
- normalized MediaBox and present CropBox have positive dimensions;
- source MediaBox/CropBox effective intersection has positive dimensions;
- effective inherited `/Rotate`, when present, is an integer multiple of 90;
- page-local `/UserUnit`, when present, is finite and strictly positive;
- public MediaBox and visible rectangles are finite and positive-area.

BleedBox/TrimBox/ArtBox are excluded from this consumed-state validation.

Malformed consumed structure returns `QUANTAPDF_ERROR_FORMAT`. Structurally valid inheritance depth greater than 256 returns `QUANTAPDF_ERROR_UNSUPPORTED`.

## 8. Rotate and UserUnit

Valid Rotate and UserUnit are supported.

- `/Rotate` uses normal page inheritance and must be a multiple of 90.
- `/UserUnit` remains page-local; absent means 1.0.
- neither key is written.
- mapping uses `pdf_to_public` and its inverse.
- no hand-coded per-rotation request mapping is allowed.

Rotate 90 and non-default UserUnit are mandatory deterministic fixtures.

## 9. Atomic batch semantics

`quantapdf_trim_pages()` is an immutable atomic batch transform.

Before source serialization/private mutation:

- `out_output != NULL`, then immediately set `*out_output = NULL`;
- `document != NULL`;
- `trims != NULL`;
- `trim_count > 0`;
- allocation arithmetic is overflow-safe;
- every request satisfies minimum `struct_size`;
- every page index is in range and unique;
- every request rectangle is finite and positive-area;
- every requested page passes strict page-box preflight;
- every request is shrink-only against current MediaBox;
- every changed request maps to a finite positive-area raw MediaBox;
- every changed request leaves a positive post-trim visible intersection;
- encrypted input is rejected;
- already-signed input is rejected.

Signed-input detection includes both signed AcroForm signature fields and
catalog `/Perms` entries for `/DocMDP`, `/UR`, or `/UR3` signatures.

All requests are validated before any private write. Any private inconsistency/write/serialization failure discards the private graph and leaves `*out_output == NULL`.

## 10. Full-document isolation

A changed batch uses:

```text
source quantapdf_document
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
new immutable quantapdf_output
```

No MuPDF object/page/annotation/widget/editor identity crosses contexts.

Private-plan consistency is checked before the first write and must reproduce CropBox provenance, source visible intersection, requested raw MediaBox containment, post-trim visible intersection, changed/no-op classification, and frame-preserving/frame-changing classification.

`pdf_graft_mapped_page()` is prohibited.

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

It must not invoke content transformation, annotation/link/widget mutation, form mutation/recalculation, appearance regeneration, JavaScript, or form event execution.

## 12. Structural preservation

Except for the page-local MediaBox write and PDF-defined derived page-box effects, the complete source PDF graph is preserved.

Content/resources/annotations/Links/Widgets/AcroForm/outlines/internal destinations/metadata/page-tree structure remain semantically unchanged.

Objects outside the new MediaBox are not deleted and are not clamped; their public geometry follows the output page transform.

This immutable transform introduces no persistent public object identity.

## 13. Observable output requirements

The deterministic suite must prove both frame modes.

### 13.1 No-CropBox fallback

- MediaBox changes;
- CropBox fallback follows MediaBox;
- visible public bounds re-anchor to `(0,0)`;
- render clips to new medium;
- public object/destination coordinates follow the new page transform;
- source remains unchanged.

### 13.2 Real CropBox, physical-only trim

- raw CropBox unchanged;
- MediaBox changes;
- effective visible intersection unchanged;
- visible public bounds unchanged;
- object and destination public geometry unchanged;
- transform is still not a no-op.

### 13.3 Real CropBox, clipping trim

- raw CropBox unchanged;
- MediaBox changes;
- effective visible intersection shrinks;
- output visible public bounds re-anchor to `(0,0)` with reduced dimensions;
- public object/destination geometry follows the new output page transform;
- no underlying object geometry is rewritten;
- at least one fixture proves output public MediaBox may have negative/non-zero coordinates relative to the output visible frame.

### 13.4 Output lifetime

Output remains usable after the source document closes.

## 14. No-op semantics

A request is a no-op only when its public bounds equal the page's current public MediaBox rectangle component-wise. `-0.0 == +0.0`; NaN is rejected first.

No-op pages do not materialize local MediaBox or touch ancestors/other dictionaries.

If all requests are no-op, validate completely, serialize source once, return canonical bytes directly, and do not reopen/write a private graph. Repeated all-no-op calls must be byte-identical. No promise is made against original input bytes.

Mixed batches leave no-op pages untouched and write local MediaBox only on changed pages.

## 15. Error model

### `QUANTAPDF_ERROR_ARGUMENT`

- null required pointer;
- zero `trim_count`;
- insufficient `struct_size`;
- out-of-range/duplicate page index;
- NaN/infinity;
- zero/inverted request;
- request outside current public MediaBox;
- inverse-mapped request outside raw MediaBox;
- changed request leaves empty/non-positive post-trim visible intersection.

### `QUANTAPDF_ERROR_FORMAT`

- malformed page dictionary/page tree;
- missing/malformed MediaBox;
- malformed truly present CropBox;
- source MediaBox/CropBox intersection empty;
- invalid inherited Rotate;
- invalid page-local UserUnit;
- private reparse changes consumed target/plan semantics.

Malformed Bleed/Trim/Art are not trim-specific FORMAT conditions.

### `QUANTAPDF_ERROR_UNSUPPORTED`

- non-PDF;
- encrypted;
- signed;
- valid inheritance depth >256;
- valid unsupported condition preventing preservation.

### `QUANTAPDF_ERROR_NOMEM`

Allocation overflow/failure.

Unexpected MuPDF operational failures map through the existing status boundary.

## 16. Security / active behavior policy

- disable private JavaScript immediately after open;
- no JS actions;
- no form validation/format/calculation/activation;
- no form setter/recalculation;
- no annotation/widget appearance regeneration;
- encrypted and signed input fail closed.

## 17. Private architecture boundary

The second page-box transform justifies one narrow shared read-only resolver:

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

The common unit is read-only and does not parse Bleed/Trim/Art. CropBox migration is only what is necessary to consume this resolver; its public behavior and existing 22nd CTest must remain unchanged.

No generic transform options/registry/visitor framework or public editor session is authorized.

## 18. Test architecture

Add one new CTest:

```text
quantapdf.pdf_trim
```

Baseline 22 CTests -> target 23 CTests.

First strict RED: old 22 executable targets continue to build/pass; new trim target fails to compile only because `quantapdf_page_trim` / `quantapdf_trim_pages()` are absent.

## 19. Required deterministic cases

Final trim target covers at least:

1. no-CropBox fallback + frame re-anchor;
2. real CropBox physical-only trim + unchanged frame;
3. real CropBox clipping trim + frame re-anchor;
4. output MediaBox non-zero/negative relative to visible frame;
5. inherited CropBox semantics;
6. inherited MediaBox changed/local materialization;
7. inherited MediaBox no-op/no materialization;
8. raw CropBox outside MediaBox;
9. Rotate 90;
10. page-local non-default UserUnit;
11. explicit opaque Bleed/Trim/Art preserved;
12. absent Bleed/Trim/Art remain absent;
13. two-page interactive preservation;
14. object outside new MediaBox remains enumerable;
15. multi-page changed batch;
16. all-no-op determinism;
17. mixed batch;
18. duplicate/out-of-range/nonfinite/empty/outside-MediaBox rejection;
19. disjoint post-trim visible intersection rejection;
20. malformed MediaBox/CropBox/Rotate/UserUnit;
21. source immutability;
22. output lifetime;
23. repeated changed determinism;
24. failure output reset.

Earlier integrated fixtures may be reused read-only; do not modify them.

## 20. Raw structural assertions

Test-only MuPDF helpers may prove:

- changed page has local normalized MediaBox;
- changed inherited MediaBox does not modify ancestor;
- no-op inherited MediaBox remains inherited;
- raw CropBox preserved/not spuriously materialized;
- explicit Bleed/Trim/Art semantically equal without repair;
- absent Bleed/Trim/Art remain absent;
- Contents/Resources/Annots/AcroForm/Outlines/Names/Dests preserved.

Do not depend on indirect object numbers or naïve deep recursion through cyclic form graphs.

## 21. Verification gates

```text
committed design + correction
        ↓
implementation plan
        ↓
strict compile RED
        ↓
ABI/runtime RED
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
Critical/Important review
        ↓
STOP — explicit integration authorization
        ↓
merge exact proven feature SHA
        ↓
integrated-master push proof
        ↓
close #51 / checkpoint #48/#2
```

Any source/test/spec change after freeze invalidates proof. Do not edit workflow YAML merely to obtain proof; use existing `full-ci` label mechanism.

## 22. Explicit non-goals

No CropBox/BleedBox/TrimBox/ArtBox mutation, content/object translation, deletion outside MediaBox, poster split, flatten, arbitrary editing, optimize/gc, image recompression, security rewrite, incremental save, JS/form runtime, signature-preserving rewrite, persistent object IDs, or multithreaded handle semantics.

## 23. Completion criterion

Complete only when one exact feature head proves:

- immutable source -> immutable output;
- shrink-only public MediaBox input;
- correct source inverse mapping under Rotate/UserUnit;
- no-CropBox fallback re-anchor;
- physical-only real-CropBox trim keeps effective frame unchanged;
- clipping real-CropBox trim re-anchors to new effective intersection;
- raw Crop/Bleed/Trim/Art untouched;
- only changed pages receive local MediaBox;
- interactive/root graph remains coherent;
- deterministic/no-op/failure-atomic batch behavior;
- 23/23 static + sanitizer;
- same-SHA Linux/macOS/Windows;
- no Critical/Important blocker;
- explicit integration authorization + integrated-master proof.

Only after this page-box foundation is integrated should Phase 6 proceed to poster split.
