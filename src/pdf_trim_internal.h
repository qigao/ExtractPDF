#ifndef EXTRACTPDF_PDF_TRIM_INTERNAL_H
#define EXTRACTPDF_PDF_TRIM_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct extractpdf_pdf_trim_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_media_pdf;
    fz_rect output_visible_pdf;
    int changed;
    int frame_changed;
} extractpdf_pdf_trim_plan;

extractpdf_status extractpdf_pdf_trim_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_pdf_trim_plan *plans,
    int *out_any_changed);

#endif
