#ifndef EXTRACTPDF_EXTRACTPDF_H
#define EXTRACTPDF_EXTRACTPDF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(EXTRACTPDF_SHARED)
#  if defined(EXTRACTPDF_BUILDING_LIBRARY)
#    define EXTRACTPDF_API __declspec(dllexport)
#  else
#    define EXTRACTPDF_API __declspec(dllimport)
#  endif
#else
#  define EXTRACTPDF_API
#endif

typedef struct extractpdf_document extractpdf_document;
typedef struct extractpdf_page extractpdf_page;
typedef struct extractpdf_bitmap extractpdf_bitmap;

typedef struct extractpdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} extractpdf_rect;

typedef enum extractpdf_page_box {
    EXTRACTPDF_PAGE_BOX_MEDIA = 0,
    EXTRACTPDF_PAGE_BOX_CROP = 1
} extractpdf_page_box;

typedef enum extractpdf_pixel_format {
    EXTRACTPDF_PIXEL_FORMAT_RGB8 = 1,
    EXTRACTPDF_PIXEL_FORMAT_RGBA8 = 2
} extractpdf_pixel_format;

/*
 * Version-1 render options prefix. Future versions may append fields.
 * Callers set struct_size to sizeof(extractpdf_render_options). A NULL
 * options pointer selects RGB8 at the default 72-DPI page-space scale.
 */
typedef struct extractpdf_render_options {
    uint32_t struct_size;
    uint32_t pixel_format; /* extractpdf_pixel_format */
} extractpdf_render_options;

#define EXTRACTPDF_RENDER_OPTIONS_V1_SIZE 8u
#define EXTRACTPDF_RENDER_OPTIONS_INIT \
    { (uint32_t)sizeof(extractpdf_render_options), \
      (uint32_t)EXTRACTPDF_PIXEL_FORMAT_RGB8 }

typedef struct extractpdf_bitmap_info {
    int width;
    int height;
    int stride;
    extractpdf_pixel_format pixel_format;
    size_t data_size;
} extractpdf_bitmap_info;

typedef enum extractpdf_status {
    EXTRACTPDF_OK = 0,
    EXTRACTPDF_ERROR_ARGUMENT = 1,
    EXTRACTPDF_ERROR_IO = 2,
    EXTRACTPDF_ERROR_PASSWORD = 3,
    EXTRACTPDF_ERROR_FORMAT = 4,
    EXTRACTPDF_ERROR_UNSUPPORTED = 5,
    EXTRACTPDF_ERROR_NOMEM = 6,
    EXTRACTPDF_ERROR_MUPDF = 7
} extractpdf_status;

EXTRACTPDF_API extractpdf_status extractpdf_open(
    const char *filename,
    const char *password,
    extractpdf_document **out_document);

EXTRACTPDF_API extractpdf_status extractpdf_page_count(
    extractpdf_document *document,
    int *out_page_count);

EXTRACTPDF_API extractpdf_status extractpdf_load_page(
    extractpdf_document *document,
    int page_index,
    extractpdf_page **out_page);

/*
 * Return a page box in ExtractPDF page space. The transformed CropBox
 * top-left is (0, 0), +x points right, +y points down, and PDF /Rotate
 * and /UserUnit are already reflected in the returned coordinates.
 * MediaBox coordinates may therefore extend outside the CropBox and be
 * negative.
 */
EXTRACTPDF_API extractpdf_status extractpdf_page_bounds(
    extractpdf_page *page,
    extractpdf_page_box box,
    extractpdf_rect *out_bounds);

/* Return the PDF page rotation normalized to 0, 90, 180, or 270 degrees. */
EXTRACTPDF_API extractpdf_status extractpdf_page_rotation(
    extractpdf_page *page,
    int *out_rotation_degrees);

/*
 * Render a loaded page. RGB8 uses an opaque white background. RGBA8 uses
 * premultiplied alpha with a transparent background. The returned bitmap
 * owns a copy of its pixels and remains valid after the source page/document
 * is released.
 */
EXTRACTPDF_API extractpdf_status extractpdf_render_page(
    extractpdf_page *page,
    const extractpdf_render_options *options,
    extractpdf_bitmap **out_bitmap);

EXTRACTPDF_API extractpdf_status extractpdf_bitmap_get_info(
    const extractpdf_bitmap *bitmap,
    extractpdf_bitmap_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_bitmap_get_pixels(
    const extractpdf_bitmap *bitmap,
    const unsigned char **out_pixels,
    size_t *out_size);

EXTRACTPDF_API const char *extractpdf_status_string(
    extractpdf_status status);

EXTRACTPDF_API void extractpdf_drop_bitmap(
    extractpdf_bitmap *bitmap);

EXTRACTPDF_API void extractpdf_drop_page(
    extractpdf_page *page);

EXTRACTPDF_API void extractpdf_close(
    extractpdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
