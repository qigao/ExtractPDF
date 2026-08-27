#ifndef EXTRACTPDF_EXTRACTPDF_H
#define EXTRACTPDF_EXTRACTPDF_H

#include <stddef.h>

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

typedef struct extractpdf_render_options {
    size_t struct_size;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    extractpdf_rect clip;
    int alpha;
} extractpdf_render_options;

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

EXTRACTPDF_API extractpdf_status extractpdf_page_bounds(
    extractpdf_page *page,
    extractpdf_rect *out_bounds);

EXTRACTPDF_API extractpdf_status extractpdf_page_box_bounds(
    extractpdf_page *page,
    extractpdf_page_box box,
    extractpdf_rect *out_bounds);

EXTRACTPDF_API extractpdf_status extractpdf_render_page(
    extractpdf_page *page,
    extractpdf_bitmap **out_bitmap);

EXTRACTPDF_API extractpdf_status extractpdf_render_page_with_options(
    extractpdf_page *page,
    const extractpdf_render_options *options,
    extractpdf_bitmap **out_bitmap);

EXTRACTPDF_API extractpdf_status extractpdf_render_thumbnail(
    extractpdf_page *page,
    int max_width,
    int max_height,
    extractpdf_bitmap **out_bitmap);

EXTRACTPDF_API extractpdf_status extractpdf_bitmap_dimensions(
    extractpdf_bitmap *bitmap,
    int *out_width,
    int *out_height,
    int *out_stride,
    int *out_components);

EXTRACTPDF_API extractpdf_status extractpdf_bitmap_data(
    extractpdf_bitmap *bitmap,
    const unsigned char **out_data,
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
