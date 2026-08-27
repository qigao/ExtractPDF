# ExtractPDF Page Image Occurrence Snapshot Design

Date: 2026-08-27  
Status: approved design  
Tracks: #13, umbrella #2  
Stacked base: text-search head `f4b9a8931094f5a43f5f20a715a86c290302439a`

## Goal

Enumerate raster images actually painted by page content, with occurrence geometry and intrinsic metadata, without scanning PDF xref resources and without decoding image pixels in this slice.

## Why occurrence-based enumeration

A PDF image resource is not equivalent to a visible page occurrence. One image object can be unused, or painted multiple times with different transforms. The public contract therefore models one occurrence per image paint operation emitted by MuPDF while interpreting page contents.

V1 listens only to MuPDF `fill_image` device callbacks. `fill_image_mask` and `clip_image_mask` are stencil/mask operations with color/clip semantics, so they are deliberately not projected as normal image occurrences in this slice.

## Public ABI

The search slice already defines `extractpdf_point` and `extractpdf_quad`; images reuse those exact public geometry types.

```c
typedef struct extractpdf_image_page extractpdf_image_page;

typedef struct extractpdf_image_info {
    size_t struct_size;
    extractpdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} extractpdf_image_info;

EXTRACTPDF_API extractpdf_status extractpdf_extract_images(
    extractpdf_page *page,
    extractpdf_image_page **out_images);

EXTRACTPDF_API extractpdf_status extractpdf_image_count(
    const extractpdf_image_page *images,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_image_get_info(
    const extractpdf_image_page *images,
    size_t index,
    extractpdf_image_info *out_info);

EXTRACTPDF_API void extractpdf_drop_image_page(
    extractpdf_image_page *images);
```

`extractpdf_image_info` is versioned through `struct_size`. V1 requires at least `offsetof(extractpdf_image_info, has_alpha) + sizeof(has_alpha)`. Future fields may be appended. On valid-sized argument/index failure, known V1 output fields are reset to zero while preserving `struct_size`; undersized structs are rejected without writing caller-unauthorized bytes.

## Capture mechanism

`src/images.c` implements a small external MuPDF device derived from `fz_device`. Its `fill_image` callback records each normal raster image paint. Extraction runs:

```c
fz_run_page_contents(ctx, page->page, device, fz_identity, NULL);
```

Using `fz_run_page_contents` deliberately excludes annotation/widget appearances from V1 page-content image enumeration. Nested form/XObject interpretation still reaches the device as normal image paint callbacks.

No PDF xref/object traversal is used.

## Geometry

For each `fill_image(ctx, dev, image, ctm, ...)`, `ctm` maps the image unit square into page space. ExtractPDF stores the transformed four corners as an `extractpdf_quad` in the same Fitz page-space contract already used by Page/Render/Text/Search:

- CropBox top-left origin;
- point units (72 points/inch);
- Y increases downward;
- independent of render DPI/zoom.

The public quad preserves orientation and shear rather than collapsing placement to an axis-aligned rectangle.

V1 records the paint transform even if later clipping or alpha would make part/all of the occurrence invisible. The semantic is therefore **page-content image paint occurrence**, not pixel-level visible-area analysis.

## Intrinsic metadata

Metadata is copied at capture time without decoding pixels:

- `pixel_width`: MuPDF image width;
- `pixel_height`: MuPDF image height;
- `components`: intrinsic component count;
- `bits_per_component`: intrinsic bits/component;
- `has_alpha`: true when the source has an associated mask/soft mask or color-key transparency.

`has_alpha` does not promise that a later decoded bitmap has four components; pixel format remains a separate decode API decision.

The implementation may read the public `fz_image` fields needed for this metadata. MuPDF's own Java JNI binding uses the same `w`, `h`, `n`, `bpc`, mask and color-key fields for image metadata access.

## Internal ownership and lifetime

Each occurrence retains the exact `fz_image *` with `fz_keep_image()` so the later decoded-bitmap slice can use the same resource without rerunning page interpretation.

Conceptually:

```text
extractpdf_image_page
  document -> non-owning parent document/context
  items[]
    image -> retained fz_image reference
    quad
    copied intrinsic metadata
```

The image snapshot may outlive the source `extractpdf_page`, but the parent `extractpdf_document` must remain alive until `extractpdf_drop_image_page()` because image references are dropped with the document's MuPDF context. This matches the existing parent-lifetime model used by page/bitmap handles and avoids introducing document refcounting in this slice.

`extractpdf_drop_image_page(NULL)` is safe.

## Allocation and errors

The capture callback grows the occurrence array with normal C allocation. On overflow/allocation failure it records an OOM flag and ignores subsequent images; after page interpretation finishes, extraction destroys partial state and returns `EXTRACTPDF_ERROR_NOMEM`.

MuPDF exceptions from page interpretation are caught before crossing the public ABI and translated through the existing status mapping.

`extractpdf_extract_images` sets `*out_images = NULL` on every failure. An image-free page returns `EXTRACTPDF_OK` with a valid zero-count snapshot.

`extractpdf_image_count` resets `*out_count = 0` before validation when supplied.

## Deterministic TDD fixture

Add `tests/fixtures/page-images.pdf` containing one 2x1 DeviceRGB 8-bpc image XObject with two pixels, reused twice in the page content with different axis-aligned transforms. The same resource must therefore produce exactly two occurrences.

Expected V1 metadata for both occurrences:

```text
pixel_width       = 2
pixel_height      = 1
components        = 3
bits_per_component= 8
has_alpha         = 0
```

The placements are chosen so their Fitz page-space bounding boxes are distinct and deterministic. Tests derive min/max bounds from each returned quad, confirm two occurrences, and verify the same intrinsic metadata on both.

The empty-page fixture verifies a valid zero-count snapshot.

## TDD / CI

Development remains Linux-first:

1. RED: fixture + `test_images.c` + CTest wiring reference the approved API before production declarations exist.
2. GREEN: add public declarations, private snapshot model, and `src/images.c` custom device.
3. Harden: invalid/null args, `struct_size`, output reset, empty page, page-drop-before-info access, and NULL-safe drop.
4. Linux normal CTest + ASan/UBSan must pass on exact head.
5. Windows/macOS remain deferred to the Phase 3 Content `full-ci` checkpoint.

## Non-goals

This slice does not add:

- decoded image pixels;
- image file export/encoding;
- annotation/widget appearance images;
- image-mask/stencil enumeration;
- PDF xref/object identifiers;
- visible-area clipping analysis;
- image deduplication by digest;
- document lifetime refcounting;
- new concurrency guarantees.
