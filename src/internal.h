#ifndef EXTRACTPDF_INTERNAL_H
#define EXTRACTPDF_INTERNAL_H

#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>

struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};

struct extractpdf_page {
    extractpdf_document *document;
    fz_page *page;
};

struct extractpdf_bitmap {
    extractpdf_document *document;
    fz_pixmap *pixmap;
};

typedef struct extractpdf_text_block_internal {
    extractpdf_rect bounds;
    size_t first_line;
    size_t line_count;
} extractpdf_text_block_internal;

typedef struct extractpdf_text_line_internal {
    extractpdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
    size_t first_span;
    size_t span_count;
} extractpdf_text_line_internal;

typedef struct extractpdf_text_span_internal {
    extractpdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
    uint16_t flags;
    size_t first_char;
    size_t char_count;
    size_t text_offset;
    size_t text_size;
} extractpdf_text_span_internal;

typedef struct extractpdf_text_char_internal {
    uint32_t codepoint;
    uint16_t bidi;
    uint16_t flags;
    fz_quad quad;
    size_t span_index;
} extractpdf_text_char_internal;

struct extractpdf_text_page {
    extractpdf_text_block_internal *blocks;
    extractpdf_text_line_internal *lines;
    extractpdf_text_span_internal *spans;
    extractpdf_text_char_internal *chars;
    char *strings;
    size_t block_count;
    size_t line_count;
    size_t span_count;
    size_t char_count;
    size_t string_size;
};

typedef struct extractpdf_image_occurrence_internal {
    fz_image *image;
    extractpdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} extractpdf_image_occurrence_internal;

struct extractpdf_image_page {
    extractpdf_document *document;
    extractpdf_image_occurrence_internal *items;
    size_t count;
    size_t capacity;
    int oom;
};

extractpdf_status extractpdf_status_from_mupdf(int code);

#endif
