# ExtractPDF MediaBox Trim V1 Page-Frame Correction

Issue: #51  
Normative amendment to: `docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-design.md`  
Baseline master: `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`  
MuPDF baseline: 1.28.2

## Status

This document is a **normative correction** to the committed MediaBox Physical Trim V1 design. It takes precedence wherever the original spec says that preserving a raw `/CropBox` necessarily preserves the same public page frame after `/MediaBox` clips that CropBox, or says that the post-trim public visible rectangle may retain a non-zero origin.

No public API, ownership model, write-surface policy, security policy, batch policy, or physical-trim scope changes. The correction is limited to the page-frame semantics that result from MuPDF's effective CropBox/MediaBox intersection.

No RED or production implementation has started, so this correction is applied before implementation planning.

## 1. Counterexample in the approved design

The approved spec distinguished:

```text
raw MediaBox  = [0, 0, 400, 300]
raw CropBox   = [50, 40, 350, 260]
```

and correctly observed that the source public MediaBox may extend outside the visible page frame.

The incorrect claim was that after shrinking MediaBox so that it clips the preserved raw CropBox:

```text
new MediaBox intersects only part of preserved CropBox
```

MuPDF would continue to anchor the public page frame to the original raw CropBox.

That would imply a non-zero public visible rectangle after trim. MuPDF 1.28.2 does not behave that way.

## 2. MuPDF 1.28.2 behavior

`pdf_page_obj_transform_box()` resolves the page's MediaBox and CropBox and, before establishing the page origin, performs:

```c
cropbox = pdf_to_rect(ctx, obj);
cropbox = fz_intersect_rect(cropbox, mediabox);
```

It then transforms that **effective intersection** and translates its top-left corner to the Fitz page origin:

```c
cropbox = fz_transform_rect(cropbox, *page_ctm);
*page_ctm = fz_concat(
    *page_ctm,
    fz_translate(-cropbox.x0, -cropbox.y0));
```

Therefore ExtractPDF must model the public page frame as anchored to the **effective visible CropBox intersection**, not blindly to the preserved raw CropBox object.

This is consistent with the already-integrated CropBox V1 project invariant:

```text
PDF user space --pdf_to_public--> ExtractPDF/Fitz page space
ExtractPDF/Fitz page space --inverse(pdf_to_public)--> PDF user space
```

The source `pdf_to_public` matrix is valid for mapping the caller's request into raw PDF coordinates. After `/MediaBox` is changed, reopening the output may produce a different `pdf_to_public` matrix because the effective CropBox intersection may have changed.

## 3. Corrected page-frame invariant

Strict source resolution remains:

```text
media_pdf = normalized(nearest local/inherited MediaBox)

if a real local/inherited CropBox exists:
    has_crop_box = true
    crop_pdf = normalized(nearest local/inherited CropBox)
else:
    has_crop_box = false
    crop_pdf = media_pdf

visible_pdf = intersection(media_pdf, crop_pdf)
```

`visible_pdf` must have positive area.

For a requested physical trim:

```text
requested_public
    -- inverse(source pdf_to_public) -->
requested_media_pdf
```

After the write, define the **post-trim effective visible box** as:

```text
if has_crop_box:
    output_visible_pdf = intersection(requested_media_pdf, crop_pdf)
else:
    output_visible_pdf = requested_media_pdf
```

`output_visible_pdf` must have positive area.

When the output is reopened, MuPDF derives a new page transform whose origin is anchored to `output_visible_pdf` after Rotate/UserUnit processing.

## 4. Output visible origin

For every valid output page, the public visible rectangle observed through:

```c
extractpdf_page_bounds(...)
extractpdf_page_box_bounds(..., EXTRACTPDF_PAGE_BOX_CROP, ...)
```

is anchored at the current effective visible page origin.

For deterministic unrotated/UserUnit=1 fixtures this means:

```text
output visible public bounds = [0, 0, width, height]
```

where width/height are the dimensions of `output_visible_pdf` after the normal page transform.

