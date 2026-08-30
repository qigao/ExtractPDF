# ExtractPDF Interactive Flatten / Bake V1 Design

Date: 2026-08-30
Status: committed design candidate
Issue: #55
Umbrella: #48 / #2
Baseline: `master@be28add194e98ccfa7b1ab613a8b284782011cf1`
Baseline tree: `3ca169114f44c5d7c7569e58c573fd621993fd01`
Baseline tests: 24 CTests

## 1. Purpose

Flatten / Bake V1 is the first ExtractPDF transform that intentionally adds ordinary page content.

Its job is narrow:

1. resolve an already-materialized current normal appearance for each selected interactive object;
2. place that existing Form XObject into the page content at the same PDF-page location;
3. remove the corresponding interactive object;
4. when Widgets are flattened, remove only the affected Widget provenance from the AcroForm field tree;
5. publish a new immutable deterministic `extractpdf_output`.

Flatten does not synthesize an appearance, interpret a form value into drawing operators, execute JavaScript, recalculate a form, apply redaction, rasterize a page, garbage-collect unreachable objects, recompress images, or change encryption.

The visual source of truth is the already-present current `/AP /N` appearance.

## 2. Public API

```c
typedef enum extractpdf_flatten_flag {
    EXTRACTPDF_FLATTEN_ANNOTATIONS = 1u << 0,
    EXTRACTPDF_FLATTEN_WIDGETS = 1u << 1
} extractpdf_flatten_flag;

EXTRACTPDF_API extractpdf_status extractpdf_flatten_interactive(
    extractpdf_document *document,
    uint32_t flags,
    extractpdf_output **out_output);
```

Rules:

- `document == NULL` or `out_output == NULL` -> `EXTRACTPDF_ERROR_ARGUMENT`.
- `flags == 0` -> `EXTRACTPDF_ERROR_ARGUMENT`.
- unknown flag bits -> `EXTRACTPDF_ERROR_ARGUMENT`.
- every failure resets `*out_output = NULL`.
- source remains immutable.
- output owns its bytes and survives source close.
- V1 is whole-document; there are no page/object selectors.

The two flags compose. `ANNOTATIONS | WIDGETS` means flatten both supported ordinary annotations and all Widgets in one atomic whole-document transform.

## 3. Selection semantics

### 3.1 Ordinary annotation flag

`EXTRACTPDF_FLATTEN_ANNOTATIONS` means that every ordinary annotation encountered in the document must be either:

- a supported static-markup annotation that is successfully baked and removed; or
- an explicitly excluded structural class (`Link`, `Popup`, `Widget`) governed below.

V1 supported static-markup subtypes are:

- Text
- FreeText
- Line
- Square
- Circle
- Polygon
- PolyLine
- Highlight
- Underline
- Squiggly
- StrikeOut
- Stamp
- Caret
- Ink

The following ordinary/non-Widget classes are `EXTRACTPDF_ERROR_UNSUPPORTED` when annotation flattening is requested:

- Redact
- FileAttachment
- Sound
- Movie
- RichMedia
- Screen
- PrinterMark
- TrapNet
- Watermark
- 3D
- Projection
- unknown/unrecognized subtype

Rationale: those classes carry semantics that are not reducible to “current static normal appearance”. In particular, Redact must not be confused with actually applying redaction.

### 3.2 Links

Links are never selected by `EXTRACTPDF_FLATTEN_ANNOTATIONS` and remain interactive.

On a page that receives baked content, a remaining Link must be visually neutral:

- no normal appearance `/AP /N`; and
- effective border width is explicitly and strictly zero.

A Link with a normal appearance, a non-zero border, an ambiguous/default border state, or malformed border data is `UNSUPPORTED`/`FORMAT` as appropriate on a page that receives baked objects.

This prevents page-content baking from silently changing visual stacking against a visible Link.

### 3.3 Popups and reply relationships

Popup objects are never independently baked.

A selected annotation is `UNSUPPORTED` if it participates in a relationship whose semantics would be broken by deleting it, including either direction of:

- `/Popup`;
- Popup `/Parent`;
- `/IRT` reply relationships.

Preflight scans the whole document annotation graph for reverse references as well as fields on the selected object. It must not leave a surviving annotation referring to a removed annotation.

Unrelated Popups may remain unchanged.

### 3.4 Widget flag

