#include "pdf_crop_internal.h"
#include "pdf_rewrite_security.h"

#include <math.h>
#include <stddef.h>

quantapdf_status quantapdf_pdf_crop_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    return quantapdf_pdf_rewrite_check_security(ctx, document);
}

static int quantapdf_pdf_crop_finite_public_rect(quantapdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1);
}

static int quantapdf_pdf_crop_rect_equal(
    quantapdf_rect left,
    quantapdf_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int quantapdf_pdf_crop_public_inside(
    quantapdf_rect inner,
    quantapdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

quantapdf_status quantapdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_pdf_crop_plan *plans,
    int *out_any_changed)
{
    size_t index;
    const size_t minimum_size = QUANTAPDF_PAGE_CROP_V1_MIN_SIZE;
    const size_t element_size = QUANTAPDF_PAGE_CROP_V1_SIZE;

    if (out_any_changed != NULL)
        *out_any_changed = 0;
    if (ctx == NULL || document == NULL || crops == NULL ||
        crop_count == 0 || plans == NULL || out_any_changed == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    for (index = 0; index < crop_count; ++index) {
        quantapdf_pdf_page_box_view view;
        fz_rect public_rect;
        fz_rect requested_pdf;
        fz_matrix public_to_pdf;
        quantapdf_status status;
        size_t prior;

        if (crops[index].struct_size < minimum_size ||
            crops[index].struct_size > element_size)
            return QUANTAPDF_ERROR_ARGUMENT;
        if (!quantapdf_pdf_crop_finite_public_rect(crops[index].bounds) ||
            !(crops[index].bounds.x0 < crops[index].bounds.x1) ||
            !(crops[index].bounds.y0 < crops[index].bounds.y1))
            return QUANTAPDF_ERROR_ARGUMENT;

        for (prior = 0; prior < index; ++prior) {
            if (crops[prior].page_index == crops[index].page_index)
                return QUANTAPDF_ERROR_ARGUMENT;
        }

        status = quantapdf_pdf_page_box_resolve(
            ctx, document, crops[index].page_index, &view);
        if (status != QUANTAPDF_OK)
            return status;
        if (!quantapdf_pdf_crop_public_inside(
                crops[index].bounds, view.visible_public))
            return QUANTAPDF_ERROR_ARGUMENT;

        plans[index].page_index = crops[index].page_index;
        plans[index].requested_public = crops[index].bounds;
        plans[index].changed = !quantapdf_pdf_crop_rect_equal(
            crops[index].bounds, view.visible_public);

        public_rect.x0 = crops[index].bounds.x0;
        public_rect.y0 = crops[index].bounds.y0;
        public_rect.x1 = crops[index].bounds.x1;
        public_rect.y1 = crops[index].bounds.y1;
        public_to_pdf = fz_invert_matrix(view.pdf_to_public);
        requested_pdf = fz_transform_rect(public_rect, public_to_pdf);
        if (!isfinite(requested_pdf.x0) || !isfinite(requested_pdf.y0) ||
            !isfinite(requested_pdf.x1) || !isfinite(requested_pdf.y1))
            return QUANTAPDF_ERROR_ARGUMENT;

        plans[index].requested_pdf.x0 = fminf(
            requested_pdf.x0, requested_pdf.x1);
        plans[index].requested_pdf.y0 = fminf(
            requested_pdf.y0, requested_pdf.y1);
        plans[index].requested_pdf.x1 = fmaxf(
            requested_pdf.x0, requested_pdf.x1);
        plans[index].requested_pdf.y1 = fmaxf(
            requested_pdf.y0, requested_pdf.y1);

        if (!(plans[index].requested_pdf.x0 < plans[index].requested_pdf.x1) ||
            !(plans[index].requested_pdf.y0 < plans[index].requested_pdf.y1) ||
            plans[index].requested_pdf.x0 < view.visible_pdf.x0 ||
            plans[index].requested_pdf.y0 < view.visible_pdf.y0 ||
            plans[index].requested_pdf.x1 > view.visible_pdf.x1 ||
            plans[index].requested_pdf.y1 > view.visible_pdf.y1)
            return QUANTAPDF_ERROR_ARGUMENT;

        if (plans[index].changed)
            *out_any_changed = 1;
    }

    return QUANTAPDF_OK;
}
