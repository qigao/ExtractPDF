#include "pdf_page_box_common.h"

#include <math.h>
#include <string.h>

#define QUANTAPDF_PDF_PAGE_BOX_MAX_PAGE_TREE_DEPTH 256

static quantapdf_status quantapdf_pdf_page_box_parse_box(
    fz_context *ctx,
    pdf_obj *object,
    fz_rect *out_rect)
{
    float x0;
    float y0;
    float x1;
    float y1;

    if (!pdf_is_array(ctx, object) || pdf_array_len(ctx, object) != 4)
        return QUANTAPDF_ERROR_FORMAT;
    if (!pdf_is_number(ctx, pdf_array_get(ctx, object, 0)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 1)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 2)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 3)))
        return QUANTAPDF_ERROR_FORMAT;

    x0 = pdf_to_real(ctx, pdf_array_get(ctx, object, 0));
    y0 = pdf_to_real(ctx, pdf_array_get(ctx, object, 1));
    x1 = pdf_to_real(ctx, pdf_array_get(ctx, object, 2));
    y1 = pdf_to_real(ctx, pdf_array_get(ctx, object, 3));
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return QUANTAPDF_ERROR_FORMAT;

    out_rect->x0 = fminf(x0, x1);
    out_rect->y0 = fminf(y0, y1);
    out_rect->x1 = fmaxf(x0, x1);
    out_rect->y1 = fmaxf(y0, y1);
    if (!(out_rect->x0 < out_rect->x1) ||
        !(out_rect->y0 < out_rect->y1))
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

static int quantapdf_pdf_page_box_same_object(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return pdf_objcmp_resolve(ctx, left, right) == 0;
}

static quantapdf_status quantapdf_pdf_page_box_public_rect(
    fz_rect raw,
    fz_matrix pdf_to_public,
    quantapdf_rect *out_public)
{
    fz_rect transformed = fz_transform_rect(raw, pdf_to_public);

    if (!isfinite(transformed.x0) || !isfinite(transformed.y0) ||
        !isfinite(transformed.x1) || !isfinite(transformed.y1))
        return QUANTAPDF_ERROR_FORMAT;

    out_public->x0 = fminf(transformed.x0, transformed.x1);
    out_public->y0 = fminf(transformed.y0, transformed.y1);
    out_public->x1 = fmaxf(transformed.x0, transformed.x1);
    out_public->y1 = fmaxf(transformed.y0, transformed.y1);
    if (!(out_public->x0 < out_public->x1) ||
        !(out_public->y0 < out_public->y1))
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_page_box_resolve_imp(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    quantapdf_pdf_page_box_view *out_view)
{
    pdf_obj *page_obj;
    pdf_obj *node;
    pdf_obj *seen[QUANTAPDF_PDF_PAGE_BOX_MAX_PAGE_TREE_DEPTH + 1];
    size_t seen_count = 0;
    pdf_obj *media_obj = NULL;
    pdf_obj *crop_obj = NULL;
    pdf_obj *rotate_obj = NULL;
    pdf_obj *user_unit_obj;
    int page_count;
    int rotate = 0;
    float user_unit = 1.0f;
    size_t depth;
    quantapdf_status status;

    page_count = pdf_count_pages(ctx, document);
    if (page_index < 0 || page_index >= page_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    page_obj = pdf_lookup_page_obj(ctx, document, page_index);
    if (!pdf_is_dict(ctx, page_obj))
        return QUANTAPDF_ERROR_FORMAT;

    node = page_obj;
    for (depth = 0;; ++depth) {
        pdf_obj *parent;
        size_t index;

        if (depth > QUANTAPDF_PDF_PAGE_BOX_MAX_PAGE_TREE_DEPTH)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!pdf_is_dict(ctx, node))
            return QUANTAPDF_ERROR_FORMAT;

        for (index = 0; index < seen_count; ++index) {
            if (quantapdf_pdf_page_box_same_object(ctx, seen[index], node))
                return QUANTAPDF_ERROR_FORMAT;
        }
        seen[seen_count++] = node;

        if (media_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(MediaBox));
            if (value != NULL)
                media_obj = value;
        }
        if (crop_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(CropBox));
            if (value != NULL)
                crop_obj = value;
        }
        if (rotate_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(Rotate));
            if (value != NULL)
                rotate_obj = value;
        }

        parent = pdf_dict_get(ctx, node, PDF_NAME(Parent));
        if (parent == NULL || pdf_is_null(ctx, parent))
            break;
        node = parent;
    }

    if (media_obj == NULL)
        return QUANTAPDF_ERROR_FORMAT;
    status = quantapdf_pdf_page_box_parse_box(
        ctx, media_obj, &out_view->media_pdf);
    if (status != QUANTAPDF_OK)
        return status;

    out_view->has_explicit_crop = crop_obj != NULL;
    if (crop_obj == NULL) {
        out_view->crop_pdf = out_view->media_pdf;
    }
    else {
        status = quantapdf_pdf_page_box_parse_box(
            ctx, crop_obj, &out_view->crop_pdf);
        if (status != QUANTAPDF_OK)
            return status;
    }

    out_view->visible_pdf.x0 = fmaxf(
        out_view->media_pdf.x0, out_view->crop_pdf.x0);
    out_view->visible_pdf.y0 = fmaxf(
        out_view->media_pdf.y0, out_view->crop_pdf.y0);
    out_view->visible_pdf.x1 = fminf(
        out_view->media_pdf.x1, out_view->crop_pdf.x1);
    out_view->visible_pdf.y1 = fminf(
        out_view->media_pdf.y1, out_view->crop_pdf.y1);
    if (!(out_view->visible_pdf.x0 < out_view->visible_pdf.x1) ||
        !(out_view->visible_pdf.y0 < out_view->visible_pdf.y1))
        return QUANTAPDF_ERROR_FORMAT;

    if (rotate_obj != NULL) {
        if (!pdf_is_int(ctx, rotate_obj))
            return QUANTAPDF_ERROR_FORMAT;
        rotate = pdf_to_int(ctx, rotate_obj);
        if (rotate % 90 != 0)
            return QUANTAPDF_ERROR_FORMAT;
        rotate %= 360;
        if (rotate < 0)
            rotate += 360;
    }

    user_unit_obj = pdf_dict_get(ctx, page_obj, PDF_NAME(UserUnit));
    if (user_unit_obj != NULL) {
        if (!pdf_is_number(ctx, user_unit_obj))
            return QUANTAPDF_ERROR_FORMAT;
        user_unit = pdf_to_real(ctx, user_unit_obj);
        if (!isfinite(user_unit) || !(user_unit > 0.0f))
            return QUANTAPDF_ERROR_FORMAT;
    }

    pdf_page_obj_transform(
        ctx, page_obj, NULL, &out_view->pdf_to_public);

    status = quantapdf_pdf_page_box_public_rect(
        out_view->media_pdf, out_view->pdf_to_public,
        &out_view->media_public);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_pdf_page_box_public_rect(
        out_view->visible_pdf, out_view->pdf_to_public,
        &out_view->visible_public);
    if (status != QUANTAPDF_OK)
        return status;

    out_view->page_obj = page_obj;
    out_view->rotate_degrees = rotate;
    out_view->user_unit = user_unit;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_page_box_resolve(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    quantapdf_pdf_page_box_view *out_view)
{
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL || out_view == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_view, 0, sizeof(*out_view));

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = quantapdf_pdf_page_box_resolve_imp(
            ctx, document, page_index, out_view);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    return status;
}
