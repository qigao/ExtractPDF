# ExtractPDF CropBox V1 Page-Transform Direction Correction

Issue: #49  
PR: #50  
Normative amendment to: `docs/superpowers/specs/2026-08-29-extractpdf-cropbox-transform-design.md`  
Observed failing implementation head: `582604199ec2557738900266cb5fc0b4131755cc`  
Observed workflow: #302 / run `33248344569`

## Status

This document is a **normative correction** to the approved CropBox V1 design. Until the original design is later consolidated, this amendment takes precedence wherever the original spec or implementation plan describes the direction of MuPDF's page transform matrix.

No public API, scope, preservation policy, security policy, or CropBox-only write surface changes. The correction is limited to the internal mapping direction used to convert between raw PDF page coordinates and ExtractPDF's public Fitz page space.

## 1. Counterexample that exposed the error

Task 4 added an inherited page-box fixture:

```text
Pages node:
  MediaBox [0 0 400 300]
  CropBox  [10 20 390 280]
  Rotate   0

Page node:
  no local MediaBox
  no local CropBox
```

The existing public page API observes this page as:

```text
[0, 0, 380, 260]
```

A CropBox V1 no-op request is therefore:

```text
[0, 0, 380, 260]
```

Workflow #302 (`33248344569`) built the library and all 22 test executables successfully. Existing CTests #1-#21 passed, but #22 failed at the inherited no-op assertion:

```text
extractpdf_crop_pages(document, &full, 1, &noop) == EXTRACTPDF_OK
```

The failure persisted after treating `pdf_page_obj_transform()`'s `outbox` as raw PDF coordinates and deriving public bounds with the inverse matrix. That result demonstrated that the approved matrix-direction assumption itself was wrong.

## 2. Authoritative behavioral evidence

MuPDF 1.28.2 `source/pdf/pdf-page.c` constructs the matrix from page `/UserUnit`, `/Rotate`, and effective CropBox, and translates the effective CropBox origin to the Fitz page origin.

More importantly, MuPDF itself consumes the returned matrix in `pdf_bound_annot()` as follows:

```c
pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
rect = pdf_annot_display_rect(ctx, annot);
return fz_transform_rect(rect, page_ctm);
```

`pdf_annot_display_rect()` is annotation/PDF-page geometry. The result of applying `page_ctm` is the Fitz page-space annotation bound.

ExtractPDF's already integrated annotation implementation follows the same contract:

```text
raw PDF /Rect
    -> fz_transform_rect(raw, page_ctm)
    -> public extractpdf_rect
```

This behavior has already been proven by the integrated annotation test suite and is therefore a stronger project invariant than an ambiguous or contradictory prose comment in MuPDF's implementation/header.

## 3. Corrected invariant

For ExtractPDF CropBox V1, the matrix returned by:

```c
pdf_page_obj_transform(ctx, page_obj, NULL, &page_ctm);
```

must be treated as:

```text
PDF user space  --page_ctm-->  Fitz/public page space
```

The inverse maps in the opposite direction:

```text
Fitz/public page space  --inverse(page_ctm)-->  PDF user space
```

Use semantic names internally:

```text
pdf_to_public = page_ctm
public_to_pdf = inverse(pdf_to_public)
```

Do not retain a field named `public_to_pdf` when it actually stores MuPDF's returned matrix.

## 4. Correct box derivation

Strict raw page-box resolution remains unchanged:

```text
media_pdf   = normalized effective MediaBox
crop_pdf    = normalized effective CropBox, or MediaBox fallback
visible_pdf = intersection(media_pdf, crop_pdf)
```

`visible_pdf` must be finite and have positive area.

Public visible bounds are derived as:

```c
pdf_page_obj_transform(ctx, page_obj, NULL, &pdf_to_public);
visible_public_raw = fz_transform_rect(visible_pdf, pdf_to_public);
visible_public = normalized(visible_public_raw);
```

No value returned in `outbox` from `pdf_page_obj_transform()` is used as the public ExtractPDF rectangle. The strict resolver owns the effective-box calculation itself so it does not depend on MuPDF's tolerant repair/fallback behavior.

## 5. Correct request mapping

A caller supplies:

```text
requested_public
```

in current ExtractPDF Fitz page space.

After public-space finite/positive/shrink-only validation, map it back to raw PDF user space with:

```c
public_to_pdf = fz_invert_matrix(pdf_to_public);
requested_pdf = fz_transform_rect(requested_public, public_to_pdf);
requested_pdf = normalized(requested_pdf);
```

Then verify `requested_pdf` remains inside `visible_pdf` within the exact deterministic fixture contract used by V1.

Only `requested_pdf` is serialized as the new page-local `/CropBox`.

## 6. Inherited-box worked example

For the counterexample page:

```text
MediaBox = [0, 0, 400, 300]
CropBox  = [10, 20, 390, 280]
Rotate   = 0
UserUnit = 1
```

MuPDF's page matrix maps raw effective CropBox to public page space:

```text
[10, 20, 390, 280] -> [0, 0, 380, 260]
```

Therefore the request:

```text
[0, 0, 380, 260]
```

is an exact semantic no-op and must not materialize a local `/CropBox`.

The changed public request:

```text
[20, 10, 360, 250]
```

maps through `inverse(pdf_to_public)` to raw PDF:

```text
[30, 30, 370, 270]
```

which is the expected page-local `/CropBox` written by Task 4.

## 7. Impact on existing design sections

This amendment supersedes only statements equivalent to:

```text
MuPDF page_ctm is Fitz -> PDF
```

or code that uses MuPDF's returned matrix directly for public-to-PDF request mapping.

The following approved design properties remain unchanged:

- public request coordinates are Fitz page space;
- V1 is shrink-only;
- effective visible raw box is CropBox intersected with MediaBox;
- MediaBox/CropBox/Rotate inheritance rules remain unchanged;
- UserUnit remains page-local for this design;
- only changed pages receive a local `/CropBox`;
- no-op pages remain structurally untouched;
- content/resources/annotations/links/Widgets/AcroForm/outlines/destinations are structurally preserved;
- source document remains immutable;
- changed transformation uses full-document isolation, not page grafting;
- JavaScript/events/recalculation remain disabled;
- encrypted and signed input remain fail-closed;
- deterministic serializer/output lifetime rules remain unchanged.

## 8. Required regression proof

The inherited fixture is now a mandatory architecture regression test, not merely an edge-case test.

Before Task 4 can be declared GREEN, the same exact feature head must prove:

```text
source inherited CropBox raw [10,20,390,280]
    -> public [0,0,380,260]

no-op public [0,0,380,260]
    -> OK
    -> no local CropBox materialized

changed public [20,10,360,250]
    -> raw local CropBox [30,30,370,270]
```

The primary two-page interactive fixture must continue proving the complete preservation surface, and existing CTests #1-#21 must remain green.

Task 5 Rotate/UserUnit fixtures must use the same corrected `PDF -> public`, inverse-for-request mapping. Poster split must later build on this corrected invariant rather than introducing another coordinate model.

## 9. Implementation correction boundary

After this amendment is approved, the smallest production correction is:

1. rename the stored MuPDF matrix from `public_to_pdf` to `pdf_to_public`;
2. derive `visible_public` with the returned `pdf_to_public` matrix directly;
3. derive `public_to_pdf` locally via `fz_invert_matrix(pdf_to_public)` when building each request plan;
4. normalize the transformed request rectangle before containment checks/write;
5. leave the isolated writer and all non-coordinate policies unchanged;
6. rerun Task 4 static + ASan/UBSan 22/22 before proceeding to Task 5.

No additional public API or transform framework is authorized by this correction.
