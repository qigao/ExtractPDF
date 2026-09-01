#ifndef QUANTAPDF_INTERNAL_H
#define QUANTAPDF_INTERNAL_H

#include <quantapdf/quantapdf.h>

#include "text_snapshot.h"
#include "image_snapshot.h"
#include "link_snapshot.h"
#include "annotation_snapshot.h"
#include "outline_snapshot.h"

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
    unsigned char *source_data;
    size_t source_size;
    quantapdf_pdfium_document *pdfium_document;
    quantapdf_qpdf_document *qpdf_document;
    char *password;
#if defined(QUANTAPDF_TESTING)
    int test_poster_fault;
    size_t test_image_unique_count;
    size_t test_image_provider_registrations;
    size_t test_image_provider_invocations;
    size_t test_image_decoded_preflight_bytes;
    int test_image_every_provider_once;
    int test_image_fault;
#endif
};

struct quantapdf_page {
    quantapdf_document *document;
    quantapdf_pdfium_page *pdfium_page;
    int page_index;
};

struct quantapdf_bitmap {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    int stride;
    int components;
};

struct quantapdf_output {
    unsigned char *data;
    size_t size;
};

typedef struct quantapdf_composer_page_state {
    float width_points;
    float height_points;
    uint32_t background_argb;
} quantapdf_composer_page_state;

struct quantapdf_composer {
    size_t max_pages;
    size_t max_operations;
    size_t max_resource_bytes;
    quantapdf_composer_page_state *pages;
    size_t page_count;
    size_t page_capacity;
    size_t operation_count;
    size_t resource_bytes;
};

quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit);
#endif