The MediaBox trim spec must **not** require a non-zero public visible x0/y0 after clipping a preserved CropBox.

The useful non-zero rectangle is instead the **public MediaBox** relative to the effective visible frame. A MediaBox that extends outside the effective CropBox may legitimately have negative or non-zero public coordinates.

## 5. Three distinct cases

### 5.1 No real CropBox: fallback follows MediaBox

Source:

```text
has_crop_box = false
crop_pdf = media_pdf by fallback
```

After changing MediaBox:

```text
output_media_pdf   = requested_media_pdf
output_visible_pdf = requested_media_pdf
```

The output page frame re-anchors to the new MediaBox.

Observable result:

```text
output visible origin  = (0,0)
output CropBox fallback = output MediaBox
```

Source request x0/y0 are coordinates in the source frame and are not preserved as output public x0/y0.

### 5.2 Real CropBox, physical-only MediaBox trim

If:

```text
intersection(requested_media_pdf, crop_pdf)
    == source visible_pdf
```

then the effective visible box is unchanged.

Observable result:

```text
MediaBox changes
raw CropBox unchanged
visible box unchanged
page transform unchanged
ordinary public object geometry unchanged
```

This remains a real transform because `/MediaBox` changed. It is not a no-op.

### 5.3 Real CropBox, MediaBox clips the effective visible box

If:

```text
intersection(requested_media_pdf, crop_pdf)
    != source visible_pdf
```

but the new intersection still has positive area, the trim is valid.

Observable result:

```text
raw CropBox unchanged
output visible PDF box = new MediaBox ∩ raw CropBox
output page frame re-anchors to that new effective intersection
output visible public origin = (0,0)
object PDF geometry unchanged
object public geometry changes only through the new page transform
```

The implementation must not preserve the old public frame by rewriting CropBox or translating individual objects.

## 6. Worked example

Source raw boxes:

```text
MediaBox = [0, 0, 400, 300]
CropBox  = [50, 40, 350, 260]
Rotate   = 0
UserUnit = 1
```

Source effective visible PDF box:

```text
[50, 40, 350, 260]
```

Source public observations are approximately:

```text
visible public = [0, 0, 300, 220]
MediaBox public extends beyond that visible frame
```

### Physical-only request

Choose a new MediaBox that still contains the source effective visible box, for example raw:

```text
[20, 20, 380, 280]
```

Then:

```text
intersection(new MediaBox, CropBox)
    = [50, 40, 350, 260]
```

so the public page frame and ordinary object coordinates remain unchanged.

### Clipping request

Choose a new MediaBox whose intersection with CropBox is smaller, for example raw:

```text
[20, 20, 380, 220]
```

Then:

```text
output visible PDF = [50, 40, 350, 220]
```

MuPDF re-anchors the output page transform to that new effective box. The output visible public rectangle begins at `(0,0)` and has the new visible dimensions.

The output public MediaBox may still extend outside that visible frame because its raw x/y extent is larger than the effective intersection. That is where negative/non-zero public MediaBox coordinates are expected and should be tested.

## 7. Corrected request/output relationship

The caller's request remains defined in the **source** public MediaBox frame:

```c
extractpdf_page_box_bounds(
    source_page,
    EXTRACTPDF_PAGE_BOX_MEDIA,
    &source_media_public);
```

The request must be a finite positive-area subrectangle of that source MediaBox.

The implementation maps it with the **source** inverse page transform to `requested_media_pdf` and writes only that raw rectangle.

The output public MediaBox is then derived from the **output** page transform. Consequently:

- when the effective visible box is unchanged, source and output frames are the same and the output MediaBox corresponds to the requested source public rectangle;
- when the effective visible box changes, the output frame re-anchors and the output MediaBox public coordinates are not required to equal the source request coordinates;
- raw physical region equality is the invariant across the transform, not public-coordinate equality across different page frames.

## 8. Corrected preservation observations

The deterministic suite must distinguish frame-preserving and frame-changing MediaBox trims.

