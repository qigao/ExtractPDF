#ifndef EXTRACTPDF_PDF_OBJECT_COMMON_H
#define EXTRACTPDF_PDF_OBJECT_COMMON_H

#include "pdf_internal.h"

#include <stdint.h>

int extractpdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value);

extractpdf_status extractpdf_pdf_read_rect(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    fz_matrix page_ctm,
    extractpdf_rect *out_rect);

extractpdf_status extractpdf_pdf_read_optional_uint32(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    uint32_t missing_value,
    uint32_t *out_value);

#endif
