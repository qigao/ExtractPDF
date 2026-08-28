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
typedef struct extractpdf_text_page extractpdf_text_page;
typedef struct extractpdf_image_page extractpdf_image_page;
typedef struct extractpdf_link_page extractpdf_link_page;
typedef struct extractpdf_output extractpdf_output;

typedef struct extractpdf_point {
    float x;
    float y;
} extractpdf_point;

typedef struct extractpdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} extractpdf_rect;

typedef struct extractpdf_quad {
    extractpdf_point ul;
    extractpdf_point ur;
    extractpdf_point ll;
    extractpdf_point lr;
} extractpdf_quad;

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

typedef struct extractpdf_text_block_info {
    size_t struct_size;
    extractpdf_rect bounds;
} extractpdf_text_block_info;

typedef struct extractpdf_text_line_info {
    size_t struct_size;
    extractpdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
} extractpdf_text_line_info;

typedef struct extractpdf_text_span_info {
    size_t struct_size;
    extractpdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
} extractpdf_text_span_info;

typedef struct extractpdf_search_result {
    size_t struct_size;
    extractpdf_quad quad;
} extractpdf_search_result;

typedef struct extractpdf_image_info {
    size_t struct_size;
    extractpdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} extractpdf_image_info;

typedef enum extractpdf_link_kind {
    EXTRACTPDF_LINK_URI = 1,
    EXTRACTPDF_LINK_INTERNAL = 2
} extractpdf_link_kind;

typedef struct extractpdf_link_info {
    size_t struct_size;
    extractpdf_rect hotspot;
    extractpdf_link_kind kind;
    int target_page;
    extractpdf_point target;
} extractpdf_link_info;

typedef enum extractpdf_metadata_field {
    EXTRACTPDF_METADATA_TITLE = 1,
    EXTRACTPDF_METADATA_AUTHOR = 2,
    EXTRACTPDF_METADATA_SUBJECT = 3,
    EXTRACTPDF_METADATA_KEYWORDS = 4,
    EXTRACTPDF_METADATA_CREATOR = 5,
    EXTRACTPDF_METADATA_PRODUCER = 6,
    EXTRACTPDF_METADATA_CREATION_DATE = 7,
    EXTRACTPDF_METADATA_MODIFICATION_DATE = 8
} extractpdf_metadata_field;

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

EXTRACTPDF_API extractpdf_status extractpdf_document_metadata(
    extractpdf_document *document,
    extractpdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_export_pages(
    extractpdf_document *document,
    const int *page_indices,
    size_t page_count,
    extractpdf_output **out_output);

EXTRACTPDF_API extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output);

EXTRACTPDF_API extractpdf_status extractpdf_merge_outputs(
    const extractpdf_output *const *inputs,
    size_t input_count,
    extractpdf_output **out_output);

EXTRACTPDF_API extractpdf_status extractpdf_output_data(
    const extractpdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);

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

EXTRACTPDF_API extractpdf_status extractpdf_extract_text(
    extractpdf_page *page,
    char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_extract_structured_text(
    extractpdf_page *page,
    extractpdf_text_page **out_text);

EXTRACTPDF_API extractpdf_status extractpdf_text_block_count(
    const extractpdf_text_page *text,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_block_info(
    const extractpdf_text_page *text,
    size_t block_index,
    extractpdf_text_block_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_line_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_line_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    extractpdf_text_line_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_span_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_span_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    extractpdf_text_span_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_span_text(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_text_search(
    const extractpdf_text_page *text,
    const char *needle_utf8,
    extractpdf_search_result *results,
    size_t capacity,
    size_t *out_count);

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

EXTRACTPDF_API extractpdf_status extractpdf_image_render(
    const extractpdf_image_page *images,
    size_t index,
    extractpdf_bitmap **out_bitmap);

EXTRACTPDF_API extractpdf_status extractpdf_extract_links(
    extractpdf_page *page,
    extractpdf_link_page **out_links);

EXTRACTPDF_API extractpdf_status extractpdf_link_count(
    const extractpdf_link_page *links,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_link_get_info(
    const extractpdf_link_page *links,
    size_t index,
    extractpdf_link_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_link_uri(
    const extractpdf_link_page *links,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API const char *extractpdf_status_string(
    extractpdf_status status);

EXTRACTPDF_API void extractpdf_free(
    void *memory);

EXTRACTPDF_API void extractpdf_drop_output(
    extractpdf_output *output);

EXTRACTPDF_API void extractpdf_drop_text_page(
    extractpdf_text_page *text);

EXTRACTPDF_API void extractpdf_drop_image_page(
    extractpdf_image_page *images);

EXTRACTPDF_API void extractpdf_drop_link_page(
    extractpdf_link_page *links);

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
