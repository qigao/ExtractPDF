#ifndef EXTRACTPDF_PDF_CROP_INTERNAL_H
#define EXTRACTPDF_PDF_CROP_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct extractpdf_pdf_crop_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_pdf;
    int changed;
} extractpdf_pdf_crop_plan;

extractpdf_status extractpdf_pdf_crop_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_pdf_crop_plan *plans,
    int *out_any_changed);

#endif
