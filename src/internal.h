#ifndef QUANTAPDF_INTERNAL_H
#define QUANTAPDF_INTERNAL_H

#include <quantapdf/quantapdf.h>
#include <mupdf/fitz.h>

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

quantapdf_status quantapdf_status_from_backend(int code);
quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit);
#endif
