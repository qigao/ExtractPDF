#ifndef QUANTAPDF_QUANTAPDF_H
#define QUANTAPDF_QUANTAPDF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(QUANTAPDF_SHARED)
#  if defined(QUANTAPDF_BUILDING_LIBRARY)
#    define QUANTAPDF_API __declspec(dllexport)
#  else
#    define QUANTAPDF_API __declspec(dllimport)
#  endif
#else
#  define QUANTAPDF_API
#endif

#define QUANTAPDF_VERSION_MAJOR 2
#define QUANTAPDF_VERSION_MINOR 0
#define QUANTAPDF_VERSION_PATCH 0
#define QUANTAPDF_ABI_VERSION 2

typedef struct quantapdf_document quantapdf_document;
typedef struct quantapdf_page quantapdf_page;
typedef struct quantapdf_bitmap quantapdf_bitmap;
typedef struct quantapdf_text_page quantapdf_text_page;
typedef struct quantapdf_image_page quantapdf_image_page;
typedef struct quantapdf_link_page quantapdf_link_page;
typedef struct quantapdf_output quantapdf_output;
typedef struct quantapdf_outline quantapdf_outline;
typedef struct quantapdf_annotation_page quantapdf_annotation_page;
typedef struct quantapdf_pdf_edit quantapdf_pdf_edit;
typedef struct quantapdf_form quantapdf_form;

typedef struct quantapdf_point {
    float x;
    float y;
} quantapdf_point;

typedef struct quantapdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} quantapdf_rect;

