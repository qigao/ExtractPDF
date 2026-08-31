#ifndef QUANTAPDF_PDF_CROP_INTERNAL_H
#define QUANTAPDF_PDF_CROP_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct quantapdf_pdf_crop_plan {
    int page_index;
    quantapdf_rect requested_public;
    fz_rect requested_pdf;
    int changed;
} quantapdf_pdf_crop_plan;

quantapdf_status quantapdf_pdf_crop_check_security(
    fz_context *ctx,
    pdf_document *document);

quantapdf_status quantapdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_pdf_crop_plan *plans,
    int *out_any_changed);

#endif
