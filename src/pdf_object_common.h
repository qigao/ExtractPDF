#ifndef QUANTAPDF_PDF_OBJECT_COMMON_H
#define QUANTAPDF_PDF_OBJECT_COMMON_H

#include "pdf_internal.h"

#include <stdint.h>

int quantapdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value);

quantapdf_status quantapdf_pdf_read_rect(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    fz_matrix page_ctm,
    quantapdf_rect *out_rect);

quantapdf_status quantapdf_pdf_read_optional_uint32(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    uint32_t missing_value,
    uint32_t *out_value);

#endif
