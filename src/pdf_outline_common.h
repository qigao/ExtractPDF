#ifndef EXTRACTPDF_PDF_OUTLINE_COMMON_H
#define EXTRACTPDF_PDF_OUTLINE_COMMON_H

#include "pdf_internal.h"

typedef extractpdf_status (*extractpdf_pdf_outline_visit_fn)(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *item,
    size_t preorder_index,
    void *user);

extractpdf_status extractpdf_pdf_outline_walk_strict(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_outline_visit_fn visit,
    void *user,
    size_t *out_count);

#endif
