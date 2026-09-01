# QuantaPDF Composer Design

**Date:** 2026-09-01
**Status:** Accepted for implementation

## Goal

Add a small, stable-C-ABI PDF generation facade for documents made from
formatted text and raster images. This is a document composer, not an Acrobat
replacement or a general PDF object editor.

## V1 scope

- Create a document and add bounded pages with explicit point dimensions.
- Draw UTF-8 text into a rectangle using the PDF base-14 font families,
  font size, ARGB color, line spacing, wrapping, and horizontal alignment.
- Register JPEG or PNG bytes once and place an image repeatedly with contain,
  cover, or stretch fitting.
- Finish to the existing owning `quantapdf_output` type.
- Produce deterministic bytes for an unchanged composer.

Out of scope: complex-script shaping, arbitrary PDF object editing, importing
existing pages, transparency groups beyond PNG alpha, vector illustration,
forms, signatures, and incremental saves. Embedded TTF/OTF fonts are a future
additive ABI extension; V1 rejects code points a selected base-14 font cannot
encode instead of silently replacing them.

## Public facade

`quantapdf_composer` is opaque. Public option records begin with
`size_t struct_size`; V1 minimum and full sizes are macros. Resource handles
are 32-bit values, with zero reserved as invalid. Pages are addressed by a
zero-based `size_t` index.

The facade operations are:

1. `quantapdf_composer_create`
2. `quantapdf_composer_add_page`
3. `quantapdf_composer_add_image`
4. `quantapdf_composer_draw_text`
5. `quantapdf_composer_draw_image`
6. `quantapdf_composer_finish`
7. `quantapdf_drop_composer`

All geometry uses PDF points and a top-left origin. Rectangles must be finite
and ordered. Color is straight ARGB; alpha is currently accepted only as 255
for text, while PNG image alpha is preserved.

## Ownership and lifecycle protocol

| Item | Contract |
|---|---|
| Composer | Created by `create`, mutable and single-threaded, destroyed by `drop_composer`. |
| Text | Copied during `draw_text`; caller may release or mutate its buffer after return. |
| Image bytes | Copied during `add_image`; caller retains ownership of its input. |
| Output | New independent allocation from each successful `finish`; destroyed by `drop_output`. |
| Page/resource IDs | Scalar values valid until the composer is destroyed; failed calls publish no ID. |
| Finish | Non-consuming. It snapshots current state and leaves the composer usable. Failure sets `*out_output` to null and does not mutate the composer. |

No call retains a caller pointer. The authoritative state is the composer's
owned page, operation, and resource arrays.

## Capacity and failure

Default limits are 1,024 pages, 1,000,000 draw operations, and 256 MiB of
copied resource/text bytes. Custom lower limits are accepted through composer
options. Every addition checks multiplication and addition before allocation.
Crossing a limit returns `QUANTAPDF_ERROR_UNSUPPORTED`; allocation failure
returns `QUANTAPDF_ERROR_NOMEM`; malformed UTF-8/image data returns
`QUANTAPDF_ERROR_FORMAT`; invalid state or identifiers return
`QUANTAPDF_ERROR_ARGUMENT`.

Mutations are transactional: validate and allocate temporary state before
publishing it. The implementation performs no user callback and no I/O until
the caller explicitly saves an output.

## Backend

The C facade stores a backend-neutral command list. A private C++ qpdf adapter
builds a fresh PDF during `finish`, creates page dictionaries and resources,
and emits content streams. PDFium is used by integration tests to render and
inspect the generated output; it remains the read/render backend.

JPEG data is embedded with `DCTDecode`. PNG data is validated and decoded with
zlib using the PNG scanline filters; RGB/gray samples are recompressed with
`FlateDecode`, and alpha becomes an image soft mask. No AGPL dependency or
MuPDF code is introduced.

## Verification

- Argument, struct-size, geometry, UTF-8, identifier, and capacity boundaries.
- Generated page count and dimensions after reopening with QuantaPDF.
- Extracted text and rendered pixel checks for alignment/color/layout.
- JPEG and PNG placement, contain/cover/stretch, and PNG alpha rendering.
- Repeated finish byte equality and output independence.
- ABI export baseline and downstream installed-package smoke test.