`EXTRACTPDF_FLATTEN_WIDGETS` selects every Widget reconciled by the existing strict AcroForm model/provenance.

Every selected Widget must resolve to exactly one field and page through the existing two-stage provenance pipeline:

```text
extractpdf_pdf_form_build(..., want_provenance=1)
        -> strict field-tree/model reconciliation
extractpdf_pdf_form_capture_provenance_widgets(...)
        -> exact live Widget identity/page provenance
```

An orphan, duplicate, ambiguous, malformed, or unreconciled Widget is `FORMAT`.

## 4. Expansion-only restrictions and semantic no-op

Restrictions that exist only because an object is actually being baked apply only when at least one requested object exists.

The public call always performs:

1. argument validation;
2. PDF/security validation;
3. strict discovery of the requested class or classes.

If the requested classes contain zero selected objects after strict discovery, the operation is a semantic no-op:

- serialize the source canonically once through the existing deterministic serializer;
- do not create a private mutation graph;
- do not apply bake-only z-order/tagged/appearance restrictions to unrelated document state.

Examples:

- annotation flatten on a document containing only zero-border Links -> canonical no-op;
- Widget flatten on a valid AcroForm with no Widgets -> canonical no-op;
- malformed `/Annots` consumed by requested annotation discovery is still `FORMAT`;
- malformed AcroForm consumed by requested Widget discovery is still `FORMAT`.

Encrypted/signed security policy is checked before publication even on a no-op, matching the transform-layer fail-closed model.

## 5. Security and execution policy

Changed flatten operations use the established immutable transform substrate:

```text
immutable source
    -> strict complete source preflight
    -> canonical full serialization
    -> fresh private MuPDF context/document
    -> disable JavaScript
    -> security recheck
    -> rebuild complete flatten plan
    -> prove source/private semantic-plan equivalence
    -> resolve all private runtime object references
    -> first write
    -> deterministic full serialization
    -> immutable output
```

Rules:

- encrypted input -> `EXTRACTPDF_ERROR_UNSUPPORTED` in V1;
- already-signed input -> `EXTRACTPDF_ERROR_UNSUPPORTED`;
- Catalog `/StructTreeRoot` with any real bake -> `EXTRACTPDF_ERROR_UNSUPPORTED` because removing annotations/Widgets would require tagged-PDF structure/ParentTree maintenance;
- no JavaScript, action, form-event, validation, formatting, calculation, activation, or appearance-regeneration code may execute;
- no source `pdf_obj *`, `pdf_page *`, `pdf_annot *`, or other MuPDF identity crosses into the private context;
- mutation never uses the source document journal;
- any private mutation failure discards the private graph and leaves the source/output publication untouched.

## 6. Strict current normal appearance resolution

Flatten uses only the normal appearance (`/AP /N`) in the ordinary non-hot/non-active state.

For each selected annotation/Widget, preflight reads the raw annotation dictionary without asking MuPDF to synthesize or update appearances.

### 6.1 `/AP /N` direct stream case

`/AP` must be a dictionary when present.

If `/AP /N` resolves to an indirect stream object, that stream is the selected appearance.

The stream must be a valid Form XObject under the rules below.

### 6.2 `/AP /N` state dictionary case

If `/AP /N` is a state dictionary:

- `/AS` must be present as a name to select the current state;
- missing `/AS` is `UNSUPPORTED` because V1 refuses to guess a current visual state, even when only one state entry exists;
- non-name `/AS` is `FORMAT`;
- `/AS` must select an existing state entry;
- the selected entry must resolve to an indirect Form XObject stream;
- missing selected state, non-stream selected state, or malformed state dictionary is `FORMAT`.

The field `/V` is not used to select the visual appearance. A stale field value and a valid `/AS` intentionally flatten the currently materialized display state, not a recalculated state.

### 6.3 Missing appearance

A selected object with no usable `/AP /N` is `UNSUPPORTED`.

V1 never calls appearance synthesis/resynthesis APIs to turn this into success.

### 6.4 Form XObject requirements

The selected appearance stream must satisfy:

- indirect stream object;
- `/Subtype /Form`;
- strict `/BBox`: exactly four finite numbers, normalized positive non-zero area;
- optional `/Matrix`: exactly six finite numbers; absent means identity;
- optional `/Resources`, when present, must be a dictionary;
- any annotation-level or appearance-level optional-content dependency (`/OC`) is `UNSUPPORTED` in V1 because baking must not silently discard optional-content visibility semantics.

