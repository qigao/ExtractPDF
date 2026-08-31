#ifndef QUANTAPDF_PDF_TRIM_INTERNAL_H
#define QUANTAPDF_PDF_TRIM_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct quantapdf_pdf_trim_plan {
    int page_index;
    quantapdf_rect requested_public;
    fz_rect requested_media_pdf;
    fz_rect output_visible_pdf;
    int changed;
    int frame_changed;
} quantapdf_pdf_trim_plan;

quantapdf_status quantapdf_pdf_trim_check_security(
    fz_context *ctx,
    pdf_document *document);

quantapdf_status quantapdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_pdf_trim_plan *plans,
    int *out_any_changed);

#endif
