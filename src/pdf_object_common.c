#include "pdf_object_common.h"

#include <math.h>
#include <stdint.h>

int quantapdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count;
    int index;

    *out_value = NULL;
    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        pdf_obj *candidate = pdf_dict_get_key(ctx, dictionary, index);
        if (pdf_name_eq(ctx, candidate, key)) {
            *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

quantapdf_status quantapdf_pdf_read_rect(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    fz_matrix page_ctm,
    quantapdf_rect *out_rect)
{
    pdf_obj *rect_obj = NULL;
    float values[4];
    fz_rect raw;
    fz_rect transformed;
    int present;
    int index;

    present = quantapdf_pdf_dict_find(
        ctx, dictionary, key, &rect_obj);
    if (!present || !pdf_is_array(ctx, rect_obj) ||
        pdf_array_len(ctx, rect_obj) != 4)
        return QUANTAPDF_ERROR_FORMAT;

    for (index = 0; index < 4; ++index) {
        pdf_obj *value = pdf_array_get(ctx, rect_obj, index);
        if (!pdf_is_number(ctx, value))
            return QUANTAPDF_ERROR_FORMAT;
        values[index] = pdf_to_real(ctx, value);
        if (!isfinite(values[index]))
            return QUANTAPDF_ERROR_FORMAT;
    }

    raw.x0 = values[0] < values[2] ? values[0] : values[2];
    raw.x1 = values[0] < values[2] ? values[2] : values[0];
    raw.y0 = values[1] < values[3] ? values[1] : values[3];
    raw.y1 = values[1] < values[3] ? values[3] : values[1];
    transformed = fz_transform_rect(raw, page_ctm);

    if (!isfinite(transformed.x0) || !isfinite(transformed.y0) ||
        !isfinite(transformed.x1) || !isfinite(transformed.y1))
        return QUANTAPDF_ERROR_FORMAT;

    out_rect->x0 = transformed.x0 < transformed.x1 ?
        transformed.x0 : transformed.x1;
    out_rect->x1 = transformed.x0 < transformed.x1 ?
        transformed.x1 : transformed.x0;
    out_rect->y0 = transformed.y0 < transformed.y1 ?
        transformed.y0 : transformed.y1;
    out_rect->y1 = transformed.y0 < transformed.y1 ?
        transformed.y1 : transformed.y0;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_read_optional_uint32(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    uint32_t missing_value,
    uint32_t *out_value)
{
    pdf_obj *value_obj = NULL;
    int64_t value;
    int present;

    *out_value = missing_value;
    present = quantapdf_pdf_dict_find(
        ctx, dictionary, key, &value_obj);
    if (!present)
        return QUANTAPDF_OK;
    if (!pdf_is_int(ctx, value_obj))
        return QUANTAPDF_ERROR_FORMAT;

    value = pdf_to_int64(ctx, value_obj);
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return QUANTAPDF_ERROR_FORMAT;

    *out_value = (uint32_t)value;
    return QUANTAPDF_OK;
}
