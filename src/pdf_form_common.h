#ifndef EXTRACTPDF_PDF_FORM_COMMON_H
#define EXTRACTPDF_PDF_FORM_COMMON_H

#include "pdf_object_common.h"

typedef struct extractpdf_pdf_form_model {
    size_t field_count;
    size_t widget_count;
} extractpdf_pdf_form_model;

extractpdf_status extractpdf_pdf_form_parse(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model **out_model);

void extractpdf_pdf_form_drop_model(
    extractpdf_pdf_form_model *model);

#endif
