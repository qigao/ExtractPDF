#include "pdf_appearance_common.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXTRACTPDF_PDF_ANNOT_INVISIBLE (1u << 0)
#define EXTRACTPDF_PDF_ANNOT_HIDDEN (1u << 1)
#define EXTRACTPDF_PDF_ANNOT_NO_ZOOM (1u << 3)
#define EXTRACTPDF_PDF_ANNOT_NO_ROTATE (1u << 4)
#define EXTRACTPDF_PDF_ANNOT_NO_VIEW (1u << 5)
#define EXTRACTPDF_PDF_ANNOT_TOGGLE_NO_VIEW (1u << 8)

static extractpdf_status appearance_read_array(
    fz_context *ctx,
    pdf_obj *value,
    int count,
    float *out_values)
{
    int index;

    if (!pdf_is_array(ctx, value) || pdf_array_len(ctx, value) != count)
        return EXTRACTPDF_ERROR_FORMAT;
    for (index = 0; index < count; ++index) {
        pdf_obj *item = pdf_array_get(ctx, value, index);
        if (!pdf_is_number(ctx, item))
            return EXTRACTPDF_ERROR_FORMAT;
        out_values[index] = pdf_to_real(ctx, item);
        if (!isfinite(out_values[index]))
            return EXTRACTPDF_ERROR_FORMAT;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status appearance_read_rect(
    fz_context *ctx,
    pdf_obj *annotation,
    fz_rect *out_rect)
{
    pdf_obj *rect = NULL;
    float values[4];
    extractpdf_status status;

    if (!extractpdf_pdf_dict_find(ctx, annotation, PDF_NAME(Rect), &rect))
        return EXTRACTPDF_ERROR_FORMAT;
    status = appearance_read_array(ctx, rect, 4, values);
    if (status != EXTRACTPDF_OK)
        return status;

    out_rect->x0 = fminf(values[0], values[2]);
    out_rect->x1 = fmaxf(values[0], values[2]);
    out_rect->y0 = fminf(values[1], values[3]);
    out_rect->y1 = fmaxf(values[1], values[3]);
    return EXTRACTPDF_OK;
}

static extractpdf_status appearance_read_bbox(
    fz_context *ctx,
    pdf_obj *form,
    fz_rect *out_bbox)
{
    pdf_obj *bbox = NULL;
    float values[4];
    extractpdf_status status;

    if (!extractpdf_pdf_dict_find(ctx, form, PDF_NAME(BBox), &bbox))
        return EXTRACTPDF_ERROR_FORMAT;
    status = appearance_read_array(ctx, bbox, 4, values);
    if (status != EXTRACTPDF_OK)
        return status;

    out_bbox->x0 = fminf(values[0], values[2]);
    out_bbox->x1 = fmaxf(values[0], values[2]);
    out_bbox->y0 = fminf(values[1], values[3]);
    out_bbox->y1 = fmaxf(values[1], values[3]);
    if (!(out_bbox->x0 < out_bbox->x1) || !(out_bbox->y0 < out_bbox->y1))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static extractpdf_status appearance_read_matrix(
    fz_context *ctx,
    pdf_obj *form,
    fz_matrix *out_matrix)
{
    pdf_obj *matrix = NULL;
    float values[6];
    extractpdf_status status;

    *out_matrix = fz_identity;
    if (!extractpdf_pdf_dict_find(ctx, form, PDF_NAME(Matrix), &matrix))
        return EXTRACTPDF_OK;
    status = appearance_read_array(ctx, matrix, 6, values);
    if (status != EXTRACTPDF_OK)
        return status;
    *out_matrix = fz_make_matrix(
        values[0], values[1], values[2], values[3], values[4], values[5]);
    return EXTRACTPDF_OK;
}

static extractpdf_status appearance_select_form(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_pdf_appearance_view *view,
    pdf_obj **out_form)
{
    pdf_obj *ap = NULL;
    pdf_obj *normal = NULL;

    *out_form = NULL;
    if (!extractpdf_pdf_dict_find(ctx, annotation, PDF_NAME(AP), &ap))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (!pdf_is_dict(ctx, ap))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, ap, PDF_NAME(N), &normal))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    if (pdf_is_stream(ctx, normal)) {
        if (!pdf_is_indirect(ctx, normal))
            return EXTRACTPDF_ERROR_FORMAT;
        *out_form = normal;
        return EXTRACTPDF_OK;
    }

    if (pdf_is_dict(ctx, normal)) {
        pdf_obj *as = NULL;
        pdf_obj *selected;
        const char *name;
        size_t size;

        if (!extractpdf_pdf_dict_find(ctx, annotation, PDF_NAME(AS), &as))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        if (!pdf_is_name(ctx, as))
            return EXTRACTPDF_ERROR_FORMAT;
        selected = pdf_dict_get(ctx, normal, as);
        if (selected == NULL || pdf_is_null(ctx, selected) ||
            !pdf_is_indirect(ctx, selected) || !pdf_is_stream(ctx, selected))
            return EXTRACTPDF_ERROR_FORMAT;

        name = pdf_to_name(ctx, as);
        if (name == NULL)
            return EXTRACTPDF_ERROR_FORMAT;
        size = strlen(name);
        view->state_name = (char *)malloc(size + 1);
        if (view->state_name == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        memcpy(view->state_name, name, size + 1);
        view->state_name_size = size;
        view->stateful = 1;
        *out_form = selected;
        return EXTRACTPDF_OK;
    }

    return EXTRACTPDF_ERROR_FORMAT;
}

static extractpdf_status appearance_validate_form(
    fz_context *ctx,
    pdf_obj *form,
    extractpdf_pdf_appearance_view *view)
{
    pdf_obj *subtype = NULL;
    pdf_obj *resources = NULL;
    pdf_obj *oc = NULL;
    extractpdf_status status;

    if (!pdf_is_indirect(ctx, form) || !pdf_is_stream(ctx, form))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, form, PDF_NAME(Subtype), &subtype) ||
        !pdf_is_name(ctx, subtype) || !pdf_name_eq(ctx, subtype, PDF_NAME(Form)))
        return EXTRACTPDF_ERROR_FORMAT;

    status = appearance_read_bbox(ctx, form, &view->bbox);
    if (status != EXTRACTPDF_OK)
        return status;
    status = appearance_read_matrix(ctx, form, &view->matrix);
    if (status != EXTRACTPDF_OK)
        return status;

    if (extractpdf_pdf_dict_find(ctx, form, PDF_NAME(Resources), &resources) &&
        !pdf_is_dict(ctx, resources))
        return EXTRACTPDF_ERROR_FORMAT;
    if (extractpdf_pdf_dict_find(ctx, form, PDF_NAME(OC), &oc))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static extractpdf_status appearance_compute_placement(
    const extractpdf_pdf_appearance_view *view,
    fz_matrix *out_placement)
{
    fz_rect transformed = fz_transform_rect(view->bbox, view->matrix);
    fz_rect normalized;
    float width;
    float height;
    float sx;
    float sy;
    float tx;
    float ty;

    if (!isfinite(transformed.x0) || !isfinite(transformed.y0) ||
        !isfinite(transformed.x1) || !isfinite(transformed.y1))
        return EXTRACTPDF_ERROR_FORMAT;

    normalized.x0 = fminf(transformed.x0, transformed.x1);
    normalized.x1 = fmaxf(transformed.x0, transformed.x1);
    normalized.y0 = fminf(transformed.y0, transformed.y1);
    normalized.y1 = fmaxf(transformed.y0, transformed.y1);

    width = normalized.x1 - normalized.x0;
    height = normalized.y1 - normalized.y0;
    if (!(width > 0.0f) || !(height > 0.0f))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    sx = (view->rect.x1 - view->rect.x0) / width;
    sy = (view->rect.y1 - view->rect.y0) / height;
    tx = view->rect.x0 - normalized.x0 * sx;
    ty = view->rect.y0 - normalized.y0 * sy;
    if (!isfinite(sx) || !isfinite(sy) || !isfinite(tx) || !isfinite(ty))
        return EXTRACTPDF_ERROR_FORMAT;

    *out_placement = fz_make_matrix(sx, 0.0f, 0.0f, sy, tx, ty);
    return EXTRACTPDF_OK;
}

void extractpdf_pdf_appearance_drop_view(
    extractpdf_pdf_appearance_view *view)
{
    if (view == NULL)
        return;
    free(view->state_name);
    memset(view, 0, sizeof(*view));
}

extractpdf_status extractpdf_pdf_appearance_resolve(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *annotation,
    extractpdf_pdf_appearance_view *out_view,
    pdf_obj **out_form)
{
    const uint32_t rejected =
        EXTRACTPDF_PDF_ANNOT_INVISIBLE |
        EXTRACTPDF_PDF_ANNOT_HIDDEN |
        EXTRACTPDF_PDF_ANNOT_NO_ZOOM |
        EXTRACTPDF_PDF_ANNOT_NO_ROTATE |
        EXTRACTPDF_PDF_ANNOT_NO_VIEW |
        EXTRACTPDF_PDF_ANNOT_TOGGLE_NO_VIEW;
    pdf_obj *oc = NULL;
    uint32_t flags = 0;
    extractpdf_status status;

    (void)document;
    if (ctx == NULL || annotation == NULL || out_view == NULL || out_form == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    memset(out_view, 0, sizeof(*out_view));
    *out_form = NULL;
    if (!pdf_is_dict(ctx, annotation))
        return EXTRACTPDF_ERROR_FORMAT;

    status = appearance_read_rect(ctx, annotation, &out_view->rect);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = extractpdf_pdf_read_optional_uint32(
        ctx, annotation, PDF_NAME(F), 0, &flags);
    if (status != EXTRACTPDF_OK)
        goto fail;
    if ((flags & rejected) != 0) {
        status = EXTRACTPDF_ERROR_UNSUPPORTED;
        goto fail;
    }
    if (extractpdf_pdf_dict_find(ctx, annotation, PDF_NAME(OC), &oc)) {
        status = EXTRACTPDF_ERROR_UNSUPPORTED;
        goto fail;
    }

    status = appearance_select_form(ctx, annotation, out_view, out_form);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = appearance_validate_form(ctx, *out_form, out_view);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = appearance_compute_placement(out_view, &out_view->placement);
    if (status != EXTRACTPDF_OK)
        goto fail;
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_appearance_drop_view(out_view);
    *out_form = NULL;
    return status;
}