typedef struct quantapdf_page_crop {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_crop;

typedef struct quantapdf_page_trim {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_trim;

typedef struct quantapdf_page_poster_split {
    size_t struct_size;
    int page_index;
    size_t columns;
    size_t rows;
} quantapdf_page_poster_split;

/*
 * These types are traversed as C arrays and therefore have fixed V1 layouts.
 * A future extension must use a new element type/API or an explicit stride.
 */
#define QUANTAPDF_PAGE_CROP_V1_MIN_SIZE \
    (offsetof(quantapdf_page_crop, bounds) + sizeof(quantapdf_rect))
#define QUANTAPDF_PAGE_CROP_V1_SIZE (sizeof(quantapdf_page_crop))
#define QUANTAPDF_PAGE_TRIM_V1_MIN_SIZE \
    (offsetof(quantapdf_page_trim, bounds) + sizeof(quantapdf_rect))
#define QUANTAPDF_PAGE_TRIM_V1_SIZE (sizeof(quantapdf_page_trim))
#define QUANTAPDF_PAGE_POSTER_SPLIT_V1_MIN_SIZE \
    (offsetof(quantapdf_page_poster_split, rows) + sizeof(size_t))
#define QUANTAPDF_PAGE_POSTER_SPLIT_V1_SIZE \
    (sizeof(quantapdf_page_poster_split))

typedef struct quantapdf_quad {
    quantapdf_point ul;
    quantapdf_point ur;
    quantapdf_point ll;
    quantapdf_point lr;
} quantapdf_quad;

typedef enum quantapdf_page_box {
    QUANTAPDF_PAGE_BOX_MEDIA = 0,
    QUANTAPDF_PAGE_BOX_CROP = 1
} quantapdf_page_box;

typedef struct quantapdf_render_options {
    size_t struct_size;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    quantapdf_rect clip;
    int alpha;
} quantapdf_render_options;

typedef struct quantapdf_text_block_info {
    size_t struct_size;
    quantapdf_rect bounds;
} quantapdf_text_block_info;

typedef struct quantapdf_text_line_info {
    size_t struct_size;
    quantapdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
} quantapdf_text_line_info;

typedef struct quantapdf_text_span_info {
    size_t struct_size;
    quantapdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
} quantapdf_text_span_info;

typedef struct quantapdf_search_result {
    size_t struct_size;
    quantapdf_quad quad;
} quantapdf_search_result;

#define QUANTAPDF_SEARCH_RESULT_V1_MIN_SIZE \
    (offsetof(quantapdf_search_result, quad) + sizeof(quantapdf_quad))
#define QUANTAPDF_SEARCH_RESULT_V1_SIZE (sizeof(quantapdf_search_result))

typedef struct quantapdf_image_info {
    size_t struct_size;
    quantapdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} quantapdf_image_info;

typedef enum quantapdf_link_kind {
    QUANTAPDF_LINK_URI = 1,
    QUANTAPDF_LINK_INTERNAL = 2
} quantapdf_link_kind;

typedef struct quantapdf_link_info {
    size_t struct_size;
    quantapdf_rect hotspot;
    quantapdf_link_kind kind;
    int target_page;
    quantapdf_point target;
} quantapdf_link_info;

typedef enum quantapdf_outline_destination_kind {
    QUANTAPDF_OUTLINE_DESTINATION_NONE = 0,
    QUANTAPDF_OUTLINE_DESTINATION_INTERNAL = 1,
    QUANTAPDF_OUTLINE_DESTINATION_URI = 2
} quantapdf_outline_destination_kind;

typedef struct quantapdf_outline_info {
    size_t struct_size;
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    int is_open;
} quantapdf_outline_info;

typedef enum quantapdf_annotation_type {
    QUANTAPDF_ANNOTATION_UNKNOWN = 0,
    QUANTAPDF_ANNOTATION_TEXT = 1,
    QUANTAPDF_ANNOTATION_FREE_TEXT = 2,
    QUANTAPDF_ANNOTATION_LINE = 3,
    QUANTAPDF_ANNOTATION_SQUARE = 4,
    QUANTAPDF_ANNOTATION_CIRCLE = 5,
    QUANTAPDF_ANNOTATION_POLYGON = 6,
    QUANTAPDF_ANNOTATION_POLY_LINE = 7,
    QUANTAPDF_ANNOTATION_HIGHLIGHT = 8,
    QUANTAPDF_ANNOTATION_UNDERLINE = 9,
    QUANTAPDF_ANNOTATION_SQUIGGLY = 10,
    QUANTAPDF_ANNOTATION_STRIKE_OUT = 11,
    QUANTAPDF_ANNOTATION_REDACT = 12,
    QUANTAPDF_ANNOTATION_STAMP = 13,
    QUANTAPDF_ANNOTATION_CARET = 14,
    QUANTAPDF_ANNOTATION_INK = 15,
    QUANTAPDF_ANNOTATION_FILE_ATTACHMENT = 16,
    QUANTAPDF_ANNOTATION_SOUND = 17,
    QUANTAPDF_ANNOTATION_MOVIE = 18,
    QUANTAPDF_ANNOTATION_RICH_MEDIA = 19,
    QUANTAPDF_ANNOTATION_SCREEN = 20,
    QUANTAPDF_ANNOTATION_PRINTER_MARK = 21,
    QUANTAPDF_ANNOTATION_TRAP_NET = 22,
    QUANTAPDF_ANNOTATION_WATERMARK = 23,
    QUANTAPDF_ANNOTATION_3D = 24,
    QUANTAPDF_ANNOTATION_PROJECTION = 25
} quantapdf_annotation_type;

typedef struct quantapdf_annotation_info {
    size_t struct_size;
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
} quantapdf_annotation_info;

typedef struct quantapdf_annotation_ref {
    uint64_t opaque[2];
} quantapdf_annotation_ref;

typedef enum quantapdf_annotation_update_field {
    QUANTAPDF_ANNOTATION_UPDATE_BOUNDS = 1u << 0,
    QUANTAPDF_ANNOTATION_UPDATE_FLAGS = 1u << 1,
    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS = 1u << 2
} quantapdf_annotation_update_field;

typedef struct quantapdf_annotation_create_options {
    size_t struct_size;
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_create_options;

typedef struct quantapdf_annotation_update {
    size_t struct_size;
    uint32_t fields;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_update;

typedef enum quantapdf_form_field_type {
    QUANTAPDF_FORM_FIELD_UNKNOWN = 0,
    QUANTAPDF_FORM_FIELD_PUSH_BUTTON = 1,
    QUANTAPDF_FORM_FIELD_CHECKBOX = 2,
    QUANTAPDF_FORM_FIELD_RADIO_BUTTON = 3,
    QUANTAPDF_FORM_FIELD_TEXT = 4,
    QUANTAPDF_FORM_FIELD_COMBO_BOX = 5,
    QUANTAPDF_FORM_FIELD_LIST_BOX = 6,
    QUANTAPDF_FORM_FIELD_SIGNATURE = 7
} quantapdf_form_field_type;

typedef enum quantapdf_form_value_presence {
    QUANTAPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    QUANTAPDF_FORM_VALUE_MISSING = 1,
    QUANTAPDF_FORM_VALUE_PRESENT = 2
} quantapdf_form_value_presence;

typedef enum quantapdf_form_value_kind {
    QUANTAPDF_FORM_VALUE_UTF8 = 1,
    QUANTAPDF_FORM_VALUE_OPTION = 2
} quantapdf_form_value_kind;

typedef struct quantapdf_form_value_info {
    size_t struct_size;
    quantapdf_form_value_kind kind;
    size_t option_index;
} quantapdf_form_value_info;

typedef enum quantapdf_form_option_kind {
    QUANTAPDF_FORM_OPTION_BUTTON_STATE = 1,
    QUANTAPDF_FORM_OPTION_CHOICE = 2
} quantapdf_form_option_kind;

typedef struct quantapdf_form_option_info {
    size_t struct_size;
    quantapdf_form_option_kind kind;
} quantapdf_form_option_info;

typedef struct quantapdf_form_field_info {
    size_t struct_size;
    quantapdf_form_field_type type;
    uint32_t flags;
    quantapdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} quantapdf_form_field_info;

typedef struct quantapdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} quantapdf_form_widget_info;

typedef struct quantapdf_form_field_ref {
    uint64_t opaque[2];
} quantapdf_form_field_ref;

typedef struct quantapdf_form_value_input {
    size_t struct_size;
    quantapdf_form_value_kind kind;
    size_t option_index;
    const char *utf8;
    size_t utf8_size;
} quantapdf_form_value_input;

#define QUANTAPDF_FORM_VALUE_INPUT_V1_MIN_SIZE \
    (offsetof(quantapdf_form_value_input, utf8_size) + sizeof(size_t))
#define QUANTAPDF_FORM_VALUE_INPUT_V1_SIZE \
    (sizeof(quantapdf_form_value_input))

typedef struct quantapdf_form_value_update {
    size_t struct_size;
    quantapdf_form_value_presence presence;
    const quantapdf_form_value_input *values;
    size_t value_count;
} quantapdf_form_value_update;

typedef enum quantapdf_metadata_field {
    QUANTAPDF_METADATA_TITLE = 1,
    QUANTAPDF_METADATA_AUTHOR = 2,
    QUANTAPDF_METADATA_SUBJECT = 3,
    QUANTAPDF_METADATA_KEYWORDS = 4,
    QUANTAPDF_METADATA_CREATOR = 5,
    QUANTAPDF_METADATA_PRODUCER = 6,
    QUANTAPDF_METADATA_CREATION_DATE = 7,
    QUANTAPDF_METADATA_MODIFICATION_DATE = 8
} quantapdf_metadata_field;

typedef enum quantapdf_status {
    QUANTAPDF_OK = 0,
    QUANTAPDF_ERROR_ARGUMENT = 1,
    QUANTAPDF_ERROR_IO = 2,
    QUANTAPDF_ERROR_PASSWORD = 3,
    QUANTAPDF_ERROR_FORMAT = 4,
    QUANTAPDF_ERROR_UNSUPPORTED = 5,
    QUANTAPDF_ERROR_NOMEM = 6,
    QUANTAPDF_ERROR_BACKEND = 7,
    QUANTAPDF_ERROR_STATE = 8
} quantapdf_status;

QUANTAPDF_API quantapdf_status quantapdf_open(
    const char *filename,
    const char *password,
    quantapdf_document **out_document);

QUANTAPDF_API quantapdf_status quantapdf_page_count(
    quantapdf_document *document,
    int *out_page_count);

QUANTAPDF_API quantapdf_status quantapdf_document_form(
    quantapdf_document *document,
    quantapdf_form **out_form);

QUANTAPDF_API quantapdf_status quantapdf_form_field_count(
    const quantapdf_form *form,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_form_field_get_info(
    const quantapdf_form *form,
    size_t field_index,
    quantapdf_form_field_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_name(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_label(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_value_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    quantapdf_form_value_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_value_utf8(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    quantapdf_form_option_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_export(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_display(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_widget_count(
    const quantapdf_form *form,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_form_widget_get_info(
    const quantapdf_form *form,
    size_t widget_index,
    quantapdf_form_widget_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline);

QUANTAPDF_API quantapdf_status quantapdf_outline_count(
    const quantapdf_outline *outline,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_outline_get_info(
    const quantapdf_outline *outline,
    size_t index,
    quantapdf_outline_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_outline_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_export_pages(
    quantapdf_document *document,
    const int *page_indices,
    size_t page_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_export_page_range(
    quantapdf_document *document,
    int first_page,
    size_t page_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_output_data(
    const quantapdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_output_save_file(
    const quantapdf_output *output,
    const char *filename);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_begin(
    quantapdf_document *document,
    quantapdf_pdf_edit **out_edit);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_form **out_form);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_field_ref_at(
    quantapdf_pdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_set_values(
    quantapdf_pdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_count(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_ref_at(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_get_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_create(
    quantapdf_pdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_update(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_delete(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_load_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_page **out_page);

QUANTAPDF_API quantapdf_status quantapdf_page_bounds(
    quantapdf_page *page,
    quantapdf_rect *out_bounds);

QUANTAPDF_API quantapdf_status quantapdf_page_box_bounds(
    quantapdf_page *page,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds);

QUANTAPDF_API quantapdf_status quantapdf_render_page(
    quantapdf_page *page,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_render_page_with_options(
    quantapdf_page *page,
    const quantapdf_render_options *options,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_render_thumbnail(
    quantapdf_page *page,
    int max_width,
    int max_height,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_bitmap_dimensions(
    quantapdf_bitmap *bitmap,
    int *out_width,
    int *out_height,
    int *out_stride,
    int *out_components);

QUANTAPDF_API quantapdf_status quantapdf_bitmap_data(
    quantapdf_bitmap *bitmap,
    const unsigned char **out_data,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_text(
    quantapdf_page *page,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_structured_text(
    quantapdf_page *page,
    quantapdf_text_page **out_text);

QUANTAPDF_API quantapdf_status quantapdf_text_block_count(
    const quantapdf_text_page *text,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_block_info(
    const quantapdf_text_page *text,
    size_t block_index,
    quantapdf_text_block_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_line_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_line_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    quantapdf_text_line_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_span_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_span_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    quantapdf_text_span_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_span_text(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_text_search(
    const quantapdf_text_page *text,
    const char *needle_utf8,
    quantapdf_search_result *results,
    size_t capacity,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_extract_images(
    quantapdf_page *page,
    quantapdf_image_page **out_images);

QUANTAPDF_API quantapdf_status quantapdf_image_count(
    const quantapdf_image_page *images,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_image_get_info(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_image_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_image_render(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_extract_links(
    quantapdf_page *page,
    quantapdf_link_page **out_links);

QUANTAPDF_API quantapdf_status quantapdf_link_count(
    const quantapdf_link_page *links,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_link_get_info(
    const quantapdf_link_page *links,
    size_t index,
    quantapdf_link_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_link_uri(
    const quantapdf_link_page *links,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_annotations(
    quantapdf_page *page,
    quantapdf_annotation_page **out_annotations);

QUANTAPDF_API quantapdf_status quantapdf_annotation_count(
    const quantapdf_annotation_page *annotations,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_annotation_get_info(
    const quantapdf_annotation_page *annotations,
    size_t index,
    quantapdf_annotation_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_annotation_contents(
    const quantapdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API const char *quantapdf_status_string(
    quantapdf_status status);

QUANTAPDF_API void quantapdf_free(
    void *memory);

QUANTAPDF_API void quantapdf_drop_output(
    quantapdf_output *output);

QUANTAPDF_API void quantapdf_drop_pdf_edit(
    quantapdf_pdf_edit *edit);

QUANTAPDF_API void quantapdf_drop_form(
    quantapdf_form *form);

QUANTAPDF_API void quantapdf_drop_text_page(
    quantapdf_text_page *text);

QUANTAPDF_API void quantapdf_drop_image_page(
    quantapdf_image_page *images);

QUANTAPDF_API void quantapdf_drop_link_page(
    quantapdf_link_page *links);

QUANTAPDF_API void quantapdf_drop_outline(
    quantapdf_outline *outline);

QUANTAPDF_API void quantapdf_drop_annotation_page(
    quantapdf_annotation_page *annotations);

QUANTAPDF_API void quantapdf_drop_bitmap(
    quantapdf_bitmap *bitmap);

QUANTAPDF_API void quantapdf_drop_page(
    quantapdf_page *page);

QUANTAPDF_API void quantapdf_close(
    quantapdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