### Required frame-preserving case

A real/inherited CropBox exists and the new MediaBox still leaves the source effective visible intersection unchanged.

Prove:

```text
raw MediaBox changed
raw CropBox unchanged
visible public bounds unchanged
text/image/link/annotation/Widget public geometry unchanged
internal/outline target public coordinates unchanged on that target page
```

### Required frame-changing case

A real/inherited CropBox exists and the new MediaBox clips the effective visible intersection.

Prove:

```text
raw MediaBox changed
raw CropBox unchanged
output visible public bounds start at (0,0)
output visible dimensions shrink
page transform changes
text/image/link/annotation/Widget public geometry follows the new transform
internal/outline target public coordinates follow the target page's new transform
no underlying object geometry is rewritten
```

Also prove at least one case where:

```text
output MediaBox public bounds
```

have negative/non-zero coordinates relative to the output visible frame. This replaces the incorrect requirement for non-zero **visible** bounds.

## 9. Raw CropBox outside MediaBox

The correction also applies when the preserved raw CropBox extends outside MediaBox.

Source validity remains based on:

```text
source visible = source MediaBox ∩ raw CropBox
```

After trim:

```text
output visible = requested MediaBox ∩ raw CropBox
```

Frame-preserving/no-frame-change is determined by equality of the **effective intersections**, not by whether requested MediaBox contains the entire raw CropBox object.

This distinction is mandatory for the existing CropBox-outside-MediaBox regression fixture.

## 10. Impact on no-op semantics

No-op semantics are unchanged.

A MediaBox request is a no-op only when it equals the current public MediaBox rectangle component-wise under the source frame.

A changed MediaBox that leaves `output_visible_pdf == source_visible_pdf` remains a real physical-only trim, even though the page transform and visible observations stay unchanged.

## 11. Impact on private preflight

The future shared page-box resolver still exposes:

```text
media_pdf
crop_pdf
visible_pdf
has_crop_box
pdf_to_public
media_public
visible_public
Rotate/UserUnit
```

Trim planning additionally computes:

```text
requested_media_pdf
output_visible_pdf
frame_changes = output_visible_pdf != source visible_pdf
```

The future private reparse consistency check must reproduce the same:

- CropBox provenance;
- source effective visible intersection;
- requested raw MediaBox containment;
- post-trim effective visible intersection;
- changed/no-op classification;
- frame-preserving versus frame-changing classification.

All checks occur before the first private write.

## 12. Unchanged policies

The following approved requirements remain unchanged:

- public API is `extractpdf_page_trim` + `extractpdf_trim_pages()`;
- source is immutable;
- output is independent immutable bytes;
- V1 is shrink-only against source MediaBox;
- only changed pages receive a local `/MediaBox`;
- `/CropBox`, `/BleedBox`, `/TrimBox`, `/ArtBox`, `/Rotate`, `/UserUnit`, content/resources/interactive/root objects are never written as side effects;
- Bleed/Trim/Art are opaque preservation state and are not trim-preflight inputs;
- objects outside the new medium remain in the graph;
- no page grafting;
- no content/object geometry rewrite;
- no JavaScript/form runtime or appearance regeneration;
- encrypted and signed input fail closed;
- no-op canonical serialization and deterministic batch behavior remain required;
- 22 -> 23 CTests;
- exact-head Linux static + ASan/UBSan and same-SHA Linux/macOS/Windows proof remain required;
- explicit integration authorization and integrated-master proof remain required.

## 13. Implementation-planning gate

The implementation plan must use this corrected model. In particular it must not encode either of these rejected assumptions:

```text
preserved raw CropBox => public page frame always unchanged

MediaBox clips CropBox => output visible public origin may stay non-zero
```

Instead it must explicitly test both:

```text
physical-only MediaBox trim => effective visible intersection unchanged => frame unchanged

clipping MediaBox trim => effective visible intersection changed => frame re-anchored
```

Implementation planning remains blocked until this correction is reviewed and approved.