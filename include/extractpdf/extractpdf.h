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
typedef struct extractpdf_outline extractpdf_outline;
typedef struct extractpdf_annotation_page extractpdf_annotation_page;
typedef struct extractpdf_pdf_edit extractpdf_pdf_edit;
typedef struct extractpdf_form extractpdf_form;

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

typedef struct extractpdf_page_crop {
    size_t struct_size;
    int page_index;
    extractpdf_rect bounds;
} extractpdf_page_crop;

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

typedef enum extractpdf_outline_destination_kind {
    EXTRACTPDF_OUTLINE_DESTINATION_NONE = 0,
    EXTRACTPDF_OUTLINE_DESTINATION_INTERNAL = 1,
    EXTRACTPDF_OUTLINE_DESTINATION_URI = 2
} extractpdf_outline_destination_kind;

typedef struct extractpdf_outline_info {
    size_t struct_size;
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    extractpdf_outline_destination_kind destination_kind;
    int target_page;
    extractpdf_point target;
    int is_open;
} extractpdf_outline_info;

typedef enum extractpdf_annotation_type {
    EXTRACTPDF_ANNOTATION_UNKNOWN = 0,
    EXTRACTPDF_ANNOTATION_TEXT = 1,
    EXTRACTPDF_ANNOTATION_FREE_TEXT = 2,
    EXTRACTPDF_ANNOTATION_LINE = 3,
    EXTRACTPDF_ANNOTATION_SQUARE = 4,
    EXTRACTPDF_ANNOTATION_CIRCLE = 5,
    EXTRACTPDF_ANNOTATION_POLYGON = 6,
    EXTRACTPDF_ANNOTATION_POLY_LINE = 7,
    EXTRACTPDF_ANNOTATION_HIGHLIGHT = 8,
    EXTRACTPDF_ANNOTATION_UNDERLINE = 9,
    EXTRACTPDF_ANNOTATION_SQUIGGLY = 10,
    EXTRACTPDF_ANNOTATION_STRIKE_OUT = 11,
    EXTRACTPDF_ANNOTATION_REDACT = 12,
    EXTRACTPDF_ANNOTATION_STAMP = 13,
    EXTRACTPDF_ANNOTATION_CARET = 14,
    EXTRACTPDF_ANNOTATION_INK = 15,
    EXTRACTPDF_ANNOTATION_FILE_ATTACHMENT = 16,
    EXTRACTPDF_ANNOTATION_SOUND = 17,
    EXTRACTPDF_ANNOTATION_MOVIE = 18,
    EXTRACTPDF_ANNOTATION_RICH_MEDIA = 19,
    EXTRACTPDF_ANNOTATION_SCREEN = 20,
    EXTRACTPDF_ANNOTATION_PRINTER_MARK = 21,
    EXTRACTPDF_ANNOTATION_TRAP_NET = 22,
    EXTRACTPDF_ANNOTATION_WATERMARK = 23,
    EXTRACTPDF_ANNOTATION_3D = 24,
    EXTRACTPDF_ANNOTATION_PROJECTION = 25
} extractpdf_annotation_type;

typedef struct extractpdf_annotation_info {
    size_t struct_size;
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
} extractpdf_annotation_info;

typedef struct extractpdf_annotation_ref {
    uint64_t opaque[2];
} extractpdf_annotation_ref;

typedef enum extractpdf_annotation_update_field {
    EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS = 1u << 0,
    EXTRACTPDF_ANNOTATION_UPDATE_FLAGS = 1u << 1,
    EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS = 1u << 2
} extractpdf_annotation_update_field;

typedef struct extractpdf_annotation_create_options {
    size_t struct_size;
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} extractpdf_annotation_create_options;

typedef struct extractpdf_annotation_update {
    size_t struct_size;
    uint32_t fields;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} extractpdf_annotation_update;

typedef enum extractpdf_form_field_type {
    EXTRACTPDF_FORM_FIELD_UNKNOWN = 0,
    EXTRACTPDF_FORM_FIELD_PUSH_BUTTON = 1,
    EXTRACTPDF_FORM_FIELD_CHECKBOX = 2,
    EXTRACTPDF_FORM_FIELD_RADIO_BUTTON = 3,
    EXTRACTPDF_FORM_FIELD_TEXT = 4,
    EXTRACTPDF_FORM_FIELD_COMBO_BOX = 5,
    EXTRACTPDF_FORM_FIELD_LIST_BOX = 6,
    EXTRACTPDF_FORM_FIELD_SIGNATURE = 7
} extractpdf_form_field_type;

typedef enum extractpdf_form_value_presence {
    EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    EXTRACTPDF_FORM_VALUE_MISSING = 1,
    EXTRACTPDF_FORM_VALUE_PRESENT = 2
} extractpdf_form_value_presence;

typedef enum extractpdf_form_value_kind {
    EXTRACTPDF_FORM_VALUE_UTF8 = 1,
    EXTRACTPDF_FORM_VALUE_OPTION = 2
} extractpdf_form_value_kind;

