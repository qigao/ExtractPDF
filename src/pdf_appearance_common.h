#ifndef EXTRACTPDF_PDF_APPEARANCE_COMMON_H
#define EXTRACTPDF_PDF_APPEARANCE_COMMON_H

#include "pdf_object_common.h"

typedef struct extractpdf_pdf_appearance_view {
    int stateful;
    char *state_name;
    size_t state_name_size;
    fz_rect rect;
    fz_rect bbox;
    fz_matrix matrix;
    fz_matrix placement;
} extractpdf_pdf_appearance_view;

extractpdf_status extractpdf_pdf_appearance_resolve(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *annotation,
    extractpdf_pdf_appearance_view *out_view,
    pdf_obj **out_form);

void extractpdf_pdf_appearance_drop_view(
    extractpdf_pdf_appearance_view *view);

#endif
