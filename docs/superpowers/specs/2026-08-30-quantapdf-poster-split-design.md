# QuantaPDF Immutable Poster Split V1 Design

Issue: #53  
Parent roadmap: #48 / #2  
Baseline master: `eca8179e59723e2e6dfd3b3acbdedc61c15bf350`  
Baseline content tree: `7454c7bd2029bad2f6fe595c66b301b431c8815f`  
MuPDF baseline: 1.28.2  
Depends on: integrated CropBox V1 (#49 / PR #50) and MediaBox Physical Trim V1 (#51 / PR #52)

## 1. Purpose

Poster Split V1 expands one or more selected source PDF pages into deterministic `columns × rows` tile pages while preserving the complete PDF document graph as far as V1 explicitly supports.

The operation is an immutable transform:

```text
quantapdf_document
    -> strict poster-split preflight
    -> isolated private PDF graph
    -> tile-page expansion + interactive remap
    -> deterministic serialization
    -> quantapdf_output
```

It is not the existing Phase 4 range split. Phase 4 range split exports existing pages. Poster Split creates multiple page views over one source page's visible content.

The transform builds on two integrated Phase 6 primitives:

```text
CropBox V1
    visible clipping semantics

MediaBox Trim V1
    physical-medium semantics

Poster Split V1
    page multiplicity + page-frame tiling
```

Poster Split must not introduce a third coordinate convention.

## 2. Why composition grafting is not the substrate

MuPDF 1.28.2 `pdf_graft_mapped_page()` intentionally creates a new page dictionary containing only a narrow page-content subset:

```text
Contents
Resources
MediaBox
CropBox
BleedBox
TrimBox
ArtBox
Rotate
UserUnit
```

It intentionally does not copy page-reference interactive structures such as `/Annots`, Widgets, or complete document-root state.

That behavior is correct for QuantaPDF's Phase 4 composition APIs but insufficient for Poster Split, whose public contract explicitly covers links, annotations, Widgets, AcroForm relationships, outlines, and internal destinations.

Therefore V1 MUST NOT implement Poster Split by building a page-only destination document with `pdf_graft_mapped_page()`.

The architectural rule is:

> Poster Split operates inside a complete private copy of the source PDF graph. Tile pages are created inside that same private document, so retained root structures and shared content/resource objects remain in one object namespace.

## 3. Public API

Add one request structure and one immutable transform primitive:

```c
typedef struct quantapdf_page_poster_split {
    size_t struct_size;
    int page_index;
    size_t columns;
    size_t rows;
} quantapdf_page_poster_split;

QUANTAPDF_API quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output);
```

### 3.1 Ownership

- `document` remains immutable.
- `splits` is caller-owned and read-only for the duration of the call.
- success returns a new independent `quantapdf_output`.
- the output owns copied serialized bytes.
- the output survives closing the source document.
- every failure leaves `*out_output == NULL`.

### 3.2 `struct_size`

For every request element:

```c
struct_size >= offsetof(quantapdf_page_poster_split, rows)
             + sizeof(size_t)
```

Because requests are passed as a C array without an explicit stride, every V1
element must satisfy:

```c
offsetof(quantapdf_page_poster_split, rows) + sizeof(size_t)
    <= struct_size
    <= sizeof(quantapdf_page_poster_split)
```

Any other size returns `QUANTAPDF_ERROR_ARGUMENT`. Future request growth
requires a new element type/API or an explicit-stride API; `struct_size` alone
cannot version an array.

### 3.3 Grid dimensions

For every request:

```text
columns >= 1
rows    >= 1
```

The product must fit in both `size_t` and the final PDF page-count model.

`1 × 1` is a semantic no-op for that page.

V1 deliberately has no:

- overlap;
- gutter;
- bleed;
- target paper size;
- target physical units;
- auto rotation;
- page scaling.

Those options require separate design because they alter print and/or content geometry semantics.

## 4. Source page coordinate model

Poster Split consumes the same strict page-box view already established by CropBox and MediaBox Trim:

```text
media_pdf   = normalized effective MediaBox
crop_pdf    = normalized effective CropBox, or MediaBox fallback
visible_pdf = intersection(media_pdf, crop_pdf)

PDF user space --pdf_to_public--> QuantaPDF/Fitz public page space
```

`visible_pdf` must have positive area.

The source public visible rectangle is:

```text
visible_public = transform(visible_pdf, pdf_to_public)
```

Grid construction is defined in this source public Fitz page space.

This means:

- row order is visually top-to-bottom;
- column order is visually left-to-right;
- Rotate and UserUnit do not create a second grid convention;
- the same design applies to unrotated, rotated, and non-default UserUnit pages.

## 5. Deterministic tile geometry

For a request with `columns = C` and `rows = R`, preflight computes public edge arrays:

```text
x[0..C]
y[0..R]
```

with these invariants:

```text
x[0] = visible_public.x0
x[C] = visible_public.x1

y[0] = visible_public.y0
y[R] = visible_public.y1
```

Interior edges are derived directly from the source span and integer edge index, not by repeatedly accumulating tile width/height.

Conceptually:

```text
x[i] = x0 + (x1 - x0) * i / C
y[j] = y0 + (y1 - y0) * j / R
```

The implementation must reject the request as `QUANTAPDF_ERROR_ARGUMENT` if finite source geometry plus requested grid density cannot produce strictly positive-area public tiles under the implementation's float representation.

Each tile is:

```text
tile_public(row, col) =
    [x[col], y[row], x[col + 1], y[row + 1]]
```

and maps back through the source inverse transform:

```text
public_to_pdf = inverse(pdf_to_public)

tile_pdf = normalized(
    transform(tile_public, public_to_pdf))
```

Every `tile_pdf` must be finite, positive-area, and contained in `visible_pdf` modulo the exact shared boundary construction.

### 5.1 Tile order

Tile order is normative row-major:

```text
for row = 0 .. rows-1
    for col = 0 .. columns-1
```

For 2×2:

```text
┌──────────┬──────────┐
│ tile 0   │ tile 1   │
├──────────┼──────────┤
│ tile 2   │ tile 3   │
└──────────┴──────────┘
```

### 5.2 Tile page frame

Every output tile page uses:

```text
MediaBox = tile_pdf
CropBox  = tile_pdf
Rotate   = source effective Rotate
UserUnit = source page-local UserUnit when non-default/present as required
```

The source content is not globally translated.

The new tile page frame performs the public-coordinate shift. For deterministic unrotated/UserUnit=1 fixtures:

```text
output visible bounds = [0, 0, tile_width, tile_height]
```

For Rotate/UserUnit cases, the public observation must follow the same MuPDF page-transform model already proven by the page-box foundation.

## 6. Output page order and request normalization

A split source page is replaced in place by its tile sequence.

Example:

```text
source:
P0 P1 P2

P1 split 2×2:
P0 P1.00 P1.01 P1.10 P1.11 P2
```

Multiple request elements are normalized by original `page_index` before any write.

Therefore caller request ordering is not observable in output page order or output bytes.

Duplicate `page_index` requests are `QUANTAPDF_ERROR_ARGUMENT` even if they specify identical grids.

### 6.1 Final page count

Preflight computes:

```text
new_page_count = source_page_count
               + Σ(columns * rows - 1)
```

The calculation must reject overflow in:

- `columns * rows`;
- cumulative `size_t` arithmetic;
- conversion/limits required by MuPDF's `int` page-count APIs.

Final page count above `INT_MAX` is `QUANTAPDF_ERROR_ARGUMENT`.

## 7. Full-document immutable transform architecture

The changed path is:

```text
immutable source document
        ↓
strict source preflight
        ↓
complete logical split plan
        ↓
canonical deterministic source serialization
        ↓
fresh private MuPDF context
        ↓
open complete source bytes
        ↓
disable JavaScript
        ↓
security re-check
        ↓
rebuild complete private split plan
        ↓
prove source/private plan equivalence
        ↓
perform first private write
        ↓
create/move/remap tile structures
        ↓
deterministic full serialization
        ↓
immutable quantapdf_output
```

No source `pdf_obj *`, `pdf_page *`, MuPDF pointer identity, or source-context allocation may cross into the private context.

Private-plan equality is semantic. It is based on page indices, grid dimensions, strict page-box geometry, tile raw/public geometry, interactive classifications, destination classifications, and expansion-sensitive policy state—not object pointer equality across contexts.

If canonical source serialization changes any fact required to reproduce the source split plan, the changed path returns `QUANTAPDF_ERROR_FORMAT` before its first page-graph mutation.

## 8. Security policy

V1 keeps the same fail-closed rewrite security model as CropBox and MediaBox Trim:

- non-PDF input: `QUANTAPDF_ERROR_UNSUPPORTED`;
- encrypted PDF: `QUANTAPDF_ERROR_UNSUPPORTED`;
- already-signed PDF: `QUANTAPDF_ERROR_UNSUPPORTED`;
- JavaScript is disabled in the private context;
- no JavaScript/event/form-runtime execution is used to derive output;
- no validation, formatting, calculation, activation, or appearance-regeneration event is executed.

Signed documents are rejected even when the requested grid is `1×1`, matching the existing rewrite-layer security boundary.

Signed-document detection covers both signed AcroForm signature fields and
catalog `/Perms` signature entries such as `/DocMDP`, `/UR`, and `/UR3`.

## 9. Semantic no-op

A batch is an all-no-op batch when every request is `1×1`.

All-no-op behavior:

1. validate public arguments, page indices, duplicate-page rules, strict page-box state, PDF/security boundary, and overflow safety;
2. do not require expansion-only structures to be transformable, because no page identity or page count changes;
3. serialize the complete source once through the existing deterministic serializer;
4. return a new immutable output;
5. repeated calls are byte-identical under the existing canonical serialization contract.

Thus a `1×1` request can canonicalize a page that would be `UNSUPPORTED` for an actual `2×1` or larger expansion because of tagged-PDF, print-production, or page-identity-sensitive state.

For a mixed batch, expansion-only checks apply to every page with `columns * rows > 1`, plus document-root structures whose semantics are affected by any page-count change.

## 10. Tile page dictionary construction

Poster Split MUST NOT copy the complete source page dictionary into every tile.

A source `/Page` dictionary contains keys whose meaning is tied to one page identity. Blind duplication could create invalid `/Parent`, structure-tree, action, thread, thumbnail, print-production, or private-data relationships.

V1 constructs a new tile `/Page` from an explicit allowlist.

### 10.1 Required/generated tile keys

Each tile receives/generated:

```text
/Type      /Page
/MediaBox  tile_pdf
/CropBox   tile_pdf
/Parent    assigned by page-tree insertion
/Annots    rebuilt only when the tile has retained/cloned annotations
```

### 10.2 Shared safe page state

V1 may share the source page's existing same-document objects for:

```text
/Contents
/Resources     using the effective inherited Resources value when present
/Group
/Tabs
```

`/Rotate` is materialized from the source effective Rotate when non-zero or when required for deterministic reproduction of the source transform.

`/UserUnit` follows the established page-local rule and is copied/materialized when source page semantics require a non-default value.

The implementation must not rewrite content streams or resource objects to make them tile-specific.

### 10.3 Expansion-sensitive page keys rejected by V1

For a non-no-op split, the selected source page is `QUANTAPDF_ERROR_UNSUPPORTED` if it has/effectively supplies any of:

```text
/BleedBox
/TrimBox
/ArtBox
/AA
/Trans
/Dur
/StructParents
/Thumb
/B
/Metadata
/PieceInfo
/SeparationInfo
/VP
/PresSteps
```

This is intentionally conservative.

V1 does not silently drop those keys and does not copy them to every tile without a feature-specific semantic contract.

### 10.4 Unknown selected-page keys

After accounting for standard structural keys, the safe-share allowlist, the explicit page-box inputs, and `/Annots`, an unrecognized extra key on a selected source page is `QUANTAPDF_ERROR_UNSUPPORTED` for a real expansion.

This prevents extension/private page state from being silently lost or replicated with unknown semantics.

## 11. Content and render semantics

Tile pages share the source page's content/resource graph.

V1 does not rewrite:

- content-stream operators;
- text matrices;
- image matrices;
- graphics-state coordinates;
- Form XObject coordinates;
- font/resources;
- image streams;
- page content clipping operators.

The tile's page boxes and resulting page transform provide the visible subdivision.

A required deterministic characterization is:

```text
render(source page clipped to tile_public)
    ==
render(output tile page)
```

for a fixture designed to avoid unrelated renderer nondeterminism.

## 12. `/Annots` structural preflight

A real split must strictly inspect the selected page's `/Annots` because page membership changes.

If `/Annots` is present it must be a valid array for V1 processing.

Every entry that V1 retains/moves/clones must be a valid annotation dictionary with a finite positive-area `/Rect` sufficient to classify tile membership.

The transform may be stricter than immutable annotation enumeration because Poster Split must rewrite membership correctly; a structure that can be safely ignored by read-only enumeration is not automatically safe to rewrite.

Each selected-page annotation is classified as:

```text
Link
Widget
Popup / relational annotation
ordinary annotation
unknown/unsupported annotation form
```

Tile `/Annots` arrays preserve source annotation ordering: each output annotation or link clone occupies the source annotation's relative ordinal among annotations that appear on that tile.

## 13. Tile-membership rule for rectangles

Annotation/link membership is determined in source public Fitz page space.

The raw PDF `/Rect` is normalized, mapped by the source `pdf_to_public`, and normalized again to a finite positive-area public rectangle.

A positive-area rectangle is fully contained in one tile when its interior does not cross a grid edge.

Boundary-only contact does not count as crossing.

Examples for one vertical boundary at `x = edge`:

```text
[x0, edge]      -> left tile when x0 < edge
[edge, x1]      -> right tile when x1 > edge
[x0, x1] with x0 < edge < x1 -> crossing
```

The same rule applies on y boundaries.

## 14. Link annotations

Links are the one annotation class V1 may deliberately clone across tile boundaries.

### 14.1 Supported selected-page Link semantics

A selected-page Link must use one of:

```text
/Dest <supported local destination>
/A << /S /GoTo /D <supported local destination> >>
/A << /S /URI ... >>
```

Other Link action kinds are `QUANTAPDF_ERROR_UNSUPPORTED` for a real split.

Selected-page Link `/AA` is `QUANTAPDF_ERROR_UNSUPPORTED`.

### 14.2 Fully contained Link

If the Link rectangle is fully contained in exactly one tile:

- move the same Link annotation object to that tile;
- preserve its `/Rect` raw value;
- preserve its URI/action/destination structure except for destination remap when the target page is itself split;
- update `/P` if `/P` was present and pointed at the source page.

### 14.3 Crossing Link

If a Link crosses one or more grid boundaries:

1. intersect its public hotspot with every tile;
2. retain only positive-area intersections;
3. the first intersecting tile in row-major order receives the original Link object;
4. remaining intersecting tiles receive shallow same-document dictionary clones whose referenced action/destination/resource objects remain shared unless that specific object is part of destination remapping;
5. each output Link receives a tile-clipped `/Rect` mapped back to PDF user space;
6. the original Link's `/Rect` is updated to its first-tile clipped rectangle;
7. each clone receives `/P` pointing to its tile page; the original `/P` is updated when present.

A source Link whose rectangle has no positive-area intersection with the source visible split domain is `QUANTAPDF_ERROR_UNSUPPORTED`; V1 does not silently drop an invisible/out-of-domain Link object.

Clipping portions outside the source visible domain is part of the defined split semantics: only visible tile intersections become output hotspots.

### 14.4 URI preservation

URI bytes/string semantics are preserved by keeping the source URI/action object state. Poster Split performs no URI normalization.

## 15. Ordinary annotations

V1 deliberately does not invent cloning semantics for ordinary annotations.

An ordinary annotation is supported only when:

- its subtype is recognized by QuantaPDF's existing annotation model or explicitly accepted by Poster Split preflight;
- its finite positive `/Rect` is fully contained in exactly one tile;
- it has no `/Popup`, `/IRT`, `/RT`, or other relation that makes its identity depend on a second annotation object;
- it has no `/A` or `/AA` action container whose page-reference semantics are outside the supported destination registry.

Supported ordinary annotation behavior:

- move the same annotation object into the containing tile `/Annots` array;
- preserve all annotation-specific state and appearance streams;
- preserve the raw `/Rect`;
- update `/P` only when `/P` was present and pointed at the source page.

If the rectangle crosses a tile boundary, return `QUANTAPDF_ERROR_UNSUPPORTED`.

If the rectangle has no containing tile because it lies entirely outside the source visible split domain, return `QUANTAPDF_ERROR_UNSUPPORTED`.

Popup annotations and annotation-relation graphs are not supported in V1.

## 16. Widgets and AcroForm

Poster Split preserves an existing Widget by moving the same Widget annotation object, never by silently creating additional Widgets for the field.

A Widget on a selected page is supported only when:

- its `/Rect` is finite and positive;
- it is fully contained in one tile;
- its field/Widget relationship is accepted by the existing strict AcroForm model needed to identify that Widget;
- its page membership is consistent with the selected source page;
- it has no unsupported action/event state requiring page-sensitive behavior.

Supported Widget behavior:

```text
same Widget object
same field object
same field tree /Kids relationship
same /V and option state
same appearance streams
same field flags
same Widget flags
```

Only page-local membership changes:

```text
source page /Annots -> tile page /Annots
Widget /P           -> tile page when /P is present/required
```

No field is cloned.

No Widget is cloned.

A Widget crossing a tile boundary returns `QUANTAPDF_ERROR_UNSUPPORTED`.

Poster Split does not call form recalculation, formatting, validation, focus/blur events, action execution, appearance regeneration, or `pdf_update_page` as a semantic repair mechanism.

## 17. Document-wide destination remapping

Page-count expansion changes the set of page objects in the page tree. Any supported internal destination that targets an original split page must target exactly one resulting tile page before the original page is removed.

This is a document-wide concern, not only a selected-page concern.

V1 preflights supported internal destinations in:

```text
Link /Dest
Link /A /GoTo /D
Outline /Dest
Outline /A /GoTo /D
Names/Dests name tree destination definitions
legacy catalog /Dests dictionary destination definitions
```

Links on unselected pages are included because they may target a selected page.

Outline and named-destination structures are included regardless of which page owns the visible source of the navigation.

### 17.1 Supported target form

For a destination whose resolved target page is split, V1 supports only an explicit point-like `/XYZ` target with both x and y coordinates present as finite numbers.

The zoom operand may be null or numeric and is preserved unchanged.

For a destination targeting a split page:

- `/Fit`, `/FitB`, `/FitH`, `/FitBH`, `/FitV`, `/FitBV`, `/FitR`, or other destination kinds are `QUANTAPDF_ERROR_UNSUPPORTED` in V1;
- `/XYZ` with missing/null x or y is `QUANTAPDF_ERROR_UNSUPPORTED` because the tile cannot be selected uniquely by the V1 rule;
- malformed destination arrays/types are `QUANTAPDF_ERROR_FORMAT`.

Destinations targeting pages that are not split are preserved without normalization.

### 17.2 Tile selection for `/XYZ`

The `/XYZ` x/y coordinates are interpreted in PDF page user space.

The target tile is the unique `tile_pdf` containing the destination point under this deterministic ownership rule:

```text
[x0, x1) × [y0, y1)
```

except:

- the final column includes the source visible right edge;
- the final row includes the source visible final edge in the corresponding raw partition.

If the `/XYZ` point lies outside the source effective visible split domain, the transform returns `QUANTAPDF_ERROR_UNSUPPORTED` rather than clamping it to a tile.

### 17.3 Destination rewrite

For a supported destination targeting a split page:

- replace only the destination page reference with the selected tile page reference;
- preserve x, y, zoom and other already-supported operands unchanged in raw PDF user space.

The new tile page transform makes the public destination observation tile-local after reopen.

No per-destination coordinate translation is applied.

### 17.4 Named destinations

If a Link or Outline refers to a named destination, the referring name/string is preserved.

The corresponding Names/Dests or legacy `/Dests` definition is rewritten once when it targets a split page.

Shared destination definitions remain shared.

## 18. Unsupported destination/action containers

Poster Split V1 does not claim to rewrite arbitrary actions anywhere in the PDF object graph.

For any real page expansion, V1 requires these document-root action/index structures to be absent unless a later spec explicitly adds support:

```text
Catalog /OpenAction
Catalog /AA
Catalog /PageLabels
Catalog /Threads
```

Similarly, annotation/Widget action containers outside the supported Link URI/GoTo model are `QUANTAPDF_ERROR_UNSUPPORTED` when they occur on a selected page whose object must move.

This fail-closed rule prevents an internal GoTo reference from silently remaining pointed at a page object that is no longer in the page tree.

## 19. Tagged PDF / structure tree

A real Poster Split changes one page identity into multiple page identities.

Correct tagged-PDF support would require explicit semantics for:

- `/StructParents`;
- ParentTree mappings;
- marked-content identifiers;
- structure elements whose page association uses `/Pg`;
- content association across duplicated tile pages.

V1 does not attempt this.

If the catalog contains `/StructTreeRoot`, any batch containing a real split (`columns * rows > 1`) returns `QUANTAPDF_ERROR_UNSUPPORTED`.

Selected page `/StructParents` is independently unsupported.

The all-`1×1` no-op path does not reject tagged PDFs solely because no page identity changes.

## 20. Print-production page boxes

A real split is `QUANTAPDF_ERROR_UNSUPPORTED` when the selected source page has an effective/local/inherited:

```text
BleedBox
TrimBox
ArtBox
```

These boxes encode whole-page print-production intent. V1 does not guess whether each tile should inherit, intersect, synthesize, or offset them.

This also prevents Poster Split V1 from pre-empting the later design of overlap/bleed-aware poster output.

The all-`1×1` no-op path may canonicalize such documents because it does not create tile pages.

## 21. Page-tree mutation order

The private writer must preserve deterministic original document order and avoid index-shift ambiguity.

One valid implementation strategy is:

1. build all plans using original source page indices;
2. create all required tile page objects while source pages still exist;
3. retain logical source-page identity through plan-owned private references only inside the private context;
4. remap supported destinations while both original and tile page objects exist;
5. replace selected source pages in descending original page-index order or via another proven index-stable sequence;
6. insert each tile sequence row-major at the source page's position;
7. remove the original source page from the page tree.

The exact internal ordering is not public, but output page order is normative and private mutation must never depend on already-shifted caller indices.

## 22. Selected page `/Annots` reconstruction

For every selected source page with a real split:

- construct tile `/Annots` arrays from the preflight classification;
- do not copy the source `/Annots` array wholesale;
- each moved ordinary annotation/Widget occurs on exactly one tile;
- each Link occurs on one or more tiles according to its hotspot intersections;
- source relative annotation order is preserved independently on each tile;
- omit `/Annots` on a tile when that tile has no retained/cloned annotations unless a deterministic serializer/implementation requirement justifies an empty array.

No annotation object may remain referenced by the removed source page after successful replacement.

## 23. Unselected pages

Unselected page dictionaries, contents, resources, annotations, Widgets, and page boxes remain structurally unchanged except where a supported Link destination on an unselected page must be remapped because its target source page was split.

Their page numbers may change naturally because earlier pages expanded; references to their page objects remain valid without rewriting merely for numeric index movement.

## 24. AcroForm document-root preservation

Poster Split does not rebuild the AcroForm field tree.

For supported Widgets:

- the same Widget objects remain referenced by the same field tree;
- field values remain unchanged;
- `/CO`, `/DR`, `/DA`, `/NeedAppearances`, field hierarchy, options and non-page relationships remain untouched;
- only Widget page membership is updated when its source page is split.

If preflight cannot prove this invariant for a selected-page Widget, the real split is `QUANTAPDF_ERROR_UNSUPPORTED` or `QUANTAPDF_ERROR_FORMAT` according to whether the source is valid-but-out-of-scope or malformed.

## 25. Outline preservation

Outline hierarchy, titles, open/closed state, URI destinations, and unrelated internal destinations remain unchanged.

For supported `/XYZ` outline destinations targeting split pages, only the target page reference changes to the selected tile.

The existing strict outline structure rules remain authoritative for any outline graph Poster Split must rewrite.

Poster Split must not rely on MuPDF repair of malformed outline structure as part of the transform.

## 26. Metadata and unrelated document-root state

Document Info metadata and unrelated catalog state remain in the complete private graph.

Poster Split does not synthesize or normalize metadata merely because page count changes.

Root structures explicitly listed as unsupported for real expansion are rejected before the first private write rather than silently dropped.

## 27. Preflight atomicity

All source requests are validated before source serialization/private mutation.

A batch such as:

```text
request 0: valid 2×2 split
request 1: invalid/crossing Widget or unsupported page state
```

must fail with:

```text
*out_output == NULL
source unchanged
```

No prefix output or partial split is ever published.

The private graph repeats the complete preflight before its first write and must produce an equivalent plan.

## 28. Determinism

Determinism includes:

- page order;
- tile edge computation;
- row-major tile order;
- boundary ownership;
- annotation/link order per tile;
- which crossing Link retains the original object (first intersecting tile row-major);
- clone order;
- destination tile selection;
- request-order normalization;
- full serializer options.

Repeated calls on the same source bytes and logically identical split request set must produce byte-identical outputs under the existing deterministic serializer contract.

## 29. Error model

### `QUANTAPDF_ERROR_ARGUMENT`

Use for caller/request errors including:

- null required pointers;
- zero `split_count`;
- too-small `struct_size`;
- invalid page index;
- duplicate page request;
- zero columns/rows;
- multiplication/page-count overflow;
- grid density that cannot produce positive finite tile rectangles.

### `QUANTAPDF_ERROR_FORMAT`

Use for malformed source state required by the transform, including:

- malformed MediaBox/CropBox/Rotate/UserUnit;
- malformed selected-page `/Annots` structures required for rewriting;
- malformed required annotation `/Rect`;
- malformed AcroForm relationship needed to move a selected Widget;
- malformed Outline/destination structures the transform must inspect;
- malformed supported destination array/operand types;
- private canonical reparse failing to reproduce a source plan fact.

### `QUANTAPDF_ERROR_UNSUPPORTED`

Use for valid-but-out-of-scope semantics including:

- non-PDF source;
- encrypted source;
- signed source;
- real split of tagged PDF;
- real split with unsupported catalog action/index structures;
- effective BleedBox/TrimBox/ArtBox on a selected split page;
- selected-page identity-sensitive/unknown keys outside the V1 tile allowlist;
- ordinary annotation crossing a grid boundary;
- Widget crossing a grid boundary;
- Popup/relation graphs not supported by V1;
- selected-page unsupported annotation/Widget action state;
- unsupported Link action kind;
- destination kind other than the supported `/XYZ` case when it targets a split page;
- `/XYZ` target lacking both finite x/y;
- destination point outside the split visible domain.

### Other errors

- allocation failure -> `QUANTAPDF_ERROR_NOMEM`;
- MuPDF exceptions map through the existing status mapper.

## 30. Required deterministic test fixture

The primary Poster Split fixture should be a small, hand-controlled, multi-page PDF that allows raw-object assertions without relying on object numbers across serialization.

At minimum it should contain:

- one 400×300 split candidate page;
- a second page used as a destination/source-control page;
- visible text spanning multiple tile regions;
- at least one image occurrence;
- one contained URI Link;
- one URI Link crossing a tile boundary;
- one internal Link to a point on the split page or from another page to the split page;
- one contained ordinary annotation;
- one contained Widget with an AcroForm field value;
- one outline entry targeting the split page with `/XYZ`;
- a named destination targeting the split page;
- enough unselected content to prove document-root and page-order preservation.

Separate fixtures should isolate crossing ordinary annotations, crossing Widgets, tagged PDF, print-production boxes, Rotate 90, UserUnit 2, malformed destinations, and page-edge destination ownership.

## 31. Required observations

The new `quantapdf.pdf_poster_split` CTest must lock at least these behaviors.

### 31.1 Basic 2×2 geometry

For a deterministic 400×300 unrotated page:

```text
2 columns × 2 rows
-> four pages
-> each 200×150 public visible bounds
-> row-major order
```

### 31.2 Render equivalence

For every tile:

```text
source clipped render(tile_public)
==
output tile render
```

### 31.3 In-place document order

Splitting the middle page of a three-page document inserts tiles at that exact logical position and leaves surrounding page order intact.

### 31.4 No-op

A repeated all-`1×1` call returns deterministic canonical bytes and does not materialize a tile graph.

### 31.5 Rotate 90

Grid ordering remains visually top-to-bottom/left-to-right in source public space and output tiles reopen with correct tile-local page bounds.

### 31.6 UserUnit 2

Tile geometry and content observations use the established page-transform model; no manual UserUnit geometry rewrite exists.

### 31.7 Contained URI Link

The same logical URI and hotspot appear on exactly one output tile with tile-local public geometry.

### 31.8 Crossing URI Link

A source hotspot crossing a boundary produces deterministic clipped Link instances on every positive-area intersecting tile and nowhere else.

### 31.9 Internal Link remap

An internal `/XYZ` target on a split page resolves after reopen to:

- the correct new tile page;
- the correct tile-local public target point.

### 31.10 Outline remap

Outline hierarchy/title remains unchanged while its supported target points to the correct tile page/local point.

### 31.11 Named destination remap

A reference through Names/Dests or legacy `/Dests` preserves the referring name and resolves to the correct tile.

### 31.12 Grid-edge destination

A destination exactly on a tile boundary follows the specified deterministic half-open/final-edge rule.

### 31.13 Ordinary annotation contained

The annotation survives on exactly one tile with contents/flags/appearance-related raw state preserved and tile-local public bounds derived from the new page frame.

### 31.14 Ordinary annotation crossing

Returns `QUANTAPDF_ERROR_UNSUPPORTED` with `*out_output == NULL`.

### 31.15 Widget contained

The same field/value semantics remain; Widget count remains one; Widget page index changes to the correct tile page; no appearance regeneration occurs.

### 31.16 Widget crossing

Returns `QUANTAPDF_ERROR_UNSUPPORTED`.

### 31.17 Unrelated AcroForm state

Fields/Widgets on unsplit pages remain structurally/semantically unchanged except for natural page-number shifts.

### 31.18 Print-production boxes

A real split of a page with effective BleedBox/TrimBox/ArtBox is `UNSUPPORTED`.

### 31.19 Tagged PDF

A real split when catalog `/StructTreeRoot` is present is `UNSUPPORTED`; a pure `1×1` no-op remains permitted subject to normal security/page-box validation.

### 31.20 Root index/action state

Real split with unsupported `/PageLabels`, `/Threads`, `/OpenAction`, or catalog `/AA` is `UNSUPPORTED`.

### 31.21 Security

Encrypted and already-signed PDFs are `UNSUPPORTED` and reset the output sentinel.

### 31.22 Malformed source structures

Malformed page-box, selected `/Annots`, required Widget/form relation, Outline, or supported destination state returns the documented deterministic error without partial publication.

### 31.23 Batch validation

Cover:

- duplicate page request;
- zero rows/columns;
- overflow;
- early valid + late invalid request;
- two different selected pages with different grids;
- caller request order permutations produce identical bytes.

### 31.24 Source immutability

After success and after every failure class, the source document retains its original page count, page bounds, text/link/annotation/form observations, and output-independent lifetime.

### 31.25 Output lifetime

Successful output remains reopenable and fully usable after the source document is closed.

## 32. Raw-graph verification

The tests may use MuPDF privately to prove raw preservation, but public ABI remains MuPDF-free.

Raw assertions should avoid assuming stable object numbers after deterministic serialization.

Prefer semantic/raw comparisons of:

- source vs output Contents/Resources/Group relationships;
- tile MediaBox/CropBox raw values;
- source annotation field state;
- Widget/field relationships;
- AcroForm field values;
- supported destination arrays/page targets;
- outline hierarchy/destination semantics;
- absence/presence of unsupported/safe page keys.

## 33. CTest boundary

Integrated baseline after MediaBox Trim V1: **23 CTests**.

Add one target:

```text
quantapdf.pdf_poster_split
```

Final suite target:

```text
24 CTests
```

The first strict RED must be attributable only to the absent approved public Poster Split ABI while existing 23 targets remain green.

## 34. Verification / integration gates

Execution follows the established project discipline:

```text
committed spec
    ↓
committed implementation plan
    ↓
strict compile RED
    ↓
ABI shell / runtime RED
    ↓
minimal architecture-preserving GREEN
    ↓
Linux static 24/24
    ↓
Linux ASan/UBSan 24/24
    ↓
freeze exact feature SHA
    ↓
same-SHA Linux/macOS/Windows full-ci
    ↓
Critical/Important review + scope audit
    ↓
STOP
    ↓
explicit integration authorization
    ↓
merge exact frozen feature
    ↓
integrated-master Linux/macOS/Windows 24/24
    ↓
close #53 / update #48 and #2
```

No integration is authorized by implementation approval alone.

## 35. Explicit non-goals

Poster Split V1 does not include:

- overlap;
- gutter;
- printer bleed;
- target paper size;
- automatic page rotation;
- content scaling;
- rasterization;
- generic content-stream translation;
- page-content rewriting;
- arbitrary ordinary-annotation cloning;
- Popup/IRT relation migration;
- Widget cloning;
- form-field duplication;
- tagged-PDF structure-tree rewrite;
- general arbitrary-action graph rewrite;
- destination types beyond the V1 targetable `/XYZ` split-page case;
- page labels/thread rewrite;
- thumbnail regeneration;
- page metadata duplication policy;
- flatten/bake;
- optimize/gc;
- image recompression;
- encryption/decryption/re-encryption;
- incremental-save compatibility;
- signature-preserving rewrite;
- persistent public object IDs;
- concurrency redesign.

## 36. Architectural checkpoint after V1

If Poster Split V1 integrates with this contract, Phase 6 has three coherent primitives:

```text
CropBox
    visible clipping

MediaBox
    physical medium

Poster Split
    deterministic page multiplicity
    + tile-local page frames
    + explicitly bounded interactive migration
```

Flatten/bake can then build on a page graph whose geometry and page-multiplicity semantics have both been established without relying on silent MuPDF graft loss or per-object content translation.