typedef struct extractpdf_form_value_info {
    size_t struct_size;
    extractpdf_form_value_kind kind;
    size_t option_index;
} extractpdf_form_value_info;

typedef enum extractpdf_form_option_kind {
    EXTRACTPDF_FORM_OPTION_BUTTON_STATE = 1,
    EXTRACTPDF_FORM_OPTION_CHOICE = 2
} extractpdf_form_option_kind;

typedef struct extractpdf_form_option_info {
    size_t struct_size;
    extractpdf_form_option_kind kind;
} extractpdf_form_option_info;

typedef struct extractpdf_form_field_info {
    size_t struct_size;
    extractpdf_form_field_type type;
    uint32_t flags;
    extractpdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} extractpdf_form_field_info;

typedef struct extractpdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_form_widget_info;

typedef struct extractpdf_form_field_ref {
    uint64_t opaque[2];
} extractpdf_form_field_ref;

typedef struct extractpdf_form_value_input {
    size_t struct_size;
    extractpdf_form_value_kind kind;
    size_t option_index;
    const char *utf8;
    size_t utf8_size;
} extractpdf_form_value_input;

typedef struct extractpdf_form_value_update {
    size_t struct_size;
    extractpdf_form_value_presence presence;
    const extractpdf_form_value_input *values;
    size_t value_count;
} extractpdf_form_value_update;

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
    EXTRACTPDF_ERROR_MUPDF = 7,
    EXTRACTPDF_ERROR_STATE = 8
} extractpdf_status;

EXTRACTPDF_API extractpdf_status extractpdf_open(
    const char *filename,
    const char *password,
    extractpdf_document **out_document);

EXTRACTPDF_API extractpdf_status extractpdf_page_count(
    extractpdf_document *document,
    int *out_page_count);

EXTRACTPDF_API extractpdf_status extractpdf_document_form(
    extractpdf_document *document,
    extractpdf_form **out_form);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_count(
    const extractpdf_form *form,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_get_info(
    const extractpdf_form *form,
    size_t field_index,
    extractpdf_form_field_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_name(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_label(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_value_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    extractpdf_form_value_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_value_utf8(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_option_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    extractpdf_form_option_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_option_export(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_form_field_option_display(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_form_widget_count(
    const extractpdf_form *form,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_form_widget_get_info(
    const extractpdf_form *form,
    size_t widget_index,
    extractpdf_form_widget_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_document_metadata(
    extractpdf_document *document,
    extractpdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_document_outline(
    extractpdf_document *document,
    extractpdf_outline **out_outline);

EXTRACTPDF_API extractpdf_status extractpdf_outline_count(
    const extractpdf_outline *outline,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_outline_get_info(
    const extractpdf_outline *outline,
    size_t index,
    extractpdf_outline_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_outline_title(
    const extractpdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_outline_uri(
    const extractpdf_outline *outline,
    size_t index,
    const char **out_utf8,
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

EXTRACTPDF_API extractpdf_status extractpdf_crop_pages(
    extractpdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_output **out_output);

EXTRACTPDF_API extractpdf_status extractpdf_output_data(
    const extractpdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_begin(
    extractpdf_document *document,
    extractpdf_pdf_edit **out_edit);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_form **out_form);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_field_ref_at(
    extractpdf_pdf_edit *edit,
    size_t field_index,
    extractpdf_form_field_ref *out_ref);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_count(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_ref_at(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t index,
    extractpdf_annotation_ref *out_ref);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_get_info(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    extractpdf_annotation_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_contents(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_create(
    extractpdf_pdf_edit *edit,
    int page_index,
    const extractpdf_annotation_create_options *options,
    extractpdf_annotation_ref *out_ref);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_update(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    const extractpdf_annotation_update *update);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_annotation_delete(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_output **out_output);

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

EXTRACTPDF_API extractpdf_status extractpdf_extract_annotations(
    extractpdf_page *page,
    extractpdf_annotation_page **out_annotations);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_count(
    const extractpdf_annotation_page *annotations,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_get_info(
    const extractpdf_annotation_page *annotations,
    size_t index,
    extractpdf_annotation_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_annotation_contents(
    const extractpdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API const char *extractpdf_status_string(
    extractpdf_status status);

EXTRACTPDF_API void extractpdf_free(
    void *memory);

EXTRACTPDF_API void extractpdf_drop_output(
    extractpdf_output *output);

EXTRACTPDF_API void extractpdf_drop_pdf_edit(
    extractpdf_pdf_edit *edit);

EXTRACTPDF_API void extractpdf_drop_form(
    extractpdf_form *form);

EXTRACTPDF_API void extractpdf_drop_text_page(
    extractpdf_text_page *text);

EXTRACTPDF_API void extractpdf_drop_image_page(
    extractpdf_image_page *images);

EXTRACTPDF_API void extractpdf_drop_link_page(
    extractpdf_link_page *links);

EXTRACTPDF_API void extractpdf_drop_outline(
    extractpdf_outline *outline);

EXTRACTPDF_API void extractpdf_drop_annotation_page(
    extractpdf_annotation_page *annotations);

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