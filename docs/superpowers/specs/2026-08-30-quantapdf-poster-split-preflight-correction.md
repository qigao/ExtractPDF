# QuantaPDF Poster Split V1 Destination / Action Preflight Correction

Issue: #53  
Normative amendment to: `docs/superpowers/specs/2026-08-30-quantapdf-poster-split-design.md`
Baseline master: `eca8179e59723e2e6dfd3b3acbdedc61c15bf350`  
MuPDF baseline: 1.28.2

## Status

This correction was discovered during design-spec self-review, before implementation planning, RED tests, public ABI changes, or production code.

It does not change the approved public API or user-visible Poster Split model. It removes two implementation ambiguities that would otherwise make Rotate 90/270 destinations or hidden page-reference actions unsafe.

Where this document conflicts with the original Poster Split design, this document is normative.

## 1. Correction A: destination tile ownership is decided in source public space

### 1.1 Rejected raw-space rule

The original draft says a split-page `/XYZ` destination may select a tile by applying a half-open rule directly to normalized `tile_pdf` rectangles.

That rule is insufficient for rotated pages.

Poster Split defines row/column order in source QuantaPDF/Fitz public page space. Under Rotate 90 or 270, public row/column boundaries map to different raw PDF axes/directions. A rule such as "the final raw row includes the final edge" is therefore not a stable definition of visual row-major ownership.

### 1.2 Correct invariant

For a supported `/XYZ` destination targeting a split page:

```text
raw PDF destination point (x, y)
    -- source pdf_to_public -->
source public destination point
    -- public x[] / y[] edge arrays -->
unique row-major tile
```

The same public edge arrays used to construct the Poster Split grid are the sole tile-ownership authority.

Boundary ownership is:

```text
[x[col], x[col + 1])
[y[row], y[row + 1])
```

with the final column including `visible_public.x1` and the final row including `visible_public.y1`.

This rule is independent of Rotate and UserUnit because those are already represented by `pdf_to_public`.

If the transformed public destination point lies outside `visible_public`, the real split is `QUANTAPDF_ERROR_UNSUPPORTED`.

### 1.3 Destination rewrite remains raw-coordinate preserving

After selecting the tile:

- replace only the destination page reference with the selected tile page reference;
- preserve raw PDF `/XYZ` x, y, and zoom operands unchanged;
- do not subtract the tile origin from raw destination coordinates;
- let the new tile page transform produce the tile-local public target after reopen.

Therefore the final invariant is:

```text
page selection changes
raw destination coordinates do not
public destination coordinates become tile-local through page-frame change
```

## 2. Correction B: action safety is document-wide, not selected-page-only

A page-count-changing transform can invalidate an internal GoTo destination even when the action that owns the destination is on an unselected page or in a document-root/form structure.

Therefore a real Poster Split cannot validate only selected-page annotations.

The changed path MUST perform a document-wide navigation/action safety preflight before the first private write.

## 3. Global `/Annots` navigation scan

When any request has `columns * rows > 1`, Poster Split scans `/Annots` on every page sufficiently to prove that no unsupported action container can retain a reference to an original split page.

For this global navigation scan:

- absent `/Annots` is allowed;
- present `/Annots` must be an array;
- every entry needed for action classification must be a valid indirect annotation dictionary;
- malformed arrays/entries that prevent classification are `QUANTAPDF_ERROR_FORMAT`.

Selected split pages still receive the stricter migration preflight from the main design: every retained/moved/cloned annotation needs a finite positive `/Rect` and a supported migration classification.

### 3.1 Annotation subtype classification

For selected-page migration:

- missing/non-name `/Subtype` is `FORMAT`;
- an unknown but syntactically valid annotation subtype is `UNSUPPORTED`;
- Popup and unsupported relation graphs are `UNSUPPORTED`;
- Link and Widget follow their dedicated policies;
- supported ordinary annotations follow the containment-only move policy.

## 4. Link actions on every page

For a real split, every Link annotation in the document must have action/destination state that can be proven safe.

V1 accepts:

```text
/Dest <local destination>
/A << /S /GoTo /D <local destination> >>
/A << /S /URI ... >>
```

subject to the destination rules in the main spec and Correction A.

V1 rejects as `UNSUPPORTED`:

- Link `/AA`;
- action `/Next` chains;
- Link action kinds outside local GoTo and URI.

Links on unselected pages are not moved or geometrically changed, but a supported local GoTo destination targeting a split page is remapped to the correct tile.

This explicitly locks the required observation:

```text
unselected source page Link
    -> target source page is split
    -> Link remains on original source page object
    -> destination page ref changes to target tile
    -> public target page/point resolves correctly after reopen
```

## 5. Non-Link annotation actions

Poster Split V1 does not implement a generic arbitrary-action graph rewriter.

For any real split, an annotation that is not a Link and contains `/A` or `/AA` anywhere in the document is `QUANTAPDF_ERROR_UNSUPPORTED`.

This rule applies to selected and unselected pages.

The selected-page ordinary-annotation and Widget migration rules remain otherwise unchanged.

## 6. Page additional actions

For any real split, page `/AA` is unsupported on **any page in the document**, not only on a selected page.

