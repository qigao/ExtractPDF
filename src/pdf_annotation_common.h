#ifndef EXTRACTPDF_PDF_ANNOTATION_COMMON_H
#define EXTRACTPDF_PDF_ANNOTATION_COMMON_H

#include "pdf_internal.h"

typedef struct extractpdf_pdf_annotation_view {
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
    int has_contents;
} extractpdf_pdf_annotation_view;

int extractpdf_pdf_annotation_classify(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type *out_type);

extractpdf_status extractpdf_pdf_annotation_read_view(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type type,
    fz_matrix page_ctm,
    extractpdf_pdf_annotation_view *out_view);

#endif