Malformed consumed objects -> `FORMAT`.

A structurally valid but degenerate/zero-area appearance that cannot be mapped uniquely -> `UNSUPPORTED`.

## 7. Annotation flags and visibility contract

Baking converts an annotation into ordinary page graphics. Some annotation flags describe viewer-dependent behavior that ordinary page content cannot preserve.

A selected object is therefore `UNSUPPORTED` when `/F` contains any of:

- Invisible;
- Hidden;
- NoZoom;
- NoRotate;
- NoView;
- ToggleNoView.

`/F` must otherwise parse through the existing strict raw `uint32_t` flag path.

ReadOnly/Locked/LockedContents and similar non-visual interaction flags do not block flattening because the interactive object is intentionally removed.

The Print bit is not reproduced as a page-content property. Once selected content is flattened, it becomes ordinary page graphics and follows ordinary page printing semantics. This is an explicit consequence of flattening, not an accidental preservation claim.

## 8. Coordinate invariant

Flatten placement is performed in **raw PDF page user space**, not public Fitz page space.

This is deliberate:

```text
appearance Form space
    -- Form /Matrix --> transformed appearance box
    -- bake placement --> page PDF user space
    -- existing page transform --> ExtractPDF/Fitz public space
```

Page Rotate/UserUnit/page-box behavior therefore applies to existing page contents and newly baked content through the same existing page transform. Flatten introduces no third coordinate model and does not rewrite page boxes.

### 8.1 Placement matrix

Let:

```text
B = normalized appearance /BBox
M = appearance /Matrix, identity if absent
R = normalized annotation/widget /Rect in PDF page user space
BM = transform_rect(B, M)
```

V1 rejects the viewer-dependent NoZoom/NoRotate cases, so the placement follows the flags-neutral MuPDF annotation transform model:

```text
w  = BM.x1 - BM.x0
h  = BM.y1 - BM.y0
sx = (R.x1 - R.x0) / w
sy = (R.y1 - R.y0) / h
tx = R.x0 - BM.x0 * sx
ty = R.y0 - BM.y0 * sy
T  = [ sx 0 0 sy tx ty ]
```

The bake stream applies `T` before invoking the same Form XObject. The Form XObject's own `/Matrix` remains on the Form and is applied by normal `Do` execution; it is not deleted or rewritten.

All intermediate/final values must be finite. Invalid numeric objects are `FORMAT`; a valid but non-invertible/degenerate appearance geometry is `UNSUPPORTED`.

A private `pdf_appearance_common` helper should own this raw resolver and placement math so later page-content operations do not duplicate it.

## 9. Page visual stacking / closure invariant

PDF page contents render before page annotations. Moving only one visual annotation class into page content can change z-order relative to another surviving visual annotation.

V1 therefore uses a strict **page visual-closure rule**.

For any page on which at least one object is baked:

- if only annotations are requested, any Widget on that same page makes the operation `UNSUPPORTED`;
- if only Widgets are requested, any ordinary annotation on that same page makes the operation `UNSUPPORTED`;
- if both are requested, all supported ordinary annotations and Widgets on that page are selected and ordered together by their original `/Annots` order;
- remaining Links must satisfy the visually-neutral Link rule;
- selected Popup/IRT relationships are rejected as defined above.

This intentionally prefers fail-closed behavior to bounding-box overlap heuristics. V1 does not try to prove that two surviving visual layers happen not to overlap.

The result is a simple invariant:

> Every page that receives a bake stream moves one closed visual annotation set into page content, preserving the selected set's original `/Annots` relative order, while any remaining page annotation is visually neutral for V1 purposes.

## 10. Bake stream and Resource strategy

The writer does not parse or rewrite existing page graphics operators.

Each changed page receives at most one new deterministic appended content stream.

For each selected target in original `/Annots` order:

```text
q
<placement matrix> cm
/<resource-name> Do
Q
```

### 10.1 Deterministic XObject aliases

Within one page:

- distinct selected appearance object identities are assigned aliases in first-use `/Annots` order;
- repeated use of the same appearance object reuses the same alias;
- aliases use the deterministic private prefix `/EPB0`, `/EPB1`, ...;
- if an alias already exists in the page's effective `/Resources /XObject`, skip deterministically to the next unused integer;
- source/private semantic plans compare the alias assignment inputs, not MuPDF pointer values.