Reason: an unselected page action can contain a local GoTo target to a page being replaced.

Selected-page `/Trans` and `/Dur` remain expansion-sensitive page-identity state as specified by the main design; unselected `/Trans` and `/Dur` are not rejected solely because another page is split.

## 7. AcroForm action/XFA safety

AcroForm field and Widget relationships may remain structurally intact only if Poster Split does not leave an unscanned page-reference action graph inside the form model.

For any real split:

- catalog `/AcroForm/XFA` present -> `QUANTAPDF_ERROR_UNSUPPORTED`;
- any AcroForm field dictionary with `/A` or `/AA` -> `QUANTAPDF_ERROR_UNSUPPORTED`;
- any Widget with `/A` or `/AA` -> `QUANTAPDF_ERROR_UNSUPPORTED`.

This applies document-wide, including Widgets on unselected pages.

The existing supported Widget move still preserves field/value/appearance state when no unsupported action state is present.

## 8. Outline action safety

For a real split, the Outline graph is structurally preflighted using the existing strict outline rules.

V1 accepts Outline navigation state only when it is:

- `/Dest` resolving to a supported local destination;
- `/A /GoTo /D` resolving to a supported local destination;
- a URI destination/action that requires no local page remap;
- no destination/action.

Other action kinds or `/Next` action chains are `QUANTAPDF_ERROR_UNSUPPORTED`.

A named local destination remains a name/string at the referring Outline item; only its Names/Dests or legacy `/Dests` definition is rewritten.

## 9. Catalog action/index surfaces

The main design remains normative that any real split requires these to be absent:

```text
Catalog /OpenAction
Catalog /AA
Catalog /PageLabels
Catalog /Threads
```

`/StructTreeRoot` remains `UNSUPPORTED` for a real split under the tagged-PDF rule.

Other unrelated catalog Names entries remain preserved. The Dests subtree is inspected only because local destinations targeting split pages require remapping.

## 10. Destination registry after correction

For a real split, the supported local-destination registry is exactly:

```text
Link /Dest
Link /A /GoTo /D
Outline /Dest
Outline /A /GoTo /D
Names/Dests destination definitions
legacy Catalog /Dests destination definitions
```

The registry is scanned document-wide.

A destination that targets an unsplit page is preserved without normalization.

A destination that targets a split page must resolve to explicit `/XYZ` with finite numeric x and y; otherwise it is `UNSUPPORTED`.

Malformed registry structures are `FORMAT`.

## 11. Private-plan equivalence additions

Source/private semantic plan equivalence must now also include:

- document-wide page `/AA` presence classification;
- global annotation action classification;
- Link action/destination classification on all pages;
- AcroForm XFA/action-presence classification;
- Outline action/destination classification;
- Names/Dests and legacy `/Dests` logical destination targets;
- every split-target `/XYZ` source public point;
- the row/column tile selected by the public-grid ownership rule.

No source object pointer identity crosses contexts.

## 12. Error-model clarification

For a real split:

### `QUANTAPDF_ERROR_FORMAT`

Includes:

- malformed `/Annots` array on any page when the global navigation scan cannot classify it;
- malformed annotation entry needed for action classification;
- malformed Outline/destination registry structures;
- malformed supported `/XYZ` operands.

### `QUANTAPDF_ERROR_UNSUPPORTED`

Includes:

- page `/AA` anywhere in the document;
- non-Link annotation `/A` or `/AA` anywhere;
- Link `/AA` or `/Next`;
- unsupported Link action kind anywhere;
- AcroForm `/XFA`;
- AcroForm field/Widget `/A` or `/AA` anywhere;
- unsupported Outline action kind or action `/Next`;
- split-target destination kind other than explicit finite-x/y `/XYZ`;
- split-target public destination point outside the source visible split domain.

## 13. Additional required tests

The final `quantapdf.pdf_poster_split` characterization must explicitly add:

1. Rotate 90 destination exactly on a public grid boundary chooses the correct visual row-major tile.
2. UserUnit destination tile selection is performed through `pdf_to_public` rather than a separate manual scale rule.
3. Link on an unselected page targeting a split page remaps to the correct output tile.
4. page `/AA` on an unselected page -> `UNSUPPORTED` for a real split.
5. non-Link annotation action on an unselected page -> `UNSUPPORTED`.
6. AcroForm field/Widget action anywhere -> `UNSUPPORTED`.
7. AcroForm `/XFA` -> `UNSUPPORTED`.
8. malformed `/Annots` on an unselected page -> `FORMAT` when a real split requires the global navigation scan.
9. pure all-`1×1` no-op does not invoke these expansion-only action restrictions, while still honoring PDF/security/page-box validation.

## 14. Final corrected invariant

Poster Split V1 now has one consistent coordinate and navigation model:

```text
source public page grid
    = tile geometry authority
    = row-major authority
    = rectangle migration authority
    = destination tile-ownership authority

raw PDF coordinates
    = preserved content/annotation/destination geometry storage

page references
    = explicitly remapped when source page identity expands

unscanned action graphs
    = fail closed
```

This keeps Rotate/UserUnit behavior inside the already-proven page transform and prevents page-count expansion from leaving hidden internal navigation pointed at removed source pages.
