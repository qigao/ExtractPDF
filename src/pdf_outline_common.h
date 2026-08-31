#ifndef QUANTAPDF_PDF_OUTLINE_COMMON_H
#define QUANTAPDF_PDF_OUTLINE_COMMON_H

#include "pdf_internal.h"

typedef quantapdf_status (*quantapdf_pdf_outline_visit_fn)(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *item,
    size_t preorder_index,
    void *user);

quantapdf_status quantapdf_pdf_outline_walk_strict(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_outline_visit_fn visit,
    void *user,
    size_t *out_count);

#endif
