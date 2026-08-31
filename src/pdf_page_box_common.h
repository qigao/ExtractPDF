#ifndef QUANTAPDF_PDF_PAGE_BOX_COMMON_H
#define QUANTAPDF_PDF_PAGE_BOX_COMMON_H

#include "pdf_internal.h"

typedef struct quantapdf_pdf_page_box_view {
    pdf_obj *page_obj;
    fz_rect media_pdf;
    fz_rect crop_pdf;
    fz_rect visible_pdf;
    fz_matrix pdf_to_public;
    quantapdf_rect media_public;
    quantapdf_rect visible_public;
    int has_explicit_crop;
    int rotate_degrees;
    float user_unit;
} quantapdf_pdf_page_box_view;

quantapdf_status quantapdf_pdf_page_box_resolve(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    quantapdf_pdf_page_box_view *out_view);

#endif
