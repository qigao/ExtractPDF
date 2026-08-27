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
    int width;
    int height;
    int stride;
    extractpdf_pixel_format pixel_format;
    size_t data_size;
    unsigned char *pixels;
};

extractpdf_status extractpdf_status_from_mupdf(int code);

#endif
