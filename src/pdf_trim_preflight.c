#include "pdf_trim_internal.h"
#include "pdf_rewrite_security.h"

#include <math.h>
#include <stddef.h>

quantapdf_status quantapdf_pdf_trim_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    return quantapdf_pdf_rewrite_check_security(ctx, document);
}

static int quantapdf_pdf_trim_finite_public_rect(quantapdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1);
}

static int quantapdf_pdf_trim_public_equal(
    quantapdf_rect left,
    quantapdf_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int quantapdf_pdf_trim_public_inside(
    quantapdf_rect inner,
    quantapdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static int quantapdf_pdf_trim_raw_inside(fz_rect inner, fz_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static int quantapdf_pdf_trim_raw_equal(fz_rect left, fz_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static fz_rect quantapdf_pdf_trim_normalize(fz_rect rect)
{
    fz_rect result;

    result.x0 = fminf(rect.x0, rect.x1);
    result.y0 = fminf(rect.y0, rect.y1);
    result.x1 = fmaxf(rect.x0, rect.x1);
    result.y1 = fmaxf(rect.y0, rect.y1);
    return result;
}

static fz_rect quantapdf_pdf_trim_intersection(fz_rect left, fz_rect right)
{
    fz_rect result;

    result.x0 = fmaxf(left.x0, right.x0);
    result.y0 = fmaxf(left.y0, right.y0);
    result.x1 = fminf(left.x1, right.x1);
    result.y1 = fminf(left.y1, right.y1);
    return result;
}

static int quantapdf_pdf_trim_positive_raw(fz_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1) &&
        rect.x0 < rect.x1 && rect.y0 < rect.y1;
}

quantapdf_status quantapdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_pdf_trim_plan *plans,
    int *out_any_changed)
{
    const size_t minimum_size = QUANTAPDF_PAGE_TRIM_V1_MIN_SIZE;
    const size_t element_size = QUANTAPDF_PAGE_TRIM_V1_SIZE;
    size_t index;

    if (out_any_changed != NULL)
        *out_any_changed = 0;
    if (ctx == NULL || document == NULL || trims == NULL ||
        trim_count == 0 || plans == NULL || out_any_changed == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    for (index = 0; index < trim_count; ++index) {
        quantapdf_pdf_page_box_view view;
        fz_rect public_rect;
        fz_rect requested_media_pdf;
        fz_matrix public_to_pdf;
        quantapdf_status status;
        size_t prior;

        if (trims[index].struct_size < minimum_size ||
            trims[index].struct_size > element_size)
            return QUANTAPDF_ERROR_ARGUMENT;
        if (!quantapdf_pdf_trim_finite_public_rect(trims[index].bounds) ||
            !(trims[index].bounds.x0 < trims[index].bounds.x1) ||
            !(trims[index].bounds.y0 < trims[index].bounds.y1))
            return QUANTAPDF_ERROR_ARGUMENT;

        for (prior = 0; prior < index; ++prior) {
            if (trims[prior].page_index == trims[index].page_index)
                return QUANTAPDF_ERROR_ARGUMENT;
        }

        status = quantapdf_pdf_page_box_resolve(
            ctx, document, trims[index].page_index, &view);
        if (status != QUANTAPDF_OK)
            return status;
        if (!quantapdf_pdf_trim_public_inside(
                trims[index].bounds, view.media_public))
            return QUANTAPDF_ERROR_ARGUMENT;

        public_rect.x0 = trims[index].bounds.x0;
        public_rect.y0 = trims[index].bounds.y0;
        public_rect.x1 = trims[index].bounds.x1;
        public_rect.y1 = trims[index].bounds.y1;
        public_to_pdf = fz_invert_matrix(view.pdf_to_public);
        requested_media_pdf = quantapdf_pdf_trim_normalize(
            fz_transform_rect(public_rect, public_to_pdf));

        if (!quantapdf_pdf_trim_positive_raw(requested_media_pdf) ||
            !quantapdf_pdf_trim_raw_inside(
                requested_media_pdf, view.media_pdf))
            return QUANTAPDF_ERROR_ARGUMENT;

        plans[index].page_index = trims[index].page_index;
        plans[index].requested_public = trims[index].bounds;
        plans[index].requested_media_pdf = requested_media_pdf;
        plans[index].changed = !quantapdf_pdf_trim_public_equal(
            trims[index].bounds, view.media_public);

        if (view.has_explicit_crop) {
            plans[index].output_visible_pdf = quantapdf_pdf_trim_intersection(
                requested_media_pdf, view.crop_pdf);
        }
        else {
            plans[index].output_visible_pdf = requested_media_pdf;
        }

        if (!quantapdf_pdf_trim_positive_raw(
                plans[index].output_visible_pdf))
            return plans[index].changed ?
                QUANTAPDF_ERROR_ARGUMENT : QUANTAPDF_ERROR_FORMAT;

        plans[index].frame_changed = !quantapdf_pdf_trim_raw_equal(
            plans[index].output_visible_pdf, view.visible_pdf);
        if (plans[index].changed)
            *out_any_changed = 1;
    }

    return QUANTAPDF_OK;
}
