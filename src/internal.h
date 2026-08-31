#ifndef QUANTAPDF_INTERNAL_H
#define QUANTAPDF_INTERNAL_H

#include <quantapdf/quantapdf.h>
#include <mupdf/fitz.h>

typedef struct quantapdf_pdfium_document quantapdf_pdfium_document;
typedef struct quantapdf_pdfium_page quantapdf_pdfium_page;
typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;

#if defined(QUANTAPDF_TESTING)
typedef enum quantapdf_test_poster_fault_internal {
    QUANTAPDF_TEST_POSTER_FAULT_NONE = 0,
    QUANTAPDF_TEST_POSTER_FAULT_ANNOTATION_PREFLIGHT = 1,
    QUANTAPDF_TEST_POSTER_FAULT_WIDGET_PREFLIGHT = 2,
    QUANTAPDF_TEST_POSTER_FAULT_NAVIGATION_PREFLIGHT = 3
} quantapdf_test_poster_fault_internal;
#endif

struct quantapdf_document {
    fz_context *ctx;
    fz_document *doc;
    unsigned char *source_data;
    size_t source_size;
    quantapdf_pdfium_document *pdfium_document;
    quantapdf_qpdf_document *qpdf_document;
    char *password;
#if defined(QUANTAPDF_TESTING)
    int test_poster_fault;
#endif
};

struct quantapdf_page {
    quantapdf_document *document;
    fz_page *page;
    quantapdf_pdfium_page *pdfium_page;
    int page_index;
};

struct quantapdf_bitmap {
    quantapdf_document *document;
    fz_pixmap *pixmap;
};

typedef struct quantapdf_text_block_internal {
    quantapdf_rect bounds;
    size_t first_line;
    size_t line_count;
} quantapdf_text_block_internal;

typedef struct quantapdf_text_line_internal {
    quantapdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
    size_t first_span;
    size_t span_count;
} quantapdf_text_line_internal;

typedef struct quantapdf_text_span_internal {
    quantapdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
    uint16_t flags;
    size_t first_char;
    size_t char_count;
    size_t text_offset;
    size_t text_size;
} quantapdf_text_span_internal;

typedef struct quantapdf_text_char_internal {
    uint32_t codepoint;
    uint16_t bidi;
    uint16_t flags;
    fz_quad quad;
    size_t span_index;
} quantapdf_text_char_internal;

struct quantapdf_text_page {
    quantapdf_text_block_internal *blocks;
    quantapdf_text_line_internal *lines;
    quantapdf_text_span_internal *spans;
    quantapdf_text_char_internal *chars;
    char *strings;
    size_t block_count;
    size_t line_count;
    size_t span_count;
    size_t char_count;
    size_t string_size;
};

typedef struct quantapdf_image_occurrence_internal {
    fz_image *image;
    quantapdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} quantapdf_image_occurrence_internal;

struct quantapdf_image_page {
    quantapdf_document *document;
    quantapdf_image_occurrence_internal *items;
    size_t count;
    size_t capacity;
    int oom;
};

typedef struct quantapdf_link_internal {
    quantapdf_rect hotspot;
    quantapdf_link_kind kind;
    int target_page;
    quantapdf_point target;
    char *uri;
    size_t uri_size;
} quantapdf_link_internal;

struct quantapdf_link_page {
    quantapdf_link_internal *items;
    size_t count;
};

struct quantapdf_output {
    unsigned char *data;
    size_t size;
};

quantapdf_status quantapdf_status_from_backend(int code);
quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit);

#endif
