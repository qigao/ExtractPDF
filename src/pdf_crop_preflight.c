#include "pdf_crop_internal.h"

#include <math.h>
#include <stddef.h>

static int extractpdf_pdf_crop_dict_has_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key)
{
    int count;
    int index;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key))
            return 1;
    }
    return 0;
}

typedef struct extractpdf_pdf_crop_signature_scan {
    pdf_document *document;
    int has_signed_field;
} extractpdf_pdf_crop_signature_scan;

static void extractpdf_pdf_crop_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    extractpdf_pdf_crop_signature_scan *scan =
        (extractpdf_pdf_crop_signature_scan *)data;

    if (scan->has_signed_field)
        return;
    if (!pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static int extractpdf_pdf_crop_has_signed_field(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    extractpdf_pdf_crop_signature_scan scan;
    pdf_obj *field_type = NULL;
    pdf_obj *fields;

    scan.document = document;
    scan.has_signed_field = 0;
    fields = pdf_dict_getp(
        ctx,
        pdf_trailer(ctx, document),
        "Root/AcroForm/Fields");
    pdf_walk_tree(
        ctx,
        fields,
        PDF_NAME(Kids),
        extractpdf_pdf_crop_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field;
}

extractpdf_status extractpdf_pdf_crop_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    int encrypted = 0;
    int signed_field = 0;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(encrypted);
    fz_var(signed_field);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf_obj *trailer = pdf_trailer(ctx, document);
        encrypted = extractpdf_pdf_crop_dict_has_key(
            ctx, trailer, PDF_NAME(Encrypt));
        if (!encrypted)
            signed_field = extractpdf_pdf_crop_has_signed_field(ctx, document);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (encrypted || signed_field)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_crop_finite_public_rect(extractpdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1);
}

static int extractpdf_pdf_crop_rect_equal(
    extractpdf_rect left,
    extractpdf_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int extractpdf_pdf_crop_public_inside(
    extractpdf_rect inner,
    extractpdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

extractpdf_status extractpdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_pdf_crop_plan *plans,
    int *out_any_changed)
{
    size_t index;
    const size_t minimum_size =
        offsetof(extractpdf_page_crop, bounds) + sizeof(extractpdf_rect);

    if (out_any_changed != NULL)
        *out_any_changed = 0;
    if (ctx == NULL || document == NULL || crops == NULL ||
        crop_count == 0 || plans == NULL || out_any_changed == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    for (index = 0; index < crop_count; ++index) {
        extractpdf_pdf_page_box_view view;
        fz_rect public_rect;
        fz_rect requested_pdf;
        fz_matrix public_to_pdf;
        extractpdf_status status;
        size_t prior;

        if (crops[index].struct_size < minimum_size)
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (!extractpdf_pdf_crop_finite_public_rect(crops[index].bounds) ||
            !(crops[index].bounds.x0 < crops[index].bounds.x1) ||
            !(crops[index].bounds.y0 < crops[index].bounds.y1))
            return EXTRACTPDF_ERROR_ARGUMENT;

        for (prior = 0; prior < index; ++prior) {
            if (crops[prior].page_index == crops[index].page_index)
                return EXTRACTPDF_ERROR_ARGUMENT;
        }

        status = extractpdf_pdf_page_box_resolve(
            ctx, document, crops[index].page_index, &view);
        if (status != EXTRACTPDF_OK)
            return status;
        if (!extractpdf_pdf_crop_public_inside(
                crops[index].bounds, view.visible_public))
            return EXTRACTPDF_ERROR_ARGUMENT;

        plans[index].page_index = crops[index].page_index;
        plans[index].requested_public = crops[index].bounds;
        plans[index].changed = !extractpdf_pdf_crop_rect_equal(
            crops[index].bounds, view.visible_public);

        public_rect.x0 = crops[index].bounds.x0;
        public_rect.y0 = crops[index].bounds.y0;
        public_rect.x1 = crops[index].bounds.x1;
        public_rect.y1 = crops[index].bounds.y1;
        public_to_pdf = fz_invert_matrix(view.pdf_to_public);
        requested_pdf = fz_transform_rect(public_rect, public_to_pdf);
        if (!isfinite(requested_pdf.x0) || !isfinite(requested_pdf.y0) ||
            !isfinite(requested_pdf.x1) || !isfinite(requested_pdf.y1))
            return EXTRACTPDF_ERROR_ARGUMENT;

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
            return EXTRACTPDF_ERROR_ARGUMENT;

        if (plans[index].changed)
            *out_any_changed = 1;
    }

    return EXTRACTPDF_OK;
}