### 10.2 Resource isolation

Never mutate an inherited/shared Resources or XObject dictionary in place.

For a changed page:

1. read the effective inherited `/Resources` strictly;
2. create a page-local shallow dictionary copy preserving all existing resource entries by reference;
3. create a private copy of the effective `/XObject` subdictionary, or create an empty one if absent;
4. add deterministic bake aliases pointing at the existing appearance Form XObjects;
5. write the local `/Resources` only on the changed page.

Malformed consumed Resources/XObject structures -> `FORMAT`.

No font/image/colorspace object is copied, decoded, regenerated, or rewritten by flattening.

### 10.3 Contents isolation

The new bake stream must be appended after existing page content because annotations originally render after page contents.

Writer rules:

- absent `/Contents`: set the new stream as the page contents;
- one valid content stream: replace page `/Contents` with a new array `[old, bake]`;
- valid content array: create a new local array preserving all existing entries in order and append the bake stream;
- never append into an existing shared/indirect Contents array in place;
- consumed malformed Contents shape/entries -> `FORMAT`.

The old content stream bytes remain untouched.

### 10.4 Number formatting

Bake-stream matrix operands are serialized by one private canonical finite-number formatter. The formatter must be locale-independent and proven byte-stable on Linux/macOS/Windows.

Do not rely on ambient locale-sensitive `printf` behavior without a pinned formatter contract.

## 11. Annotation removal

Before first write, private runtime resolution keeps exact private references for:

- each changed page;
- every selected annotation/Widget;
- every selected appearance Form XObject;
- AcroForm provenance needed for Widget pruning.

Writers never rediscover targets by mutable array index after removals begin.

For each changed page, construct a new `/Annots` array from the original array, preserving the identity and relative order of every kept object and omitting every selected baked object.

If no annotation remains, remove the page-local `/Annots` key instead of leaving an empty malformed/ambiguous shell.

Ordinary annotation objects are not explicitly deleted from xref. They become unreachable when no other legal relation points to them. Unreachable-object collection is #56, not #55.

## 12. Widget / AcroForm pruning

Widget flattening is not “remove `/Annots` and ignore the field tree”.

It must update the strict field provenance consistently.

### 12.1 NeedAppearances and XFA

When real Widget baking is requested:

- `/AcroForm /XFA` present -> `UNSUPPORTED`;
- `/NeedAppearances true` -> `UNSUPPORTED` because current appearance is not a stable authoritative display contract;
- malformed `/NeedAppearances` -> `FORMAT`;
- absent/false is accepted.

No appearance regeneration is triggered to satisfy NeedAppearances.

### 12.2 Widget identity

Every selected Widget is removed from page `/Annots` by exact private object identity and from field provenance using the exact strict field locator captured by the form model.

Support must include both PDF field layouts:

1. separate terminal field dictionary with Widget child/children;
2. merged terminal field+Widget dictionary appearing directly in the field tree and page `/Annots`.

No public snapshot index becomes persistent identity.

### 12.3 Field pruning rule

V1 is whole-document Widget flattening, but pre-existing field-only semantics must not be discarded accidentally.

Prune only field nodes that become removable because selected Widgets were removed:

- when a terminal field loses its last selected Widget and has no surviving field descendants, remove that terminal field from its parent `/Kids` or root `/Fields`;
- recursively remove an ancestor field node only when its `/Kids` becomes empty because of those removals and it has no independently surviving field semantics/descendants under the strict provenance model;
- a pre-existing valid field with zero Widgets is preserved if it was not made empty by Widget removal;
- unrelated field subtrees are byte/graph-semantically untouched.

The exact prune predicate must be derived from the strict form tree/provenance, not guessed from public field counts alone.

### 12.4 Calculation order

If `/AcroForm /CO` is present:

- it must be a structurally valid array of field references under the strict form model;
- references to fields removed by flattening are removed from `/CO` while preserving the relative order of surviving entries;
- malformed or unresolved entries -> `FORMAT`;
- no calculation is executed.

### 12.5 Removing AcroForm

After pruning:

- if at least one field remains, preserve `/AcroForm` and all unrelated keys unchanged except the necessary `/Fields`/`/CO` structural edits;
- if zero fields remain, remove Catalog `/AcroForm` entirely.

