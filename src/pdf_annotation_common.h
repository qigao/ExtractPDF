#ifndef QUANTAPDF_PDF_ANNOTATION_COMMON_H
#define QUANTAPDF_PDF_ANNOTATION_COMMON_H

#include "pdf_internal.h"

typedef struct quantapdf_pdf_annotation_view {
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
    int has_contents;
} quantapdf_pdf_annotation_view;

int quantapdf_pdf_annotation_classify(
    fz_context *ctx,
    pdf_obj *annotation,
    quantapdf_annotation_type *out_type);

quantapdf_status quantapdf_pdf_annotation_read_view(
    fz_context *ctx,
    pdf_obj *annotation,
    quantapdf_annotation_type type,
    fz_matrix page_ctm,
    quantapdf_pdf_annotation_view *out_view);

#endif
