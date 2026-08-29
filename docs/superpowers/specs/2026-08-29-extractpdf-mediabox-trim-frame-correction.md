# ExtractPDF MediaBox Trim V1 Page-Frame Correction

Issue: #51  
Normative amendment to: `docs/superpowers/specs/2026-08-29-extractpdf-mediabox-trim-design.md`  
Baseline master: `3fc48b5fb0f7a07926f7942fc4a4a3fb5e93a753`  
MuPDF baseline: 1.28.2

## Status

This file records the architecture correction discovered during implementation-plan self-review. The main MediaBox trim design spec has now been consolidated to match this correction; this document remains the explicit counterexample/evidence record.

No RED or production implementation had started when the correction was found.

## 1. Rejected assumption

The earlier draft assumed that preserving a raw `/CropBox` also preserved the same public page frame when a new `/MediaBox` clipped that CropBox, producing a non-zero public visible origin.

That is false for MuPDF 1.28.2.

## 2. MuPDF behavior

Before establishing the page origin, `pdf_page_obj_transform_box()` computes:

```c
cropbox = pdf_to_rect(ctx, obj);
cropbox = fz_intersect_rect(cropbox, mediabox);
```

and then anchors the page transform to that effective intersection:

```c
cropbox = fz_transform_rect(cropbox, *page_ctm);
*page_ctm = fz_concat(
    *page_ctm,
    fz_translate(-cropbox.x0, -cropbox.y0));
```

Therefore the public frame is anchored to the **effective visible CropBox/MediaBox intersection**, not blindly to the preserved raw CropBox object.

## 3. Correct invariant

Strict source resolution remains:

```text
media_pdf = normalized(effective MediaBox)

if a real CropBox exists:
    has_crop_box = true
    crop_pdf = normalized(effective raw CropBox)
else:
    has_crop_box = false
    crop_pdf = media_pdf

source_visible_pdf = intersection(media_pdf, crop_pdf)
```

A request is supplied in source public page space and maps through:

```text
requested_public
  -- inverse(source pdf_to_public) -->
requested_media_pdf
```

Post-trim:

```text
if has_crop_box:
    output_visible_pdf = intersection(requested_media_pdf, crop_pdf)
else:
    output_visible_pdf = requested_media_pdf
```

The output page transform is derived from `output_visible_pdf` after Rotate/UserUnit processing.

## 4. Three required cases

### No real CropBox

```text
output_visible_pdf = requested_media_pdf
```

The output frame re-anchors to the new MediaBox. For deterministic unrotated/UserUnit=1 fixtures, visible public bounds start at `(0,0)`.

### Real CropBox, physical-only trim

If:

```text
output_visible_pdf == source_visible_pdf
```

then:

```text
MediaBox changes
raw CropBox unchanged
page frame unchanged
visible public geometry unchanged
ordinary object public geometry unchanged
```

This is still a real physical trim, not a no-op.

### Real CropBox, clipping trim

If:

```text
output_visible_pdf != source_visible_pdf
```

but remains positive-area, then:

```text
MediaBox changes
raw CropBox unchanged
page frame re-anchors to output_visible_pdf
visible public bounds restart at the new effective origin
object PDF geometry remains unchanged
object public geometry follows the new page transform
```

The implementation must not rewrite CropBox or individual objects to preserve the old public frame.

## 5. Non-zero rectangle to test

The output **visible** public rectangle should not be required to preserve a non-zero origin after clipping.

Instead, test a case where the output **MediaBox public rectangle** is negative/non-zero relative to the output visible frame because MediaBox extends outside the effective CropBox intersection.

## 6. Raw CropBox outside MediaBox

Frame-preserving versus frame-changing classification is based on equality of effective intersections:

```text
source_visible_pdf
vs
requested_media_pdf ∩ raw crop_pdf
```

It is not based on whether requested MediaBox contains the entire raw CropBox object.

## 7. Planning consequence

The implementation plan must explicitly test:

```text
physical-only trim
  => effective visible intersection unchanged
  => frame unchanged

clipping trim
  => effective visible intersection changed
  => frame re-anchored
```

and must store/revalidate enough private plan state to distinguish those two cases before the first private write.

All other approved policies remain unchanged: public ABI, shrink-only MediaBox input, MediaBox-only semantic write, opaque Bleed/Trim/Art preservation, full-document isolation, security fail-closed behavior, deterministic batch semantics, 22 -> 23 CTests, exact-head cross-platform proof, and explicit integration authorization.