#ifndef EXTRACTPDF_INTERNAL_H
#define EXTRACTPDF_INTERNAL_H

#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>

struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};

extractpdf_status extractpdf_status_from_mupdf(int code);

#endif
