# ExtractPDF Flatten / Bake V1 — Normative Preflight Correction

Date: 2026-08-30
Status: normative correction to `2026-08-30-extractpdf-flatten-bake-design.md`
Issue: #55

This document resolves implementation ambiguity discovered during committed-spec self-review. Where this document is more specific than the main design, this document is normative.

## 1. Visually-neutral Link border rule

Section 3.2 of the main design says that a Link remaining on a changed page must have no normal appearance and an explicitly zero effective border width.

The exact V1 rule is:

1. If Link `/AP /N` is present in any form, the Link is **not visually neutral** for V1. A changed page containing it is `EXTRACTPDF_ERROR_UNSUPPORTED`.
2. Otherwise inspect border style without invoking appearance synthesis.
3. If `/BS` is present, `/BS` is authoritative for V1:
   - `/BS` must be a dictionary;
   - `/W`, when absent, has the PDF default width `1` and is therefore non-neutral;
   - `/W`, when present, must be one finite non-negative number;
   - only exact numeric width `0` is visually neutral;
   - malformed `/BS` or `/W` is `EXTRACTPDF_ERROR_FORMAT`.
4. Only when `/BS` is absent, inspect `/Border`:
   - absent `/Border` uses the PDF default border width `1` and is therefore non-neutral;
   - present `/Border` must be an array with at least the first three numeric entries required to determine width;
   - the first three consumed values must be finite numbers;
   - the third value is the border width;
   - only exact numeric width `0` is visually neutral;
   - malformed consumed `/Border` data is `EXTRACTPDF_ERROR_FORMAT`.
5. A non-zero or default/implicit width is structurally valid but visually non-neutral and therefore `EXTRACTPDF_ERROR_UNSUPPORTED` on a changed page.

This rule intentionally does not try to infer whether a viewer happens not to paint a default Link border. V1 requires a proof of visual neutrality rather than a viewer-specific guess.

## 2. AcroForm affected-node closure

Section 12.3 is replaced by this exact pruning model.

Before first write, the strict Widget provenance produces a field-tree locator for every selected Widget.

Define the **affected field-node closure** as:

```text
for every selected Widget:
    owning terminal/merged field node
    + every field ancestor on its locator path to /AcroForm /Fields
```

Nodes outside this closure are never removed merely because they already have zero Widgets or an empty-looking shape.

### 2.1 Widget removal

All Widgets are selected when `EXTRACTPDF_FLATTEN_WIDGETS` is active because V1 is whole-document.

For every selected Widget:

- remove its exact object identity from the page `/Annots` replacement;
- if it is a separate Widget child in a field `/Kids`, omit that exact child from the replacement `/Kids`;
- if it is a merged field+Widget dictionary, mark that field node itself removable after its page-annotation identity is baked.

### 2.2 Bottom-up field pruning

After selected Widget entries are removed, walk only affected field nodes bottom-up.

An affected field node is removed from its parent `/Kids` or root `/Fields` when either:

1. it is a merged field+Widget node whose Widget was selected; or
2. after child/Widget removal, it has no surviving `/Kids` entries.

A non-terminal affected ancestor is therefore removed when every child in its affected subtree was removed and no unaffected child survives.

A node outside the affected closure is preserved exactly even if it was already widgetless or empty before flattening.

This removes the ambiguous phrase “independently surviving field semantics”. V1 pruning is defined structurally by provenance ancestry and surviving children, not by interpreting arbitrary field values/actions as a reason to keep an otherwise removed branch.

### 2.3 Copy-on-write arrays

Modified field arrays are never edited in place.

For every changed owner:

- construct a new `/Kids` array preserving surviving child identities and order; or
- construct a new root `/Fields` array preserving surviving root identities and order;
- replace the owner's reference with the new array;
- if no child survives, remove `/Kids` from an affected field before the field itself is pruned by its parent;
- never mutate an indirect/shared source array object in place.

The same copy-on-write rule applies to `/CO`: construct a new array containing surviving field references in original order.

### 2.4 Root AcroForm result

After bottom-up pruning:

- if root `/Fields` has at least one surviving entry, preserve `/AcroForm` and unrelated keys;
- if root `/Fields` has zero surviving entries, remove Catalog `/AcroForm` entirely;
- do not preserve a now-empty `/Fields []` shell merely to retain former form-only keys.

## 3. Strict `/Contents` shape for changed pages

Section 10.3 is clarified as follows.

Flatten consumes `/Contents` because it must append a new stream. A changed page accepts only:

1. absent `/Contents`;
2. one indirect stream object; or
3. an array whose every entry resolves to an indirect stream object.

Anything else is `EXTRACTPDF_ERROR_FORMAT` for a changed page.

The writer never mutates an existing Contents array in place. It creates a new page-local array containing all original stream references in order followed by the one new bake stream.

This means malformed content topology cannot be silently carried through a transform that claims deterministic page-render preservation.

## 4. Required correction tests

The #55 test target must include explicit cases proving:

1. Link with `/BS /W 0` is visually neutral.
2. Link with `/Border [0 0 0]` and no `/BS` is visually neutral.
3. Link with missing `/BS` and `/Border` is `UNSUPPORTED` on a changed page because default width is non-zero.
4. Link `/BS` overrides conflicting `/Border` for neutrality classification.
5. malformed `/BS /W` and malformed consumed `/Border` are `FORMAT`.
6. a pre-existing widgetless field outside the affected closure survives.
7. an affected grouping ancestor is pruned when its last affected child disappears.
8. an affected ancestor with one unaffected child survives with a copy-on-write `/Kids` array containing that child in the same order.
9. a merged field+Widget root entry is removed correctly.
10. modified `/Fields`, `/Kids`, and `/CO` arrays are replacement objects rather than in-place edits of their prior array objects.
11. changed page with malformed Contents array entry -> `FORMAT`.
12. changed page with valid stream-array Contents retains original stream references/order and appends exactly one bake stream.

No production code is authorized by this correction. The feature remains at the committed-spec review gate after self-review completes.