Do not leave a root AcroForm dictionary whose only former purpose was the removed Widget tree.

Field values are never converted to text operators; the selected appearance stream remains the sole visual source.

## 13. Source/private plan equivalence

The semantic plan must contain no source MuPDF pointers.

For each selected object it records enough context-independent facts to prove private canonical reparse equivalence, including:

- page index;
- original `/Annots` ordinal;
- selected class/subtype;
- strict raw Rect and flags;
- appearance selection mode and `/AS` state when applicable;
- strict BBox/Matrix/placement values;
- page resource alias assignment inputs;
- Widget field locator/provenance when applicable;
- relationship/z-order closure facts.

The private graph independently rebuilds the full plan before any write.

Any difference between source and private plan -> `FORMAT` and no write.

Private runtime references are resolved only after plan equivalence succeeds.

## 14. What flatten may write

Semantic writes are limited to changed pages and Widget field provenance.

Allowed page writes:

- page-local `/Resources` materialization/copy needed for bake XObject aliases;
- page-local `/Contents` replacement needed to append one bake stream;
- page-local `/Annots` replacement/removal.

Allowed selected Widget/form writes:

- field/root `/Kids` or `/Fields` pruning;
- `/CO` pruning;
- Catalog `/AcroForm` removal if no fields remain.

Allowed new objects:

- one bake content stream per changed page;
- new private page-local Resources/XObject dictionaries/arrays where required for isolation.

Forbidden writes include:

- page boxes, Rotate, UserUnit;
- existing content-stream bytes;
- existing appearance Form bytes/BBox/Matrix/Resources;
- image/font/colorspace streams;
- Link dictionaries;
- metadata/Info;
- Outline;
- Names/Dests;
- internal destination arrays;
- AcroForm field values/options/appearance generation;
- JavaScript/actions execution;
- encryption/signature dictionaries;
- object GC/deduplication.

## 15. Determinism and idempotence

For one source and one flag set:

```text
flatten(source, flags)
```

must be byte-identical across repeated calls under the existing deterministic writer contract.

Request flag order is irrelevant because flags are a set.

After a successful flatten, reopening the output and calling the same flatten operation again should be a semantic no-op for the requested classes and produce the canonical no-op serialization defined by the final implementation.

V1 does not require the second call's bytes to equal the first call until canonical serializer/idempotence behavior is proven; the required guarantee is deterministic repeated output for identical input bytes and flags. #56 owns stronger canonical rewrite idempotence.

## 16. Error classification

`ARGUMENT`:

- null required handles;
- zero/unknown flags.

`FORMAT`:

- malformed consumed annotation dictionaries/arrays;
- invalid Rect/F/AP structure types;
- malformed BBox/Matrix;
- invalid `/AS` type or broken selected state;
- malformed consumed Resources/Contents;
- malformed AcroForm/provenance/CO structures;
- source/private plan mismatch.

`UNSUPPORTED`:

- encrypted/signed changed transform;
- tagged PDF requiring structure-tree rewrite;
- selected subtype outside static-markup whitelist;
- missing normal appearance;
- missing `/AS` for stateful appearance where V1 refuses to infer state;
- degenerate valid appearance geometry;
- optional-content-dependent selected appearance;
- NoZoom/NoRotate/Invisible/Hidden/NoView/ToggleNoView selected object;
- selected Popup/IRT relationship;
- page visual-closure violation;
- visible/non-neutral Link on a changed page;
- XFA;
- NeedAppearances true.

`NOMEM`/MuPDF-mapped errors retain the existing kernel policy.

## 17. Private architecture

Recommended private decomposition:

```text
src/pdf_appearance_common.[ch]
    strict raw /AP /N selection
    Form BBox/Matrix validation
    raw PDF-page placement matrix
    read-only; no synthesis or writes

src/pdf_flatten_preflight.c
    whole-document selection
    annotation subtype/flags/relationship policy
    page visual-closure policy
    deterministic page/appearance plan

src/pdf_flatten_form.c
    strict Widget provenance adapter
    field-tree/CO prune plan + writer

src/pdf_flatten_bake.c
    page-local Resources/XObject isolation
    deterministic aliases
    canonical bake stream
    Contents append + Annots replacement

src/pdf_flatten.c
    public ABI/orchestration
    source canonicalization
    private reparse/revalidation
    plan equivalence
    deterministic serialization
```

