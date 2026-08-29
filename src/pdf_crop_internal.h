#ifndef EXTRACTPDF_PDF_CROP_INTERNAL_H
#define EXTRACTPDF_PDF_CROP_INTERNAL_H

#include "pdf_internal.h"

typedef struct extractpdf_pdf_crop_page_view {
    pdf_obj *page_obj;
    fz_rect media_pdf;
    fz_rect crop_pdf;
    fz_rect visible_pdf;
    fz_matrix public_to_pdf;
    extractpdf_rect visible_public;
    int rotate_degrees;
    float user_unit;
} extractpdf_pdf_crop_page_view;

typedef struct extractpdf_pdf_crop_plan {
    int page_index;
    extractpdf_rect requested_public;
    fz_rect requested_pdf;
    int changed;
} extractpdf_pdf_crop_plan;

extractpdf_status extractpdf_pdf_crop_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_crop_resolve_page(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_pdf_crop_page_view *out_view);

extractpdf_status extractpdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_pdf_crop_plan *plans,
    int *out_any_changed);

#endif