Do not restructure the repository into a new `kernel/` hierarchy in this slice. Reuse current common modules and extract only helpers that #55 genuinely proves reusable.

## 18. TDD boundary

Integrated baseline is 24 CTests.

Add exactly one target:

```text
extractpdf.pdf_flatten
```

Target after #55: 25 CTests.

First RED must be attributable only to the absent approved flatten ABI while all existing 24 CTests continue to pass.

Required deterministic fixtures/observations include at least:

1. Square with existing Form `/AP /N`: output page render equals source render, ordinary annotation disappears.
2. FreeText with existing appearance: bake without regeneration.
3. Text annotation with an eligible flags-neutral appearance succeeds; a NoZoom/NoRotate Text case is `UNSUPPORTED`.
4. non-identity appearance `/Matrix` + non-zero `/BBox` origin maps correctly.
5. state dictionary `/AP /N` + valid `/AS` selects exact state.
6. stale Widget field `/V` vs `/AS`: baked pixels follow `/AS`, proving no recalculation.
7. missing normal appearance -> `UNSUPPORTED`.
8. malformed AP/BBox/Matrix/AS -> deterministic `FORMAT`.
9. selected Redact/FileAttachment/RichMedia/unknown subtype -> `UNSUPPORTED`.
10. selected Popup/IRT forward and reverse relationships -> `UNSUPPORTED`.
11. neutral zero-border Link survives unchanged.
12. visible-border/AP Link on a changed page -> `UNSUPPORTED`.
13. annotation-only flatten on a page with Widget -> visual-closure `UNSUPPORTED`.
14. Widget-only flatten on a page with ordinary annotation -> visual-closure `UNSUPPORTED`.
15. flatten both preserves original selected `/Annots` relative stacking in the bake stream and leaves neutral Links.
16. Text Widget with existing appearance: pixels preserved, Widget removed.
17. checkbox/radio Widget state: current AP state preserved exactly.
18. merged field+Widget dictionary is removed correctly from page and field tree.
19. multi-Widget field: all Widgets baked, field removed only after last Widget provenance is removed.
20. unrelated pre-existing widgetless field subtree survives.
21. `/CO` entries for removed fields are pruned; surviving order unchanged.
22. NeedAppearances true -> `UNSUPPORTED`; malformed NeedAppearances -> `FORMAT`.
23. XFA -> `UNSUPPORTED`.
24. annotation-only operation leaves AcroForm graph unchanged on pages satisfying closure.
25. Widget-only operation leaves ordinary annotations unchanged on pages satisfying closure.
26. semantic no-op with no requested targets is deterministic.
27. source page/annotation/form snapshots unchanged after success/failure.
28. output remains valid after source close.
29. repeated same-source/same-flags bytes are identical.
30. tagged, encrypted, and signed changed cases fail closed.
31. Linux/macOS/Windows render-equivalence fixtures agree and Windows DLL exports the new ABI.

## 19. Execution gates

Implementation sequence after this committed spec is separately approved:

```text
1. write committed implementation plan
2. strict compile RED + draft PR
3. ABI shell -> runtime RED
4. strict appearance resolver / policy GREEN
5. page bake writer GREEN
6. Widget provenance + field pruning GREEN
7. visual-closure / relationship / malformed matrix
8. determinism / no-op / lifetime / atomicity
9. freeze exact head + semantic-write audit
10. same-SHA Linux static + ASan/UBSan + macOS + Windows DLL full-ci
11. STOP at explicit integration gate
12. integrate only after `go integrate`
13. integrated-master exact-SHA proof
14. only then close #55 and update #48/#2
```

No production implementation, RED target, PR creation, or workflow change belongs in the spec phase.

## 20. Explicit non-goals

V1 does not provide:

- appearance generation/resynthesis;
- JavaScript/event/calculation/format/validation execution;
- Link flattening;
- redaction application;
- attachment/media extraction or flatten semantics;
- page/object-selective flatten IDs;
- arbitrary content-stream editing DSL;
- rasterization;
- tagged-PDF structure-tree rewrite;
- optional-content/OCG rewrite;
- page box/Rotate/UserUnit changes;
- image recompression;
- GC/object deduplication;
- encryption/decryption;
- incremental save;
- signature preservation;
- public MuPDF object identity.

The next implementation step is blocked until this committed design is reviewed and approved.
